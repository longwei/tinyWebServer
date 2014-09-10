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

#define VERSION 23
#define BUFSIZE 8096
#define ERROR      42
#define LOG        44
#define FORBIDDEN 403
#define NOTFOUND  404
#define BACKLOG_PENNDING_CONNECTION 64

struct {
    char *ext;
    char *filetype;
} extensions [] = {
        {"gif", "image/gif" },
        {"jpg", "image/jpg" },
        {"jpeg","image/jpeg"},
        {"png", "image/png" },
        {"ico", "image/ico" },
        {"zip", "image/zip" },
        {"gz",  "image/gz"  },
        {"tar", "image/tar" },
        {"htm", "text/html" },
        {"html","text/html" },
        {0,0} };

void logger(int type, char *s1, char *s2, int socket_fd)
{
    int log_fd ;
    char logbuffer[BUFSIZE+1];
    time_t rawtime;
    struct tm * timeinfo;

    time (&rawtime);
    timeinfo = localtime (&rawtime);

    switch (type) {
        case ERROR: snprintf(logbuffer, BUFSIZE, "%sERROR: %s:%s Errno=%d exiting pid=%d",asctime(timeinfo), s1, s2, errno,getpid());
            break;
        case FORBIDDEN:
            write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n",271);
            snprintf(logbuffer, BUFSIZE, "%sFORBIDDEN: %s:%s",asctime(timeinfo), s1, s2);
            break;
        case NOTFOUND:
            write(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n",224);
            snprintf(logbuffer, BUFSIZE, "%sNOT FOUND: %s:%s",asctime(timeinfo), s1, s2);
            break;
        case LOG: snprintf(logbuffer, BUFSIZE,"%sINFO: %s:%s:%d\n",asctime(timeinfo), s1, s2,socket_fd); break;
    }

    if((log_fd = open("tiny.log", O_CREAT| O_WRONLY | O_APPEND, 0644)) >= 0) {
        write(log_fd,logbuffer,strlen(logbuffer));
        write(log_fd,"\n",1);
        close(log_fd);
    }
    if(type == ERROR || type == NOTFOUND || type == FORBIDDEN) exit(3);
}

/* this is a child web server process, so we can exit on errors */
void web(int fd, int hit)
{
    int j, file_fd, buflen;
    long i, ret, len;
    char * fstr;
    static char buffer[BUFSIZE+1]; /* static so zero filled */

    //just assume request could be read in one read
    ret =read(fd,buffer,BUFSIZE);
    if(ret == 0 || ret == -1) {	/* read failure stop now */
        logger(FORBIDDEN,"failed to read browser request","",fd);
    }
    if(ret > 0 && ret < BUFSIZE)	/* return code is valid chars */
        buffer[ret]=0;		/* terminate the buffer */
    else buffer[0]=0;
    /* remove CF and LF characters */
    for(i=0;i<ret;i++)
        if(buffer[i] == '\r' || buffer[i] == '\n')
            buffer[i]='*';
    logger(LOG,"request",buffer,hit);
    if( strncmp(buffer,"GET ",4) && strncmp(buffer,"get ",4) ) {
        logger(FORBIDDEN,"Only simple GET operation supported",buffer,fd);
    }
    for(i=4;i<BUFSIZE;i++) { /* null terminate after the second space to ignore extra stuff */
        if(buffer[i] == ' ') { /* string is "GET URL " +lots of other stuff */
            buffer[i] = 0;
            break;
        }
    }
    //todo
    // "//etc/foobar" will bypass this check
    // check for illegal parent directory use ..
    for(j=0;j<i-1;j++)
        if(buffer[j] == '.' && buffer[j+1] == '.') {
            logger(FORBIDDEN,"Parent directory (..) path names not supported",buffer,fd);
        }

    if( !strncmp(&buffer[0],"GET /\0",6) || !strncmp(&buffer[0],"get /\0",6) ) /* convert no filename to index file */
        strcpy(buffer,"GET /index.html");

    // double check the file type
    buflen=strlen(buffer);
    fstr = (char *)0;
    for(i=0;extensions[i].ext != 0;i++) {
        len = strlen(extensions[i].ext);
        if( !strncmp(&buffer[buflen-len], extensions[i].ext, len)) {
            fstr =extensions[i].filetype;
            break;
        }
    }
    if(fstr == 0) logger(FORBIDDEN,"file extension type not supported",buffer,fd);

    if(( file_fd = open(&buffer[5],O_RDONLY)) == -1) {  /* open the file for reading */
        logger(NOTFOUND, "failed to open file",&buffer[5],fd);
    }
    logger(LOG,"SEND",&buffer[5],hit);
    //lseek to the file end to find the length
    len = (long)lseek(file_fd, (off_t)0, SEEK_END);
    //lseek back to the file start ready for reading
    lseek(file_fd, (off_t)0, SEEK_SET);
    snprintf(buffer, BUFSIZE,"HTTP/1.1 200 OK\nServer: tiny/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", VERSION, len, fstr); /* Header + a blank line */
    logger(LOG,"Header",buffer,hit);
    //sending header
    write(fd,buffer,strlen(buffer));

    //send content in 8KB block - last block may be smaller
    while (	(ret = read(file_fd, buffer, BUFSIZE)) > 0 ) {
        write(fd,buffer,ret);
    }
    //some os may not wait for socket to finished sending the data but drop connection
    sleep(1);
    close(fd);
    exit(1);
}

int main(int argc, char **argv)
{
    int i, port, pid, listenfd, socketfd, hit;
    socklen_t length;
    static struct sockaddr_in cli_addr;
    static struct sockaddr_in serv_addr;

    argv[1] = "8081";
    argv[2] = ".";
    // Become deamon
    if(fork() != 0)
        return 0;

    signal(SIGCHLD, SIG_IGN); /* ignore child death */
    signal(SIGHUP, SIG_IGN); /* ignore terminal hangups */
    setpgrp();		/* break away from process group */

    logger(LOG,"tiny starting",argv[1],getpid());
    /* setup the network socket */
    if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)
        logger(ERROR, "system call","socket",0);
    port = atoi(argv[1]);
    if(port < 0 || port >60000)
        logger(ERROR,"Invalid port number (try 1->60000)",argv[1],0);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)
        logger(ERROR,"system call","bind",0);
    if( listen(listenfd,BACKLOG_PENNDING_CONNECTION) <0)
        logger(ERROR,"system call","listen",0);

    logger(LOG,"starting","listen",0);
    for(hit=1; ;hit++) {
        length = sizeof(cli_addr);
        if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
            logger(ERROR,"system call","accept",0);
        if((pid = fork()) < 0) {
            logger(ERROR,"system call","fork",0);
        }
        else {
            if(pid == 0) { 	/* child */
                close(listenfd);
                web(socketfd,hit); /* never returns */
            } else { 	/* parent */
                close(socketfd);
            }
        }
    }
}
