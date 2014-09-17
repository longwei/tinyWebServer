#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <netdb.h>
#include <sys/wait.h>

#define VERSION 777
#define BUFSIZE 8096
#define ERROR      42
#define LOG        44
#define FORBIDDEN 403
#define NOTFOUND  404
#define PORT "8081"  // the port users will be connecting to
#define BACKLOG 10   // how many pending connections queue will hold

static const char FORBIDDEN_RESPOND[] =
"HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n"
"<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\n"
"The requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n";

static const char NOT_FOUND_RESPOND[] =
"HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n"
"<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\n"
"The requested URL was not found on this server.\n</body></html>\n";

static const char HEADER[] =
"HTTP/1.1 200 OK\nServer: tiny/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n";


struct {
    char *ext;
    char *filetype;
} extensions[] = {
        {"gif", "image/gif"},
        {"jpg", "image/jpg"},
        {"jpeg", "image/jpeg"},
        {"png", "image/png"},
        {"ico", "image/ico"},
        {"zip", "image/zip"},
        {"gz", "image/gz"},
        {"tar", "image/tar"},
        {"htm", "text/html"},
        {"html", "text/html"},
        {0, 0}};


void sigchld_handler(int s) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *) sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *) sa)->sin6_addr);
}

void logger(int type, char *s1, char *s2, int socket_fd) {
    int log_fd;
    char logbuffer[BUFSIZE + 1];
    time_t rawtime;
    struct tm *timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    switch (type) {
        case ERROR:
            snprintf(logbuffer, BUFSIZE, "%sERROR: %s:%s Errno=%d exiting pid=%d", asctime(timeinfo), s1, s2, errno, getpid());
            break;
        case FORBIDDEN:
            send(socket_fd, FORBIDDEN_RESPOND, strlen(FORBIDDEN_RESPOND),0);
            snprintf(logbuffer, BUFSIZE, "%sFORBIDDEN: %s:%s", asctime(timeinfo), s1, s2);
            break;
        case NOTFOUND:
            send(socket_fd, NOT_FOUND_RESPOND, strlen(NOT_FOUND_RESPOND),0);
            snprintf(logbuffer, BUFSIZE, "%sNOT FOUND: %s:%s", asctime(timeinfo), s1, s2);
            break;
        case LOG:
            snprintf(logbuffer, BUFSIZE, "INFO %s%s: %s\n", asctime(timeinfo), s1, s2);
            break;
    }

    if ((log_fd = open("tiny.log", O_CREAT | O_WRONLY | O_APPEND, 0644)) >= 0) {
        write(log_fd, logbuffer, strlen(logbuffer));
        close(log_fd);
    }
    if (type == ERROR || type == NOTFOUND || type == FORBIDDEN) exit(3);
}

/* this is a child web server process, so we can exit on errors */
void web(int fd) {
    int j, file_fd, buflen;
    long i, ret, len;
    char *fstr;
    static char buffer[BUFSIZE + 1]; /* static so zero filled */

    //just assume request could be read in one read
    ret = recv(fd, buffer, BUFSIZE, 0);
    if (ret == 0 || ret == -1) {    /* read failure stop now */
        logger(FORBIDDEN, "failed to read browser request", "", fd);
    }
    if (ret > 0 && ret < BUFSIZE)    /* return code is valid chars */
        buffer[ret] = 0;        /* terminate the buffer */
    else buffer[0] = 0;
    /* remove CF and LF characters */
    for (i = 0; i < ret; i++)
        if (buffer[i] == '\r' || buffer[i] == '\n')
            buffer[i] = '*';
    logger(LOG, "request", buffer, fd);
    if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4)) {
        logger(FORBIDDEN, "Only simple GET operation supported", buffer, fd);
    }
    for (i = 4; i < BUFSIZE; i++) { // null terminate after the second space to ignore extra stuff
        if (buffer[i] == ' ') { // string is "GET URL " +lots of other stuff
            buffer[i] = 0;
            break;
        }
    }
    //todo
    // "//etc/foobar" will bypass this check
    // check for illegal parent directory use ..
    for (j = 0; j < i - 1; j++)
        if (buffer[j] == '.' && buffer[j + 1] == '.') {
            logger(FORBIDDEN, "Parent directory (..) path names not supported", buffer, fd);
        }

    if (!strncmp(&buffer[0], "GET /\0", 6) || !strncmp(&buffer[0], "get /\0", 6)) /* convert no filename to index file */
        strcpy(buffer, "GET /index.html");

    // double check the file type
    buflen = strlen(buffer);
    fstr = (char *) 0;
    for (i = 0; extensions[i].ext != 0; i++) {
        len = strlen(extensions[i].ext);
        if (!strncmp(&buffer[buflen - len], extensions[i].ext, len)) {
            fstr = extensions[i].filetype;
            break;
        }
    }
    if (fstr == 0) logger(FORBIDDEN, "file extension type not supported", buffer, fd);

    if ((file_fd = open(&buffer[5], O_RDONLY)) == -1) {  /* open the file for reading */
        logger(NOTFOUND, "failed to open file", &buffer[5], fd);
    }
    logger(LOG, "SEND", &buffer[5], fd);
    //lseek to the file end to find the length
    len = (long) lseek(file_fd, (off_t) 0, SEEK_END);
    //lseek back to the file start ready for reading
    lseek(file_fd, (off_t) 0, SEEK_SET);
    snprintf(buffer, BUFSIZE, HEADER, VERSION, len, fstr); /* Header + a blank line */
    logger(LOG, "Header", buffer, fd);
    //sending header
    send(fd, buffer, strlen(buffer),0);

    //send content in 8KB block - last block may be smaller
    while ((ret = read(file_fd, buffer, BUFSIZE)) > 0) {
        send(fd, buffer, ret,0);
    }
    //some os may not wait for socket to finished sending the data but drop connection
    sleep(1);
    close(fd);
    exit(1);
}

int main(int argc, char **argv) {
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;
    int pid;
    // parent returns OK to shell
    if (fork() != 0) {
        return 0;
    }
    // break from user's group so log off have no impact
    setpgrp();

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        return 2;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("all good\nserver: waiting for connections...\ndetaching from console\n");

    while (1) {  // main accept() loop
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        if ((pid = fork()) < 0) {
            logger(ERROR, "system call", "fork", 0);
        } else {
            if (pid == 0) {  /* child */
                close(sockfd);
                web(new_fd); /* never returns */
            } else {  /* parent */
                close(new_fd);
            }
        }
    }

    return 0;
}
