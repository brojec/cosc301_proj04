#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include "network.h"

struct node{
	int sock;
	char* ip;
	int port;
	struct node* next;
	struct node* prev;

};

// global variable; can't be avoided because
// of asynchronous signal interaction
int still_running = TRUE;
void signal_handler(int sig) {
    still_running = FALSE;
}
int queuecount = 0;
struct node* head = NULL;
struct node* tail = NULL;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

void usage(const char *progname) {
    fprintf(stderr, "usage: %s [-p port] [-t numthreads]\n", progname);
    fprintf(stderr, "\tport number defaults to 3000 if not specified.\n");
    fprintf(stderr, "\tnumber of threads is 1 by default.\n");
    exit(0);
}

void queue_add_head(int sock, char* ip, int port){
	struct node* newnode = (struct node*)malloc(sizeof(struct node));
	newnode->sock = sock;
	newnode->prev = NULL;
	newnode->ip = ip;
	newnode->port = port;
	pthread_mutex_lock(&lock);
	if(head){
		newnode->next = head->next;
		head->next->prev = newnode;
		if(!tail)
			tail = head;
	}else{
		newnode->next = NULL;
		if(!tail)
			tail = newnode;
	}
	head = newnode;
	queuecount++;
	pthread_mutex_unlock(&lock);
}

void* worker_start(void* info){
	while(TRUE){
		pthread_mutex_lock(&lock);
		while(queuecount==0){
			pthread_cond_wait(&cond, &lock);
		}
		printf("processing request\n");
		if(head==tail)
			head = NULL;
		struct node* request = tail;
		tail = tail->prev;
		queuecount--;
		pthread_mutex_unlock(&lock);
		char* filename = (char*)malloc(sizeof(char)*1024);
		getrequest(request->sock, filename, 1024);
		//ignore leading '/'
		if(filename[0] == '/'){
			memmove(filename, filename+1, sizeof(filename)-1);
		}
		FILE* request_file = fopen(filename, "r");
		struct stat fstats;
		stat(filename, &fstats);
		char* header;
		char* request_result;
		if(request_file){
			int strsize = fstats.st_size + strlen(HTTP_200) + 1;
			header = (char*)malloc(strsize);
			header[strsize-1] = '\0';
			fread(header, 1, strsize-1, request_file);
			fclose(request_file);
			request_result = "200";		
		}else{
			header = HTTP_404;
			request_result = "404";	
		}
		pthread_mutex_lock(&file_lock);
		FILE* log = fopen("weblog.txt","a");
		if(senddata(request->sock, header, strlen(header))){
			time_t now = time(NULL);
			fprintf(log, "%s:%d %s \"GET %s\" %s %ld\n",request->ip,request->port, ctime(&now), filename, request_result, fstats.st_size); 
		}
		pthread_mutex_unlock(&file_lock);
		free(request);
		if(header!=HTTP_404){
			free(header);
		}
	}


}


void runserver(int numthreads, unsigned short serverport) {
///////////////////////////////////////////////////////////////
	int i=0;
	for(;i<numthreads;i++){
		pthread_t thread;
		pthread_create(&thread,NULL, &worker_start, NULL); 
	}
    	printf("made threads\n");
//////////////////////////////////////////////////////////////   
    int main_socket = prepare_server_socket(serverport);
    if (main_socket < 0) {
        exit(-1);
    }
    signal(SIGINT, signal_handler);

    struct sockaddr_in client_address;
    socklen_t addr_len;

    fprintf(stderr, "Server listening on port %d.  Going into request loop.\n", serverport);
    while (still_running) {
        struct pollfd pfd = {main_socket, POLLIN};
        int prv = poll(&pfd, 1, 10000);

        if (prv == 0) {
            continue;
        } else if (prv < 0) {
            PRINT_ERROR("poll");
            still_running = FALSE;
            continue;
        }
        
        addr_len = sizeof(client_address);
        memset(&client_address, 0, addr_len);

        int new_sock = accept(main_socket, (struct sockaddr *)&client_address, &addr_len);
        if (new_sock > 0) {
            
            time_t now = time(NULL);
            char* ip = inet_ntoa(client_address.sin_addr);
            int port = ntohs(client_address.sin_port);
            fprintf(stderr, "Got connection from %s:%d at %s\n", ip, port, ctime(&now));
/////////////////////////////////////////////////////////////////////
           queue_add_head(new_sock, ip, port);
           printf("added to queue\n");
           pthread_cond_signal(&cond);
/////////////////////////////////////////////////////////////////////
        }
    }
    fprintf(stderr, "Server shutting down.\n");
        
    close(main_socket);
}


int main(int argc, char **argv) {
    unsigned short port = 3000;
    int num_threads = 1;

    int c;
    while (-1 != (c = getopt(argc, argv, "hp:t:"))) {
        switch(c) {
            case 'p':
                port = atoi(optarg);
                if (port < 1024) {
                    usage(argv[0]);
                }
                break;

            case 't':
                num_threads = atoi(optarg);
                if (num_threads < 1) {
                    usage(argv[0]);
                }
                break;
            case 'h':
            default:
                usage(argv[0]);
                break;
        }
    }

    runserver(num_threads, port);
    
    fprintf(stderr, "Server done.\n");
    exit(0);
}
