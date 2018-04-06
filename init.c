/*
 * cd /
 * pwd
 * ls
 * ls | wc
 * ls | cat | wc
 * env
 * export MY_OWN_VAR=1
 * env | grep MY_OWN_VAR
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>


#ifdef DEBUG
    #define MAX_BUF 4
#else
    #define MAX_BUF 4096
#endif    

char buffer[MAX_BUF+1];
int input_filedes[2], output_filedes[2];

/* handle cmd */
int sh(int argc,char* argv[],int in,int out) {
    #ifdef DEBUG
        printf("sh(%d,%d,%d)\n",argc,in,out);

        for(int i=0;i<argc;i++){
            printf("%s\n",argv[i]);
        }
    #endif

    /* no command */
    if (argc==0) return 0;

    /* built-in command */
    int wrong_argc=0,built_in=0;
    if (strcmp(argv[0], "cd") == 0) {
        if (argc==2) {
            if(chdir(argv[1])==-1)
                printf("no such file or directory : %s\n",argv[1]);
        } else {
            wrong_argc = 1;
        }
        built_in=1;
    } else if (strcmp(argv[0], "pwd") == 0) {
        if(argc==1) {
            char wd[4096];  
            puts(getcwd(wd, 4096));
        } else {
            wrong_argc = 1;
        }
        built_in=1;
    } else if (strcmp(argv[0], "exit") == 0) {
        if(argc==1) {
            exit(0);
        } else {
            wrong_argc = 1;
        }
        built_in = 1;
    } else if (strcmp(argv[0], "export") == 0) {
        if(argc==3){
            int r = setenv(argv[1],argv[2],1);
            #ifdef DEBUG
                printf("%s,%s,%d\n",argv[1],argv[2],r);
            #endif
            if(r){
                printf("error\n");
            }
        } else {
            wrong_argc = 1;
        }
        built_in=1;
    }
    if(wrong_argc) {
        printf("%s: wrong number of parameters\n",argv[0]);
        return -1;
    }
    if(built_in) return 0;

    /* external command */
    pid_t pid = fork();
    if (pid == 0) {
        if(in!=0){
            close(0);
            dup(in);
            close(input_filedes[0]);
            close(input_filedes[1]);
        }
        if(out!=1){
            close(1);
            dup(out);
            close(output_filedes[0]);
            close(output_filedes[1]);
        }
        execvp(argv[0], argv);
        write(2,"shell: no such command\n",23);
        exit(0);
    } else {
        if(in!=0) {
            close(input_filedes[0]);
            close(input_filedes[1]);
            pipe(input_filedes);
        }
        wait(NULL);
        if(out!=1) {
            size_t n;
            int j=0;
            do {
                #ifdef DEBUG
                    printf("%d,%d\n",n,j++);
                #endif
                n = read(output_filedes[0],buffer,MAX_BUF);

                if(n<=0) break;
                #ifdef DEBUG
                    printf("%d,%d\n",n,j++);
                #endif
                write(input_filedes[1],buffer,n);

            }while(n==MAX_BUF);
            #ifdef DEBUG
                printf("end loop\n");
            #endif
            close(output_filedes[0]);
            close(output_filedes[1]);
            pipe(output_filedes);
        }
    }
    return 0;
}

int main() {
    while (1) {
        printf("# ");
        fflush(stdin);

        /* read and construct `argc` `argv` */
        char cmd[256];
        fgets(cmd, 256, stdin);

        int taskc=1;
        int taskv[128] = {0};
        int argc=0;
        char *argv[128];

        int last_space=1;
        int end=0;
        char* p=cmd;
        while(!end) {
            if(*p==' ' || *p=='\t' || *p=='\n' || *p=='='){
                if(*p=='\n') end=1;
                *p = 0;
                last_space=1;
            } else if(*p=='|'){
                *p = 0;
                argv[argc++] = NULL;
                taskv[taskc++] = argc;
                last_space=1;
            } else {
                if(last_space){
                    argv[argc++] = p;
                }
                last_space=0;
            }
            p++;
        }
        argv[argc++] = NULL;
        taskv[taskc] = argc;

        /* fork and execute */
        pipe(input_filedes);
        pipe(output_filedes);
        for(int i=0;i<taskc;i++) {
            int in,out;

            if(i==0) in = 0;
            else in = input_filedes[0];
            
            if(i==taskc-1) out = 1;
            else out = output_filedes[1];
            
            sh(taskv[i+1]-taskv[i]-1,&argv[taskv[i]],in,out);
        }
    }
}