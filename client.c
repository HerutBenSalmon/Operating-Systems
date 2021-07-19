#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <memory.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#define N 256

#define DO_SYS(syscall) do { \
    if( (syscall) == -1 ) { \
        perror( #syscall ); \
        exit(EXIT_FAILURE); \
    } \
} while( 0 )


struct addrinfo*
alloc_tcp_addr(const char *host, uint16_t port, int flags)
{
    int err;   struct addrinfo hint, *a;   char ps[16];

    snprintf(ps, sizeof(ps), "%hu", port); // why string?
    memset(&hint, 0, sizeof(hint));
    hint.ai_flags    = flags;
    hint.ai_family   = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_protocol = IPPROTO_TCP;

    if( (err = getaddrinfo(host, ps, &hint, &a)) != 0 ) {
        fprintf(stderr,"%s\n", gai_strerror(err));
        exit(EXIT_FAILURE);
    }

    return a; // should later be freed with freeaddrinfo()
}


/*
 * Return client fd connect()ed to host+port
 */
int tcp_connect(const char* host, uint16_t port)
{
    int clifd;
    struct addrinfo *a = alloc_tcp_addr(host, port, 0);

    DO_SYS( clifd = socket( a->ai_family,
                            a->ai_socktype,
                            a->ai_protocol ) );
    DO_SYS( connect( clifd,
                            a->ai_addr,
                            a->ai_addrlen  )   );

    freeaddrinfo( a );
    return clifd;
}

//check how many words in a command
int count_words(char *str)
{
    int i = 0, count = 0;
    char s[2] = " ";
    char *token;
    token = strtok(str, s);
    while( token != NULL ) {
    count++;
    token = strtok(NULL, s);
    }
    return count;
}

