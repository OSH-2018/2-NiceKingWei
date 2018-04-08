/*
 * init.c
 * author:nicekingwei
 * 
 * cd /
 * pwd
 * ls
 * ls | wc
 * ls | cat | wc
 * env
 * export MY_OWN_VAR=1
 * env | grep MY_OWN_VAR
 * 
 * echo "x" > x | grep x        pass
 * echo "x3" > x >> y           pass
 * cat x > y | grep x           pass  
 * cat <x                       pass
 * echo "hello" | cat <x <x     pass
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>

#define MAX_ARGV 128
#define MAX_FILE 8
#define MAX_TASK 32
#define MAX_PIPE MAX_TASK*16
#define MAX_BUF 4096

/*
 * utils module
 */
void print_to_error(const char* s){
    write(2,s,strlen(s));
}



/*
 * raw_task module
 * command line string -> raw_buffer
 */
typedef struct raw_task {
    int argc;
    char *argv[MAX_ARGV];

    int fin_count;
    char *fin_path[MAX_FILE];

    int fout_count;
    char *fout_path[MAX_FILE];
    char fout_mode[MAX_FILE][2];
} raw_task;

raw_task raw_buffer[MAX_TASK];
size_t raw_count;

extern inline void raw_task_add_infile(char *file) {
    raw_buffer[raw_count].fin_path[raw_buffer[raw_count].fin_count++] = file;
}

extern inline void raw_task_add_outfile(char *file, char mode) {
    raw_buffer[raw_count].fout_path[raw_buffer[raw_count].fout_count] = file;
    raw_buffer[raw_count].fout_mode[raw_buffer[raw_count].fout_count++][0] = mode;
}

extern inline void raw_task_add_argv(char *arg) {
    raw_buffer[raw_count].argv[raw_buffer[raw_count].argc++] = arg;
}

extern inline void raw_task_new() {
    raw_count++;
}

typedef enum redirect_mode {
    NOTHING, READ, WRITE, APPEND
}redirect_mode;


#define NEW_TASK \
    flag_last_space=1;\
    flag_start_redirect=0;\
    mode=NOTHING;\
    raw_task_new

/* main function of this module */
void raw_task_gen(char *cmd) {

    raw_count = 0;
    memset(raw_buffer, 0, sizeof(raw_buffer));

    char *p = cmd;
    int flag_last_space, flag_start_redirect;
    redirect_mode mode;

    NEW_TASK();

    while (1) {
        if (*p == '\n') {
            *p = 0;
            break;  
        } else if (*p == ' ' || *p == '\t' || *p == '=') {
            *p = 0;
            flag_last_space = 1;
        } else if (*p == '|') {
            NEW_TASK();
            *p = 0;
        } else if (*p == '<' || *p == '>') {
            if (*p == '>') {
                if (*(p + 1) == '>') {
                    *p++ = 0;
                    mode = APPEND;
                } else {
                    mode = WRITE;
                }
            } else {
                mode = READ;
            }
            *p = 0;
            flag_start_redirect = 1;
            flag_last_space = 1;
        } else {
            if (flag_last_space) {
                if (flag_start_redirect) {
                    switch (mode) {
                        case READ:
                            raw_task_add_infile(p);
                            break;
                        case WRITE:
                            raw_task_add_outfile(p, 'w');
                            break;
                        case APPEND:
                            raw_task_add_outfile(p, 'a');
                            break;
                        default:
                            print_to_error("shell: parse error\n");
                            return;
                    }
                } else {
                    raw_task_add_argv(p);
                }
            }
            flag_last_space = 0;
        }
        p++;
    }
}


/*
 * process pool module
 * raw_buffer -> process_pool
 * run the system
 */
typedef struct process {
    int argc;
    char *argv[MAX_ARGV];
}process;
char str_write[] = ">";
char str_read[]  = "<";
process proc_write_file = {3,{str_write}};
process proc_read_file = {2,{str_read}};
process proc_pool[MAX_PIPE];
size_t proc_count;

int pipes[MAX_PIPE][2];
int pipec;

