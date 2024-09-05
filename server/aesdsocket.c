#include <syslog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/queue.h>

#define MYPORT "9000"
#define BACKLOG 10
#define DATA_STREAM "/var/tmp/aesdsocketdata"
#define MAX_BUFFER_SIZE 100

int sockfd = -1;
pthread_mutex_t file_mutex;
FILE* fptr = NULL;

SLIST_HEAD(slist_head, slist_data_s) head = SLIST_HEAD_INITIALIZER(head);
struct slisthead *headp;

struct connection_data_s {
    int socket_id;
    bool is_done;
    pthread_t thread_id;
    const char* client_addr;
};

struct slist_data_s {
    struct connection_data_s* connection;
    SLIST_ENTRY(slist_data_s) entries;
};

/**
 * @brief Get the right socket address struct based on socket family
 * @param sa generic socket address
*/
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/**
 * @brief Clean up all of the files and sockets and exit the program
 * @param status the code return after terminate the program 
*/
void clean_and_exit(int status) {
    if (fptr != NULL) fclose(fptr);
    if (sockfd != -1) close(sockfd);
    pthread_mutex_destroy(&file_mutex);
    while (SLIST_EMPTY(&head)) {
        struct slist_data_s* tmp = SLIST_FIRST(&head);
        SLIST_REMOVE_HEAD(&head, entries);
        close(tmp->connection->socket_id);
        pthread_join(tmp->connection->thread_id, NULL);
        free(tmp->connection);
        free(tmp);
    }
    closelog();
    exit(status);
}

/**
 * @brief Handle connection when client connect and send data to setup socket
 * Open the data path file and append data from client to setup socket
 * Then send back all the data has received up to now from data file back to the client
*/
void * handle_client_connection(void * accpeted_sock_params) {
    char stream_buf[MAX_BUFFER_SIZE];
    struct connection_data_s * accepted_sock = (struct connection_data_s *) accpeted_sock_params;
    
    // Logging accept client connection success
    syslog(LOG_INFO, "Accepted connection from %s\n", accepted_sock->client_addr);

    pthread_mutex_lock(&file_mutex);
    fptr = fopen(DATA_STREAM, "a");
    if (fptr == NULL) {
        syslog(LOG_ERR, "Error to opening file %s", DATA_STREAM);
        clean_and_exit(EXIT_FAILURE);
    }

    // Receive data over connection from client and appends
    // packet data to file DATA_STREAM with newline character
    while(1) {
        ssize_t numbytes;
        if ((numbytes = recv(accepted_sock->socket_id, stream_buf, MAX_BUFFER_SIZE-1, 0)) == -1)
        {
            syslog(LOG_ERR, "Error while receiving data %m");
            clean_and_exit(EXIT_FAILURE);
        }

        // Check if no data left from client
        if (numbytes == 0) break;

        // Write data to DATA_STREAM and add to the end of buffer null character
        stream_buf[numbytes] = '\0';
        fputs(stream_buf, fptr);

        // New line character detected that inform the end of packet
        if (stream_buf[numbytes-1] =='\n') break;
    }
    fclose(fptr);
    pthread_mutex_unlock(&file_mutex);

    // Returns the full content of DATA_STREAM to the client as soon as
    // client complete sending packets
    pthread_mutex_lock(&file_mutex);
    fptr = fopen(DATA_STREAM, "r");
    while(fgets(stream_buf, MAX_BUFFER_SIZE, fptr) !=  NULL) {
        if (send(accepted_sock->socket_id, stream_buf, strlen(stream_buf), 0) == -1) {
            syslog(LOG_ERR, "Error while sending data");
            clean_and_exit(EXIT_FAILURE);
        }
    }
    fclose(fptr);
    pthread_mutex_unlock(&file_mutex);

    // Logging close client connection success
    close(accepted_sock->socket_id);
    syslog(LOG_INFO, "Closed connection from %s\n", accepted_sock->client_addr);
    accepted_sock->is_done = true;
    fptr = NULL;
    return NULL;
}

/**
 * @brief Handle whenever any expected signal received
*/
static void signal_handler(int signo) {
    syslog(LOG_INFO, "Caught signal %d, exiting", signo);
    clean_and_exit(EXIT_SUCCESS);
}