// check the input of the user:
int check_command (char *input)
{
    int count = 0, c1, c2, c3, c4, c5, c6;
    char buf[256];
    while(input[count] != ' ' && input[count] != '\0' && input[count] != '\n')
    {
        buf[count] = input[count];
        count++;
    }
    buf[count] = '\0';
    c1 = strcmp(buf, "Login");
    c2 = strcmp(buf, "ReadGrade");
    c3 = strcmp(buf, "GradeList");
    c4 = strcmp(buf, "UpdateGrade");
    c5 = strcmp(buf, "Logout");
    c6 = strcmp(buf, "Exit");
    if(c1 == 0 || c2 == 0 || c3 == 0 || c4 == 0 || c5 == 0 || c6 == 0)
    {
        if(c1 == 0 || c4 == 0) { //Login, UpdateGrade
            if(3 != count_words(input)) {
                return 1;
            }
        } else if(c2 == 0) { //ReadGrade
            if(count_words(input) > 2) {
                return 1;
            }
        } else if(c3 == 0 || c5 == 0 || c6 == 0) { //GradeList
            if(1 != count_words(input)) {
                return 1;
            }
        } else {
            return 0;
        }
    } else {
        return 1;
    }
    return 0;
}
// parent - command line interpreter, child - communication process.
void GradeClient(const char *hostname, uint16_t port)
{
    int k=0, fd = tcp_connect(hostname, port);
    pid_t pid_child, pid_par = getpid();
    //ctp = child to parent, ptc = parent to child
    int pipe_ctp[2], pipe_ptc[2], c;
    DO_SYS(pipe(pipe_ctp));
    DO_SYS(pipe(pipe_ptc));
    pid_child = fork();
    if(pid_child > 0) { //parent process
        //close descriptors for parent
        DO_SYS(close(pipe_ptc[0]));
        DO_SYS(close(pipe_ctp[1]));
        while(1)
        {
            char str_input[N], check_input[N], *str_output, check_com[12];
            int i=0;
            if ( NULL == (str_output = malloc(N*sizeof(char)))) {
                exit(1);
            }
            memset(str_input, '\0', N);
            memset(check_input, '\0', N);
            memset(str_output, '\0', N);
            memset(check_com,'\0',12);
            write(1, "> ", 3);
            read(1, str_input, N);
            strcpy(check_input, str_input);
            //check if the input is valid
            if(check_command(check_input) == 1)
            {
                write(0, "Wrong Input\n", 13);
                continue;
            }
            // write input to child using pipe
            DO_SYS(write(pipe_ptc[1], str_input, strlen(str_input)+1));
            // read output from child using pipe
            //check if the command is 'GradeList'
            char number_list[7];
            int num_list;
            memset(number_list,'\0',7);
            while( str_input[i] != ' ' && str_input[i] != '\n') {
                check_com[i] = str_input[i];
                i++;
            }
            if (0 == strcmp(check_com,"GradeList")) {
                DO_SYS(read (pipe_ctp[0],number_list, 7));
                //if it is 'Action not allowed' or 'Not logged in'
                if (0 == strcmp(number_list,"000000")){
                    DO_SYS(read (pipe_ctp[0], str_output, N));
                    write(1, str_output, N);
                }
                // if it is the student list
                else {
                    num_list = atoi(number_list);
                    if(NULL == (str_output = (char *)realloc(str_output,(num_list)*(sizeof(char)))))
                    {
                        exit(1);
                    }
                    DO_SYS(read (pipe_ctp[0], str_output, num_list));
                    write(1, str_output, num_list);
                }
            }
            else {
                DO_SYS(read(pipe_ctp[0], str_output, N));
                // print the output
                if(strlen(str_output) > 0) {
                    write(0, str_output, strlen(str_output));
                }
                if(0 == strcmp("Exit\n", str_input))
                {
                    DO_SYS(close(fd));
                    DO_SYS(kill(pid_child, SIGKILL));
                    return ;
                }
            }
            free(str_output);
        }
    }
    else if (pid_child == 0) // child process
    {
        //close descriptors for child
        DO_SYS(close(pipe_ptc[1]));
        DO_SYS(close(pipe_ctp[0]));
        while(1) {
            char com_buf[N], *server_buf;
            if ( NULL == (server_buf = malloc(N*sizeof(char)))) {
                exit(1);
            }
            memset(com_buf, '\0',N);
            memset(server_buf, '\0',N);
            DO_SYS(k = read(pipe_ptc[0], com_buf, N));
            if(k>0) {
                DO_SYS(write(fd, com_buf, strlen(com_buf)+1));
            } else {
                exit(1);
            }
            // check if the command is 'GradeList'
            int i=0, num_list;
            char check_list[12], number_list[7];
            memset(check_list,'\0',12);
            memset(number_list,'\0',7);
            while( com_buf[i] != ' ' && com_buf[i] != '\n') {
                check_list[i] = com_buf[i];
                i++;
            }
            if (0 == strcmp(check_list,"GradeList")) {
                DO_SYS(read (fd, number_list, 7));
                DO_SYS(write(pipe_ctp[1], number_list, 7));
                //if it is 'Action not allowed' or 'Not logged in'
                if (0 == strcmp(number_list,"000000")){
                    DO_SYS(read (fd, server_buf, N));
                    DO_SYS(write(pipe_ctp[1], server_buf, N));
                }
                // if it is the student list
                else {
                    num_list = atoi(number_list);
                    if(NULL == (server_buf = (char *)realloc(server_buf,(num_list)*(sizeof(char)))))
                    {
                        exit(1);
                    }
                    memset(server_buf,'\0',num_list);
                    DO_SYS(read (fd, server_buf, num_list));
                    DO_SYS(write(pipe_ctp[1], server_buf, num_list));
                }
            }
            else {
            // read from socket
            DO_SYS(read (fd, server_buf, N));
            // write to child using pipe
            DO_SYS(write(pipe_ctp[1], server_buf, N));
            }
            free(server_buf);
        }
    }
}

int main(int argc, char *argv[]) {
    char server_name[N];
    memset(server_name,'\0',N);
    if (argc < 3) {
        printf("Wrong Input\n");
    }
    strcpy(server_name,argv[1]);
    int port = atoi(argv[2]);
    GradeClient(server_name, port);
  return 0;
}