extern inline void close_pipes(){
    for(int i=0;i<pipec;i++){
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

void proc_gen() {
    pipec=0;
    proc_count=0;

    for(int i=1;i<=raw_count;i++){
        raw_task* task = &raw_buffer[i];

        for(int j=0;j<task->fin_count;j++){
            proc_read_file.argv[1] = task->fin_path[j];
            proc_read_file.argv[2] = NULL;
            proc_pool[proc_count++] = proc_read_file;
        }

        proc_pool[proc_count].argc = task->argc;
        for(int j=0;j<task->argc;j++){
            proc_pool[proc_count].argv[j] = task->argv[j];
        }
        proc_pool[proc_count].argv[task->argc] = NULL;
        proc_count++;

        for(int j=0;j<task->fout_count;j++){
            proc_write_file.argv[1] = task->fout_path[j];
            proc_write_file.argv[2] = task->fout_mode[j];
            proc_write_file.argv[3] = NULL;
            proc_pool[proc_count++] = proc_write_file;
        }
    }

    pipec = proc_count-1;
    for(int i=0;i<pipec;i++){
        pipe(pipes[i]);
    }
}

void proc_run_ext(int index) {
    process* proc = &proc_pool[index];

    int *pipe_in,*pipe_out;
    pipe_in = index==0?NULL:pipes[index-1];
    pipe_out = index==pipec?NULL:pipes[index];    

    pid_t pid = fork();
    
    if(pid==0) {
        int flag_stdout=0,flag_stdin=0;

        if (pipe_in) {
            dup2(pipe_in[STDIN_FILENO],STDIN_FILENO);
        } else {
            flag_stdin = 1;
        }

        if (pipe_out) {
            dup2(pipe_out[STDOUT_FILENO],STDOUT_FILENO);
        } else {
            flag_stdout = 1;
        }
        
        close_pipes();
        
        if(strcmp(proc->argv[0],">")==0){
            char* buf = malloc(MAX_BUF);
            FILE* fout = fopen(proc->argv[1],proc->argv[2]);
            while(!feof(stdin)){
                size_t n = fread(buf,sizeof(char),MAX_BUF,stdin);
                if(n>0) {
                    fwrite(buf,sizeof(char),n,fout);
                    if(!flag_stdout) fwrite(buf,sizeof(char),n,stdout);
                }
            }
            fclose(fout);
            free(buf);
            exit(0);
        }else if(strcmp(proc->argv[0],"<")==0){
            char* buf = malloc(MAX_BUF);
            while(!flag_stdin && !feof(stdin)){
                size_t n = fread(buf,sizeof(char),MAX_BUF,stdin);
                if(n>0) {
                    fwrite(buf,sizeof(char),n,stdout);
                }
            }
            FILE* fin = fopen(proc->argv[1],"r");
            while(!feof(fin)){
                size_t n = fread(buf,sizeof(char),MAX_BUF,fin);
                if(n>0) {
                    fwrite(buf,sizeof(char),n,stdout);
                }
            }
            fclose(fin);
            free(buf);
            exit(0);
        }else{
            execvp(proc->argv[0], proc->argv);
        }
        print_to_error(proc->argv[0]);
        print_to_error(": no such command\n");
        exit(0);
    }
}

void proc_run(int index) {

    process* proc = &proc_pool[index];

    if (proc->argc == 0) return;

    if (strcmp(proc->argv[0], "cd") == 0) {
        if (proc->argc == 2) {
            if (chdir(proc->argv[1]) == -1)
                printf("no such file or directory : %s\n", proc->argv[1]);
            return;
        }
    } else if (strcmp(proc->argv[0], "pwd") == 0) {
        if (proc->argc == 1) {
            char* wd = malloc(MAX_BUF);
            puts(getcwd(wd,MAX_BUF));
            free(wd);
            return;
        }
    } else if (strcmp(proc->argv[0], "exit") == 0) {
        if (proc->argc == 1) {
            exit(0);
        }
    } else if (strcmp(proc->argv[0], "export") == 0) {
        if (proc->argc == 3) {
            int r = setenv(proc->argv[1], proc->argv[2], 1);
            if (r) {
                print_to_error("error\n");
            }
            return;
        }
    } else {
        proc_run_ext(index);
        return;
    }

    /* error */
    print_to_error(proc->argv[0]);
    print_to_error(": wrong number of parameters\n");
}

void proc_start(){
    for(int i=0;i<proc_count;i++) proc_run(i);
    for(int i=0;i<pipec;i++){
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}


/*
 * main module
 * loop
 */
char cmd_buffer[MAX_BUF];

int main(){
    while(1) {
        printf("# ");
        
        fflush(stdin);
        fgets(cmd_buffer, MAX_BUF, stdin);
        
        raw_task_gen(cmd_buffer);

        proc_gen();
        proc_start();

        while(wait(NULL)!=-1);
    }
    return 0;
}