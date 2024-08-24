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

#define MYPORT "9000"
#define BACKLOG 10
#define DATA_STREAM "/var/tmp/aesdsocketdata"
#define MAX_BUFFER_SIZE 100

int sockfd = -1;
int accepted_sockfd = -1;
FILE* fptr = NULL;

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
    if (accepted_sockfd != -1) close(accepted_sockfd);
    remove(DATA_STREAM);
    closelog();
    exit(status);
}

/**
 * @brief Handle connection when client connect and send data to setup socket
 * Open the data path file and append data from client to setup socket
 * Then send back all the data has received up to now from data file back to the client
*/
void handle_client_connection(void) {
    char stream_buf[MAX_BUFFER_SIZE];

    fptr = fopen(DATA_STREAM, "a");
    if (fptr == NULL) {
        syslog(LOG_ERR, "Error to opening file %s", DATA_STREAM);
        clean_and_exit(EXIT_FAILURE);
    }

    // Receive data over connection from client and appends
    // packet data to file DATA_STREAM with newline character
    while(1) {
        ssize_t numbytes;
        if ((numbytes = recv(accepted_sockfd, stream_buf, MAX_BUFFER_SIZE-1, 0)) == -1)
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
    
    // Returns the full content of DATA_STREAM to the client as soon as
    // client complete sending packets
    fptr = fopen(DATA_STREAM, "r");
    while(fgets(stream_buf, MAX_BUFFER_SIZE, fptr) !=  NULL) {
        if (send(accepted_sockfd, stream_buf, strlen(stream_buf), 0) == -1) {
            syslog(LOG_ERR, "Error while sending data");
            clean_and_exit(EXIT_FAILURE);
        }
    }
    fclose(fptr);
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

    // Main loop to accept and handle client connections
    while (1) {
        struct sockaddr_storage their_addr;
        socklen_t addr_size = sizeof(their_addr);
        char ipstr[INET6_ADDRSTRLEN];

        accepted_sockfd = accept(sockfd, (struct sockaddr *) &their_addr, &addr_size);
        if (accepted_sockfd == -1) {
            syslog(LOG_ERR, "Error while accept the socket: %m");
            clean_and_exit(EXIT_FAILURE);
        }
        
        // Logging client connection success
        const char* client_addr =  inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), ipstr, sizeof ipstr);
        syslog(LOG_INFO, "Accepted connection from %s\n", client_addr);

        handle_client_connection();

        close(accepted_sockfd);
        syslog(LOG_INFO, "Closed connection from %s\n", client_addr);
        
        fptr = NULL;
    }
    clean_and_exit(EXIT_SUCCESS);
}