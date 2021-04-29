#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <sys/wait.h>

#define PRINT "print"
#define ADD "add"
#define CONN "conn"
#define FIFO_NAME "graph.fifo"

#define ERR(source) (perror(source),\
                     fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
                     exit(EXIT_FAILURE))

volatile sig_atomic_t last_signal = 0;

int sethandler( void (*f)(int), int sigNo) {
        struct sigaction act;
        memset(&act, 0, sizeof(struct sigaction));
        act.sa_handler = f;
        if (-1==sigaction(sigNo, &act, NULL))
                return -1;
        return 0;
}

void sig_handler(int sig) {
    last_signal = sig;
}

int digit_counter(int number)
{
    int counter = 0;
    if(number == 0)
        return 1;

    while(number != 0)
    {
        counter++;
        number /= 10;
    }
    return counter;
}

void usage(char * name){
	fprintf(stderr,"USAGE: %s n\n",name);
	exit(EXIT_FAILURE);
}

void read_from_fifo(int fifo, int* vertices_write, int number_process){
    ssize_t count;
    char buffer[PIPE_BUF];
    if(sethandler(sig_handler,SIGINT)) ERR("Setting SIGINT handler in parent");

    for(;;) {
        if(SIGINT==last_signal)
            break;

        count = read(fifo,buffer,PIPE_BUF-1);
        if(count < 0 && errno==EINTR) continue;
        if(count < 0) ERR("read from parent");
        if(count == 0) break;
        buffer[count] = 0;
            
        if(strncmp(buffer, PRINT, strlen(PRINT)) == 0){
            printf("Komenda print\n");        
            unsigned char s = 'p';

            for(int i=0; i<number_process; i++)
                if(TEMP_FAILURE_RETRY(write(vertices_write[i], &s, sizeof(int))<0)) 
                    ERR("write");
            
        }
        else if(strncmp(buffer, ADD, strlen(ADD)) == 0){
            int x_offset = 4;
            int x = atoi(buffer+x_offset);
            int count = digit_counter(x);
            int y = atoi(buffer+x_offset+count);
            if(x >= number_process || x < 0 || y >= number_process || y < 0) {
                printf("Podano nieprawidlowe wierzcholki\n");
                continue;
            }

            int buf[2];
            buf[0] = 'a';
            buf[1] = y;

            if(TEMP_FAILURE_RETRY(write(vertices_write[x], buf, 2*sizeof(int))<0)) ERR("write");
        }
        else if(strncmp(buffer, CONN, strlen(CONN)) == 0){
            int x_offset = 5;
            int x = atoi(buffer+x_offset);
            int count = digit_counter(x);
            int y = atoi(buffer+x_offset+count);
            if(x >= number_process || x < 0 || y >= number_process || y < 0) {
                printf("Podano nieprawidlowe wierzcholki\n");
                continue;
            }

            printf("Komenda conn: %d %d\n", x, y);
            int buf[number_process+4];
            
            memset(buf, 0, sizeof(buf));
            buf[0] = 'c';
            buf[1] = y;
            buf[2] = x;
            buf[3] = 0;

            if(TEMP_FAILURE_RETRY(write(vertices_write[x], buf, (number_process+4)*sizeof(int)))<0) 
                ERR("write");
        }
    }
}

