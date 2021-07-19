#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <memory.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <signal.h>

#define N 256
#define MAX_STUDENTS 100 //Leonid told us that the maximum students is 100

static pthread_t id_thread[5];

typedef struct user {
    char *ID;
    char *password;
    char *grade; // -1 for TA
} user;

typedef struct threadStruct {
    char *ID;
    int permission; // 0 for student || 1 for TA
} threadStruct;

// A linked list node in the queue - client connection
typedef struct clicon {
    int id_sock;
    struct clicon* next;
} clicon;

// A task queue - queue of client connections
typedef struct taskQueue {
    clicon *front, *rear;
} taskQueue;

typedef struct argStruct {

    user **ta_arr;
    int *num_ta;
    user **st_arr;
    int *num_st;
    taskQueue *task_queue;
    pthread_mutex_t *m;
    pthread_cond_t *c;
    pthread_mutex_t *m_thread;
} argStruct;

#define DO_SYS(syscall) do { \
    if( (syscall) == -1 ) { \
        perror( #syscall ); \
        exit(EXIT_FAILURE); \
    } \
} while( 0 )

struct addrinfo*
alloc_tcp_addr(const char *host, uint16_t port, int flags)
{
    int err;
    struct addrinfo hint, *a;
    char ps[16];

    snprintf(ps, sizeof(ps), "%hu", port);
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

int tcp_establish(int port) {
    int srvfd;
    struct addrinfo *a =
   alloc_tcp_addr(NULL/*host*/, port, AI_PASSIVE);
    DO_SYS( srvfd = socket( a->ai_family,
                            a->ai_socktype,
                            a->ai_protocol ) );
    DO_SYS( bind( srvfd,
                            a->ai_addr,
                            a->ai_addrlen  ) );
    DO_SYS( listen( srvfd, 5/*backlog*/   ) );
    freeaddrinfo( a );
    return srvfd;
}


// Create a new linked list node.
clicon* newConnection(int id_sock)
{
    clicon* temp = (clicon*)malloc(sizeof(clicon));
    temp->id_sock = id_sock;
    temp->next = NULL;
    return temp;
}

// Create an empty queue
taskQueue* createQueue()
{
    taskQueue* q = (taskQueue*)malloc(sizeof(taskQueue));
    q->front = NULL;
    q->rear = NULL;
    return q;
}
// The function to add a id_sock to taskQueue
void enQueue(taskQueue* q, int id_sock)
{
    // Create a new LL node
    clicon* temp = newConnection(id_sock);
    // If queue is empty, then new node is both front and rear
    if (q->rear == NULL) {
        q->front = temp;
        q->rear = temp;
        return;
    }
    // Add the new node at the end of queue and change rear
    q->rear->next = temp;
    q->rear = temp;
}

// Function to remove a key from given queue q
int deQueue(taskQueue* q)
{
    // If queue is empty, return 1.
    if (q->front == NULL)
        return 1;

    // Store previous front and move front one node ahead
    clicon* temp = q->front;

    q->front = q->front->next;

    // If front becomes NULL, then change rear also as NULL
    if (q->front == NULL)
        q->rear = NULL;
    int id_sock = 0;
    id_sock = temp->id_sock;
    free(temp);
    return id_sock;
}

// Defining comparator function for qsort
static int compare(const void* id1, const void* id2)
{
    // setting up rules for comparison
    return strcmp(*(const char**)id1, *(const char**)id2);
}

char *grade_list(threadStruct *thread_s, user **st_arr, int *num_st)
{
    char *output;
    if(NULL == (output = malloc(((16*(*num_st))+1)*sizeof(char)))) {
        exit(1);
    }
    memset(output,'\0',(16*(*num_st))+1);
    if(strcmp(thread_s->ID, "0") == 0) //No one is logedin
    {
        strcpy(output, "Not logged in\n");
        return output;
    }
    if(thread_s->permission == 0) //student
    {
        strcpy(output, "Action not allowed\n");
        return output;
    } else //TA
    {
        char **arr;
        if(NULL == (arr = malloc((*num_st)*sizeof(char*)))) {
            exit(1);
        }
        for(int j = 0; j < *num_st; j++) {
            if(NULL == (arr[j] = malloc(16*sizeof(char)))) {
                exit(1);
            }
            memset(arr[j],'\0',16);
        }
        for(int i = 0; i < *num_st; i++)
        {
            strcpy(arr[i], (st_arr[i]->ID));
            strcat(arr[i], ": ");
            strcat(arr[i], (st_arr[i]->grade));
            strcat(arr[i], "\n");
        }
        qsort(arr, *num_st, sizeof(char*), compare);
        if(*num_st != 0) {
            strcpy(output, arr[0]);
            for(int j = 1; j < *num_st; j++)
            {
                strcat(output, arr[j]);
            }
        } else {
            strcpy(output, "\n");
        }
        return output;
    }
}

void update_grade(threadStruct *thread_s, char *input, user **st_arr, int *num_st)
{
    char id[10];
    char grade[4];
    int j = 0, k = 0;
    memset(id,'\0',10);
    memset(grade,'\0',4);
    while(input[j] != ' ') {
        j++;
    }
    j++;
    while(input[j] != ' ' && k < 9) {
        id[k] = input[j];
        k++;
        j++;
    }
    j++;
    id[k] = '\0';
    k = 0;
    while(input[j] != '\n' && input[j] != '\0' && k < 3) {
        grade[k] = input[j];
        k++;
        j++;
    }
    grade[k] = '\0';
    for(int i = 0; i < *num_st; i++) {
        if(strcmp(id, st_arr[i]->ID) == 0) {
            strcpy(st_arr[i]->grade, grade);
            return;
        }
    }
    //if we arrived here, than we need to add a new student
    if(NULL == (st_arr[(*num_st)] = (user *)malloc(sizeof(user))))
    {
        exit(1);
    }
    if(NULL == (st_arr[(*num_st)]->ID = (char *)malloc(10*sizeof(char))))
    {
        exit(1);
    }
    if(NULL == (st_arr[(*num_st)]->grade = (char *)malloc(4*sizeof(char))))
    {
        exit(1);
    }
    if(NULL == (st_arr[(*num_st)]->password = (char *)malloc(N*sizeof(char))))
    {
        exit(1);
    }
    memset(st_arr[(*num_st)]->ID,'\0',10);
    memset(st_arr[(*num_st)]->grade,'\0',4);
    memset(st_arr[(*num_st)]->password,'\0',N);
    strcpy(st_arr[(*num_st)]->ID, id);
    strcpy(st_arr[(*num_st)]->grade, grade);
    strcpy(st_arr[(*num_st)]->password, "");
    *num_st = (*num_st)+1;
}

char *read_grade(char *input, threadStruct *thread_s, user **st_arr, int *num_st)
{
    char *output;
    if(NULL == (output = malloc(20*sizeof(char)))) {
        exit(1);
    }
    memset(output,'\0',20);
    if(strcmp(thread_s->ID, "0") == 0) //No one is logedin
    {
        strcpy(output, "Not logged in\n");
        return output;
    }
    if(thread_s->permission == 0) //student
    {
        if(strcmp(input, "ReadGrade") != 0)
        {
            strcpy(output, "Action not allowed\n");
            return output;
        } else {
            for(int i = 0; i < *num_st; i++) {
                if(strcmp(thread_s->ID, st_arr[i]->ID) == 0) {
                    strcpy(output, st_arr[i]->grade);
                    strcat(output, "\n");
                    return output;
                }
            }
        }
    } else //TA
        {
            if(strcmp(input, "ReadGrade") == 0)
            {
                strcpy(output, "Missing argument\n");
                return output;
            } else {
                int i = 0, j = 0;
                char id[10];
                // copy the requested student id to id parameter
                while(input[i] != ' ' && input[i] != '\0')
                {
                    i++;
                }
                //if there is only one word, or two parameters without a space
                if(input[i] == '\0'){
                    strcpy(output, "Action not allowed\n");
                    return output;
                }
                i++;
                while(input[i] != '\0' && j < 9)
                {
                    id[j] = input[i];
                    i++;
                    j++;
                }
                id[j] = '\0';
                // find the relevant student in st_arr
                for(int k = 0; k < *num_st; k++)
                {
                    if(strcmp(st_arr[k]->ID, id) == 0)
                    {
                        strcpy(output, st_arr[k]->grade);
                        strcat(output, "\n");
                        return output;
                    }
                }
                strcpy(output, "Invalid id\n");
                return output;
            }
        }
}

char *Login(char *input, threadStruct *thread_s, user **st_arr, int *num_st, user **ta_arr, int *num_ta)
{
    char *output;
    if(NULL == (output = malloc(27*sizeof(char)))) {
        exit(1);
    }
    memset(output,'\0',27);
    char id[10];
    char password[N];
    memset(id,'\0',10);
    memset(password,'\0',N);
    int j = 0, k = 0;
    while(input[j] != ' ') {
        j++;
    }
    j++;
    while(input[j] != ' ' && k < 9) {
        id[k] = input[j];
        k++;
        j++;
    }
    j++;
    k = 0;
    while(input[j] != '\n' && input[j] != '\0') {
        password[k] = input[j];
        k++;
        j++;
    }
    if(0 != strcmp(thread_s->ID, "0"))
    {
        strcpy(output, "Wrong user information\n");
        return output;
    }
    for(int i = 0; i < *num_st; i++) {
        if(strcmp(id, st_arr[i]->ID) == 0) { //ID of a student
            if(0 == strcmp(password, st_arr[i]->password)) //correct password
            {
                strcpy(thread_s->ID, id);
                thread_s->permission = 0;
                strcpy(output, "Welcome Student ");
                strcat(output, id);
                strcat(output, "\n");
                return output;
            } else { //Wrong password
                strcpy(output, "Wrong user information\n");
                return output;
                }
        }
    }
    for(int i = 0; i < *num_ta; i++) {
        if(strcmp(id, ta_arr[i]->ID) == 0) { //ID of a TA
            if(0 == strcmp(password, ta_arr[i]->password)) //correct password
            {
                strcpy(thread_s->ID, id);
                thread_s->permission = 1;
                strcpy(output, "Welcome TA ");
                strcat(output, id);
                strcat(output, "\n");
                return output;
            } else { //Wrong password
                strcpy(output, "Wrong user information\n");
                return output;
            }
        }
    }
    //if ID doesn't exist in the server
    strcpy(output, "Wrong user information\n");
    return output;
}

char *Logout(threadStruct *thread_s)
{
    char *output;
    if(NULL == (output = malloc(20*sizeof(char)))) {
        exit(1);
    }
    memset(output,'\0',20);
    if(0 == strcmp(thread_s->ID, "0")) //No one is logged in
    {
        strcpy(output, "Not logged in\n");
        return output;
    } else {
        strcpy(output, "Good bye ");
        strcat(output, thread_s->ID);
        strcat(output, "\n");
        strcpy(thread_s->ID, "0");
        thread_s->permission = -1;
        return output;
    }
}

void *thread_func(void *args1)
{
    argStruct *args = (argStruct *)args1;
    for(;;)
    {
        int id_sock, count = 0;
        int c1, c2, c3, c4, c5, c6;
        char *input, buf[12];
        if(NULL == (input = malloc(N*sizeof(char))))
        {
            exit(1);
        }
        // Condition variable
        pthread_mutex_lock(args->m);
        while(args->task_queue->front == NULL) {
            pthread_cond_wait(args->c, args->m);
        }
        id_sock = deQueue(args->task_queue);
        pthread_mutex_unlock(args->m);
        // initialize thread connection struct
        threadStruct *thread_s;
        if(NULL == (thread_s = malloc(sizeof(threadStruct))))
        {
            exit(1);
        }
        if(NULL == (thread_s->ID = malloc(10*sizeof(char))))
        {
            exit(1);
        }
        memset(thread_s->ID,'\0',10);
        strcpy(thread_s->ID, "0");
        thread_s->permission = -1;
        for(;;) //commands from client
        {
            c1 =-1;
            c2 =-1;
            c3=-1;
            c4=-1;
            c5=-1;
            c6=-1;
            count = 0;
            memset(input, '\0', N);
            memset(buf, '\0', 12);
            DO_SYS(read(id_sock, input, N));
            for( int i=0; i< strlen(input);i++)
            {
                if (input[i] == '\n') {
                    input[i] = '\0';
                }
            }
            char *output;
            // copy the first word from input to buf
            while(input[count] != ' ' && input[count] != '\0' && input[count] != '\n')
            {
                buf[count] = input[count];
                count++;
            }
            // find out which command the user asked for
            c1 = strcmp(buf, "Login");
            c2 = strcmp(buf, "ReadGrade");
            c3 = strcmp(buf, "GradeList");
            c4 = strcmp(buf, "UpdateGrade");
            c5 = strcmp(buf, "Logout");
            c6 = strcmp(buf, "Exit");
            if(c1 == 0) //Login
            {
                pthread_mutex_lock(args->m_thread);
                output = Login(input, thread_s, args->st_arr, args->num_st, args->ta_arr, args->num_ta);
                DO_SYS(write(id_sock, output, 27));
                pthread_mutex_unlock(args->m_thread);
                free(output);
            } else if(c2 == 0) //ReadGrade
            {
                pthread_mutex_lock(args->m_thread);
                output = read_grade(input, thread_s, args->st_arr, args->num_st);
                DO_SYS(write(id_sock, output, 20*sizeof(char)));
                pthread_mutex_unlock(args->m_thread);
                free(output);
            } else if(c3 == 0) //GradeList
            {
                pthread_mutex_lock(args->m_thread);
                output = grade_list(thread_s, args->st_arr, args->num_st);
                if ( strcmp(output, "Action not allowed\n") == 0 || strcmp(output, "Not logged in\n") == 0 )
                {
                    DO_SYS(write(id_sock, "000000", 7));
                    DO_SYS(write(id_sock, output, 20));
                }
                else {
                    int num = (16*(*(args->num_st)))+1;
                    char number[7];
                    memset(number,'\0',7);
                    sprintf(number,"%d",num);
                    DO_SYS(write(id_sock, number , 7));
                    DO_SYS(write(id_sock, output, ((16*(*(args->num_st)))+1)*sizeof(char)));
                }
                pthread_mutex_unlock(args->m_thread);
                free(output);
            } else if(c4 == 0) //UpdateGrade
            {
                pthread_mutex_lock(args->m_thread);
                char output1[20];
                //No one is logedin
                if(strcmp(thread_s->ID, "0") == 0)
                {
                    strcpy(output1, "Not logged in\n");
                    DO_SYS(write(id_sock, output1, 20*sizeof(char)));
                }
                //if student
                else if(thread_s->permission == 0)
                {
                    strcpy(output1,"Action not allowed\n");
                    DO_SYS(write(id_sock, output1, 20*sizeof(char)));
                }
                // if TA
                else {
                    update_grade(thread_s, input, args->st_arr, args->num_st);
                    DO_SYS(write(id_sock, "", 1));
                }
                pthread_mutex_unlock(args->m_thread);
            } else if(c5 == 0) //Logout
            {
                output = Logout(thread_s);
                DO_SYS(write(id_sock, output, 20*sizeof(char)));
                free(output);
            } else //Exit
            {
                if(0 != strcmp(thread_s->ID, "0")) //Logout from user
                {
                    strcpy(thread_s->ID, "0");
                    thread_s->permission = -1;
                }
                DO_SYS(write(id_sock, "", 1));
                DO_SYS(close(id_sock));
                break;
            }
        }
        free(input);
    }
    return NULL;
}

//count how many lines in the file:
int count_users(char filename[15])
{
    FILE *fp;
    char temp[N];
    int count = 0;
    if (NULL == (fp = fopen(filename, "r")))  {
          exit(1);
    }
    fgets(temp, N, fp);
    count += 1;
    while(!feof(fp))
    {
        count += 1;
        fgets(temp, N, fp);
    }
    fclose(fp);
    return (count-1);
}

    //parameter num: (0) for student || (1) for TA
user **initialize_data(char filename[15], int *size, int num)
{
    FILE *fp;
    char temp[N];
    int i_user = 0, i = 0;
    user **arr;
    if (0 == num){ //student
        if(NULL == (arr = malloc((MAX_STUDENTS)*sizeof(user*))))
        {
            exit(1);
        }
    }
    else { //TA
        if(NULL == (arr = malloc((*size)*sizeof(user*))))
        {
            exit(1);
        }
    }
    for(i = 0; i < *size; i++)
    {
        if(NULL == (arr[i] = malloc(sizeof(user))))
        {
            exit(1);
        }
        if(NULL == (arr[i]->ID = malloc(10*sizeof(char))))
        {
            exit(1);
        }
        memset(arr[i]->ID,'\0',10);
        if(NULL == (arr[i]->password = malloc(N*sizeof(char))))
        {
            exit(1);
        }
        memset(arr[i]->password,'\0',N);
        if(NULL == (arr[i]->grade = malloc(4*sizeof(char))))
        {
            exit(1);
        }
        memset(arr[i]->grade,'\0',4);
    }
    if (NULL == (fp = fopen(filename, "r")))  {
          exit(1) ;
    }
    fgets(temp, N, fp);
    while(!feof(fp))
    {
        int i_temp = 0, i_pass = 0;
        //read the ID of user:
        while(temp[i_temp]!= ':')
        {
            arr[i_user]->ID[i_temp] = temp[i_temp];
            i_temp++;
        }
        arr[i_user]->ID[i_temp] = '\0';
        i_temp++;
        //read the password of user:
        while(temp[i_temp]!= '\0' && temp[i_temp]!='\n')
        {
            arr[i_user]->password[i_pass] = temp[i_temp];
            i_pass++;
            i_temp++;
        }
        arr[i_user]->password[i_pass] = '\0';
        if(0 == num) //student
        {
            strcpy(arr[i_user]->grade, "0");
        } else //TA
        {
            strcpy(arr[i_user]->grade, "-1");
        }
        i_user++;
        fgets(temp, N, fp);
    }
    fclose(fp);
    return arr;
}

void free_mem(argStruct *args)
{
    for(int i = 0; i<*(args->num_ta); i++){
        free(args->ta_arr[i]->ID);
        free(args->ta_arr[i]->password);
        free(args->ta_arr[i]->grade);
        free(args->ta_arr[i]);
    }
    free(args->ta_arr);
    for(int i = 0; i<*(args->num_st); i++){
        free(args->st_arr[i]->ID);
        free(args->st_arr[i]->password);
        free(args->st_arr[i]->grade);
        free(args->st_arr[i]);
    }
    free(args->st_arr);
    free(args->num_ta);
    free(args->num_st);
    DO_SYS(pthread_mutex_destroy(args->m));
    free(args->m);
    DO_SYS(pthread_mutex_destroy(args->m_thread));
    free(args->m_thread);
    DO_SYS(pthread_cond_destroy(args->c));
    free(args->c);
    while(args->task_queue->front != NULL) {
        deQueue(args->task_queue);
    }
    free(args->task_queue);
    free(args);
}

// function for main thread
void GradeServer(int port)
{
    char buf[N];
    int k, clifd, srvfd = tcp_establish(port), func;
    int *num_st, *num_ta;
    user **ta_arr, **st_arr;
    argStruct *args;
    // initialize struct for pthread_create function's arguments
    if(NULL == (args = malloc(sizeof(argStruct)))) {
        exit(1);
    }
    // initialize the server's data
    if(NULL == (num_st = malloc(sizeof(int))))
    {
        exit(1);
    }
    if(NULL == (num_ta = malloc(sizeof(int))))
    {
        exit(1);
    }
    *num_st = count_users("students.txt");
    *num_ta = count_users("assistants.txt");
    //third parameter: (0) for students || (1) for TA
    ta_arr = initialize_data("assistants.txt", num_ta, 1);
    st_arr = initialize_data("students.txt", num_st, 0);
    // initialize task queue
    taskQueue *task_queue = createQueue();
    // initialize mutex for the queue
    pthread_mutex_t *m;
    if(NULL == (m = malloc(sizeof(pthread_mutex_t))))
    {
        exit(1);
    }
    DO_SYS(pthread_mutex_init(m, NULL));
    //initialize mutex for the pool thread
    pthread_mutex_t *m_thread;
    if(NULL == (m_thread = malloc(sizeof(pthread_mutex_t))))
    {
        exit(1);
    }
    DO_SYS(pthread_mutex_init(m_thread, NULL));
    // initialize condition variable for the queue
    pthread_cond_t *c;
    if(NULL == (c = malloc(sizeof(pthread_cond_t))))
    {
        exit(1);
    }
    DO_SYS(pthread_cond_init(c, NULL));
    args->ta_arr = ta_arr;
    args->num_ta = num_ta;
    args->st_arr = st_arr;
    args->num_st = num_st;
    args->task_queue = task_queue;
    args->m = m;
    args->c = c;
    args->m_thread = m_thread;
    for(int i = 0; i<5; i++) {
        DO_SYS(pthread_create(&id_thread[i], NULL, thread_func, (void *)args));
    }
    for(;;) {
        DO_SYS(clifd = accept(srvfd, NULL, NULL));
        pthread_mutex_lock(m);
        enQueue(task_queue, clifd);
        pthread_cond_signal(c);
        pthread_mutex_unlock(m);
    }
}

void sighandler(int signum)
{
    for (int i =0;i < 5; i++) {
        pthread_kill(id_thread[i], signum);
    }
    exit(1);
}




int main(int argc,char* argv[]) {
  int port;
  signal(SIGINT,sighandler);
  if( argc < 2 ) {
    write(1,"Wrong Input\n", 13);
    exit(1);
  }
  port= atoi(argv[1]);
  GradeServer(port);
  return 0;
}