/**
 * @brief Run the program as daemon
*/
void run_daemon(void) {

    // First fork: Detach the program from terminal and become process group leader
    pid_t pid = fork();
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    } else if (pid == -1) {
        syslog(LOG_ERR, "Error to fork: %m");
        clean_and_exit(EXIT_FAILURE);
    }

    // Create new session that child process become the session leader
    if (setsid() < 0) {
        syslog(LOG_ERR, "Error to setsid %m");
        clean_and_exit(EXIT_FAILURE);
    }

    // Second fork: Ensure the daemon cannot acquire a controlling terminal
    pid = fork();
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    } else if (pid == -1) {
        syslog(LOG_ERR, "Error to fork: %m");
        clean_and_exit(EXIT_FAILURE);
    }

    // Change the working directory to root to avoid keeping any directory in use
    if (chdir("/") < 0) {
        syslog(LOG_ERR, "Error when change the cwd to root: %m");
        exit(EXIT_FAILURE);
    }

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect stdin/out/err to /dev/null
    int dev_null = open("/dev/null", O_RDWR);
    if (dev_null == -1)
    {
        syslog(LOG_ERR, "Error opening /dev/null: %m");
        clean_and_exit(EXIT_FAILURE);
    }

    dup2(dev_null, STDIN_FILENO);
    dup2(dev_null, STDOUT_FILENO);
    dup2(dev_null, STDERR_FILENO);

    // Close the original file descriptor for /dev/null
    close(dev_null);
}

int main(int argc, char *argv[]) {
    // Start logging
    openlog(NULL, 0, LOG_USER);
    remove(DATA_STREAM);
    // Set up signal handling
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = signal_handler;

    if (sigaction(SIGTERM, &sa, NULL) != 0 || sigaction(SIGINT, &sa, NULL) != 0)
    {
        syslog(LOG_ERR, "Error setting up signal handler");
        clean_and_exit(EXIT_FAILURE);
    }

    bool is_daemon = false;
    if ((argc > 1) && (strcmp(argv[1], "-d") == 0)) {
        syslog(LOG_INFO, "Running as daemon");
        is_daemon = true;
    }
    
    struct addrinfo hints;
    struct addrinfo *servinfo;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, MYPORT, &hints, &servinfo) != 0) {
        syslog(LOG_ERR, "Error to get socket address info: %m");
        clean_and_exit(EXIT_FAILURE);
    }

    sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (sockfd == -1) {
        syslog(LOG_ERR, "Error to create new socket: %m");
        freeaddrinfo(servinfo);
        clean_and_exit(EXIT_FAILURE);
    }

    int optval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) != 0)
    {
        syslog(LOG_ERR, "Error setting socket options: %m");
        freeaddrinfo(servinfo);
        clean_and_exit(EXIT_FAILURE);
    }

    if (bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) != 0) {
        syslog(LOG_ERR, "Error to binding the socket: %m");
        freeaddrinfo(servinfo);
        clean_and_exit(EXIT_FAILURE);
    }
    freeaddrinfo(servinfo);

    // If running as a daemon, daemonize before connection after checking binding is successful
    if (is_daemon) run_daemon();
    
    // Start listening for connections
    if (listen(sockfd, BACKLOG) != 0) {
        syslog(LOG_ERR, "Error to listen to the socket: %m");
        clean_and_exit(EXIT_FAILURE);
    }

    // Initialize the mutex lock
    pthread_mutex_init(&file_mutex, NULL);

    // Main loop to accept and handle client connections
    while (1) {
        struct sockaddr_storage their_addr;
        socklen_t addr_size = sizeof(their_addr);
        char ipstr[INET6_ADDRSTRLEN];

        int accepted_sockfd = accept(sockfd, (struct sockaddr *) &their_addr, &addr_size);
        if (accepted_sockfd == -1) {
            syslog(LOG_ERR, "Error while accept the socket: %m");
            clean_and_exit(EXIT_FAILURE);
        }

        const char* client_addr =  inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), ipstr, sizeof ipstr);


        // TODO: Create new thread for each accepted socket
        struct slist_data_s* thread_item = (struct slist_data_s*) malloc(sizeof(struct slist_data_s));
        thread_item->connection = (struct connection_data_s*) malloc(sizeof(struct connection_data_s));
        thread_item->connection->client_addr = client_addr;
        thread_item->connection->socket_id = accepted_sockfd;
        thread_item->connection->is_done = false;
        pthread_create(&thread_item->connection->thread_id, NULL, handle_client_connection, (void *) thread_item->connection);
        
        SLIST_INSERT_HEAD(&head, thread_item, entries);
        struct slist_data_s *current = SLIST_FIRST(&head);
        struct slist_data_s *prev = NULL;
        while (current != NULL) {
            struct slist_data_s *next = SLIST_NEXT(current, entries);
            if (current->connection->is_done) {
                if (prev == NULL) {
                    SLIST_REMOVE_HEAD(&head, entries);
                } else {
                    SLIST_NEXT(prev, entries) = next;
                }
                
                pthread_join(current->connection->thread_id, NULL);
                free(current->connection);
                free(current);
            } else {
                prev = current;
            }
            current = next;
        }
        // handle_client_connection((void*) accepted_sock);
    }
    clean_and_exit(EXIT_SUCCESS);
}