void child_work(int fd_read, int vertice_num, int number_process, int* vertices_write) {

    int command, neigh_num, searched_vert;
    int  is_return = 0, can_move = -1, prev_from = -1, from;
    int neighbours[number_process];
    int visited[number_process+4];
    unsigned char c;

    memset(neighbours, 0, sizeof(neighbours));
    memset(visited, 0, sizeof(visited));

    if(sethandler(sig_handler,SIGINT)) ERR("Setting SIGINT handler in parent");    ssize_t count;
    for(;;) {
        if(SIGINT==last_signal)
            break;
        
        count = read(fd_read, &command, sizeof(int));
        if(count<0 && errno==EINTR) continue;
        if(count < 0) ERR("read from parent");
        if(count == 0)
            break;
        
        c = command;

        switch(c)
        {
            case 'a':
                if((TEMP_FAILURE_RETRY(count = read(fd_read, &neigh_num, sizeof(int))))<0) ERR("read");
                if(neigh_num >= number_process || neigh_num < 0) {
                    printf("Podano nieprawidlowe wierzcholki\n");
                    break;
                }
                printf("Dodano krawedz z [%d] do: [%d]\n", vertice_num, neigh_num);
                neighbours[neigh_num] = 1;
            break;

            case 'c':
                if((TEMP_FAILURE_RETRY(count = read(fd_read, visited+1, sizeof(int)*(number_process+3))))<0) ERR("read");
                searched_vert = visited[1];
                from = visited[2];
                is_return = visited[3];
                

                if(searched_vert >= number_process || searched_vert < 0){
                    printf("Podano nieprawidlowe wierzcholki\n");
                    break;
                }

                if(searched_vert == vertice_num) {
                    printf("Tak\n");
                    break;
                }
                
                if(is_return == 0)
                    prev_from = from;

                can_move = -1;
                visited[vertice_num+4] = 1;
                
                for(int i=0; i<number_process; i++)
                {
                    if(neighbours[i] == 1 && visited[i+4] == 0 && i != vertice_num)
                    {
                        
                        visited[0] = 'c';
                        visited[1] = searched_vert;
                        visited[2] = vertice_num;
                        visited[3] = 0;
                        can_move = 1;
                        count = TEMP_FAILURE_RETRY(write(vertices_write[i], visited, sizeof(int)*(number_process+4)));
                        if(count != sizeof(int)*(number_process+4)) 
                            break;

                        break;
                    }
                }

                if(can_move == -1)
                {
                    if(is_return == 1 && prev_from == vertice_num)
                    {
                        printf("Nie\n");
                        break;
                    } 

                    visited[0] = 'c';
                    visited[1] = searched_vert;
                    visited[2] = vertice_num;
                    visited[3] = 1;
                    count = TEMP_FAILURE_RETRY(write(vertices_write[prev_from], visited, sizeof(int)*(number_process+4)));
                        if(count != sizeof(int)*(number_process+4)) 
                            break;
                }
                
            break;

            case 'p':
                for(int i=0; i<number_process; i++)
                    if(neighbours[i] == 1)
                        printf("Krawedz %d -> %d\n", vertice_num, i);
            break;

            default:
                printf("Podano nie poprawna komende\n");
            break;
        }
    }
}

int main(int argc, char** argv) {
	int fifo, pid, *vertices_write;
    if(argc!=2) usage(argv[0]);
    int number_process = atoi(argv[1]);

    if(sethandler(SIG_IGN,SIGPIPE)) ERR("Setting SIGINT handler");
    if(mkfifo(FIFO_NAME, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP)<0)
    {
        if(errno == EEXIST) 
        {
            if(unlink(FIFO_NAME)<0) ERR("remove fifo:");
            if(mkfifo(FIFO_NAME, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP)<0)
            {
                if(errno != EEXIST) ERR("create");
            }
        }
        else ERR("mkfifo");
    }
    if((fifo=open(FIFO_NAME,O_RDONLY))<0)ERR("open");

    if(NULL==(vertices_write=(int*)malloc(sizeof(int)*number_process))) 
        ERR("malloc");
    
    int *pipes = malloc((2 * number_process) * sizeof(int));
    if (!pipes) 
        ERR("malloc");

    for (int i = 0; i < number_process; i++) {
        if(pipe(&pipes[2*i]) < 0 ) ERR("pipe");
        vertices_write[i] = pipes[(2*i+1)];
    }

    for(int i=0; i<number_process; i++)
    {
        if((pid=fork()) < 0) ERR("fork");
        if(pid == 0)
        {
            int read = pipes[2*i]; 

            for (int j = 0; j < number_process*2; j++) 
                if (pipes[j] != read && j%2==0) 
                    if(TEMP_FAILURE_RETRY(close(pipes[j]))) 
                        ERR("close");
            
            child_work(read, i, number_process, vertices_write);

            for (int j = 0; j < number_process; j++) 
                if(j != i)
                    if(TEMP_FAILURE_RETRY(close(pipes[2*j+1])))
                        ERR("close");
            
            free(vertices_write);
            if(TEMP_FAILURE_RETRY(close(read))) ERR("close");
            exit(EXIT_SUCCESS);
        }       
    }

    for (int i = 0; i < number_process; i++)
        if(TEMP_FAILURE_RETRY(close(pipes[2*i]))) ERR("clsoe");

    read_from_fifo(fifo, vertices_write, number_process);
    
    for (int i = 0; i < number_process; i++)
        if(TEMP_FAILURE_RETRY(close(pipes[2*i+1]))) ERR("clsoe");
    
    if(sethandler(SIG_IGN,SIGINT)) ERR("Setting SIGINT handler");
    if (kill(0, SIGINT)<0) ERR("kill");

    while(wait(NULL)>0);
    
    free(pipes);
    free(vertices_write);

    if(close(fifo)<0) ERR("close fifo:");
    if(unlink(FIFO_NAME)<0) ERR("close fifo:");
	return EXIT_SUCCESS;
}