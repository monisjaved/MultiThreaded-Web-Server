#define BUF_LEN 8192

#define SERVER "MYHTTPD"

// creating boolean for easier usage
typedef int bool;
#define true 1
#define false 0

#include    <stdio.h>
#include    <stdlib.h>
#include    <string.h>
#include    <ctype.h>
#include    <sys/types.h>
#include    <sys/socket.h>
#include    <netdb.h>
#include    <netinet/in.h>
#include    <inttypes.h>
#include    <time.h>
#include    <dirent.h>
#include    <sys/types.h>
#include    <sys/stat.h>
#include    <unistd.h>
#include    <errno.h>
#include    <pwd.h>
#include    <pthread.h>



char *progname;
char buf[BUF_LEN];

struct Entry
{
    unsigned long long ts;
    char *timeString;
    char *remoteAddress;
    char *fileName;
    char *requestType;
    long fileSize;
    int sock;
    int status;
    char *quotedFirstLine;

} requests[1000], available;

struct arg_struct 
{
    int sock;
    char *remoteAddress;
};

void usage();
int setup_client();
int setup_server();
int getFileSize(const char *);
const char *getFileLastModifiedTime(const char *);
int comparator(const void *, const void *);
const char *getUserName();
void *listener(void *);
void *queuer(void *);
void *executor(void *);
const char *getTimeString(time_t);
unsigned long long getTimeStamp();
const char ** getSplitString(const char *);
int sendFile(int, const char *, const char *);
bool sendData(int, const char *);




int s, sock, ch, server, done, bytes, aflg;
int soctype = SOCK_STREAM;
int sleepTime;
int threadNum;
FILE* logFile;
char *host = NULL;
char *log_file = NULL;
char *port = NULL;
char *directory = NULL;
char cwd[1024];
char *sched = NULL;
char buff[100];
bool isDebug;
int requestsCount = 0;
pthread_mutex_t requests_access_lock;
pthread_mutex_t available_access_lock;
pthread_mutex_t log_lock;
char *username;


extern char *optarg;
extern int optind;

int
main(int argc,char *argv[])
{
    username = getUserName();

    // default initializations
    port = "8080";
    sched = "FCFS";
    sleepTime = 60;
    threadNum = 4;
    isDebug = false;
    directory = ".";


    // initalize locks
    pthread_mutex_init ( &requests_access_lock, NULL);
    pthread_mutex_init ( &available_access_lock, NULL);
    pthread_mutex_init ( &log_lock, NULL);


    struct dirent *ent;

    struct sockaddr_in serv, remote;
    struct servent *se;

    int newsock, i;
    socklen_t len;

    pthread_t listener_thread, queuer_thread, executor_thread[threadNum];
    available.status = -1;


    if ((progname = rindex(argv[0], '/')) == NULL)
        progname = argv[0];
    else
        progname++;
    while ((ch = getopt(argc, argv, "dhl:p:r:t:n:s:")) != -1)
        switch(ch) 
        {
            case 'd':
                isDebug = true; /* print address in output */
                threadNum = 1;
                break;
            case 'h':
                usage();
                exit(0);
                break;
            case 'l':
                log_file = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            case 'r':
                directory = optarg;
                if(strcmp(directory, "~") == 0){
                    sprintf(directory, "/home/%s/myhttpd", username);
                }
                break;
            case 't':
                sleepTime = atoi(optarg);
                break;
            case 'n':
                threadNum = atoi(optarg);
                break;
            case 's':
                sched = optarg;
                break;
            case '?':
            default:
                usage();
        }

    if(isdigit(*port))
        // fprintf(stderr, "%d %d %d %d %d\n", argc, server, host, htons(atoi(port)), optind);
    argc -= optind;

    // DIR* dir = opendir(directory);
    // if (dir)
    // {
    //     /* Directory exists. */
    //     chdir(dir);
    //     if (getcwd(cwd, sizeof(cwd)) != NULL)
    //         // fprintf(stdout, "Current working dir: %s\n", cwd);
    //     while ((ent = readdir (dir)) != NULL) 
    //     {
    //         if(ent->d_name[0] == '.')
    //             continue;
    //         // fil = getFileMeta(ent->d_name);
    //         // printf ("%s \t %d %s\n", ent->d_name, getFileSize(ent->d_name), getFileLastModifiedTime(ent->d_name));
    //     }
    //     closedir (dir);
    // }

    if(log_file)
    {
        pthread_mutex_lock(&log_lock);
        logFile = fopen(log_file, "wb");
        pthread_mutex_unlock(&log_lock);
    }

    if(pthread_create( &queuer_thread , NULL ,  queuer , NULL) < 0)
    {
        perror("could not create queuer thread");
        exit(1);
    }

    // fprintf(stderr, "reached\n");

    for(i = 0; i< threadNum; i++){
        if(pthread_create( &executor_thread[i] , NULL ,  executor , NULL) < 0)
        {
            perror("could not create executor thread");
            exit(1);
        }
    }

/*
 * Create socket on local host.
 */
    if ((s = socket(AF_INET, soctype, 0)) < 0) 
    {
        perror("socket");
        exit(1);
    }
    len = sizeof(remote);
    memset((void *)&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    if (port == NULL)
        serv.sin_port = htons(0);
    else if (isdigit(*port))
        serv.sin_port = htons(atoi(port));
    else 
    {
        if ((se = getservbyname(port, (char *)NULL)) < (struct servent *) 0) 
        {
            perror(port);
            exit(1);
        }
        serv.sin_port = se->s_port;
    }
    if (bind(s, (struct sockaddr *)&serv, sizeof(serv)) < 0) 
    {
        perror("bind");
        exit(1);
    }
    if (getsockname(s, (struct sockaddr *) &remote, &len) < 0) 
    {
        perror("getsockname");
        exit(1);
    }

    listen(s , 3);

    fprintf(stderr, "Waiting for connection.\n");

    while ((newsock = accept(s, (struct sockaddr *) &remote, &len)))
    {
        struct arg_struct arguments;

        arguments.sock = newsock;
        sprintf(arguments.remoteAddress, "%d.%d.%d.%d", remote.sin_addr.s_addr&0xFF,
              (remote.sin_addr.s_addr&0xFF00)>>8,
              (remote.sin_addr.s_addr&0xFF0000)>>16,
              (remote.sin_addr.s_addr&0xFF000000)>>24);

        if(pthread_create( &listener_thread , NULL ,  listener , (void*) &arguments) < 0)
        {
            perror("could not create listener thread");
            exit(1);
        }
    }
    if (newsock < 0)
    {
        perror("accept failed");
        exit(1);
    }
     
    return 0;
}

/*
 * setup_client() - set up socket for the mode of soc running as a
 *      client connecting to a port on a remote machine.
 */
void 
*listener(void *arguments)
{
    struct arg_struct *args = (struct arg_struct *)arguments;
    int sock = args->sock;
    char *remoteAddress = args->remoteAddress;
    int readLen;

    char clientMessage[BUF_LEN];
     
    while( (readLen = recv(sock , clientMessage , BUF_LEN , 0)) > 0 )
    {
        //end of string marker

        clientMessage[readLen] = '\0';
        char temp[BUF_LEN];

        strcpy(temp, clientMessage);
        temp[strlen(temp)-1] = '\0';

        time_t now = time (0);
        unsigned long long ts = getTimeStamp();

        // char timeString[BUF_LEN];

        // strcpy(timeString, getTimeString(now));

        // fprintf(stdout, "%s %s %s", remoteAddress, timeString, clientMessage);

        const char **strArray = getSplitString(clientMessage);
        int status, fileSize;
        char *requestType, *fileName;

        int arrLen = -1;
        while (strArray[++arrLen] != NULL) { }

        if (arrLen != 3){
            status = 501;
            requestType = malloc(BUF_LEN);
            strcpy(requestType, "GET");
            fileSize = -1;  
        }       

        else
        {
            requestType = strArray[0];
            fileName = strArray[1];
            if(fileName[0] == '~')
            {
                char temp[BUF_LEN];
                sprintf(temp, "/home/%s/myhttpd/%s" , getUserName(), strcpy(fileName, fileName+1));
                // fileName = temp;
                fileName = temp;
                // fprintf(stderr, "%s\n", fileName);
            }

            fileSize=0;

            if(getFileSize(fileName) == -1)
            {
                status = 404;
            }
            else
            {
                fileSize = getFileSize(fileName);
                // fprintf(stderr, "%d\n", fileSize);
                if(fileSize == -2)
                {
                    char temp[BUF_LEN];
                    sprintf(temp, "%s/index.html", fileName);
                    fileName = temp;
                    if(getFileSize(fileName) == -1)
                    {
                        char temp[BUF_LEN];
                        strncpy(temp, fileName, strlen(fileName)-11);
                        fileName = temp;
                        fileSize = 0;
                        status = 403;
                    }
                    else
                    {
                        fileSize = getFileSize(fileName);
                        status = 200;
                    }
                }
                else
                {
                    status = 200;
                }
            }

            // fprintf(stderr, "reached\n");

            if(strcmp(requestType,"HEAD") == 0){
                fileSize = 0;
            } 
        }

        
        
        pthread_mutex_lock(&requests_access_lock);
        requests[requestsCount].ts = ts;
        requests[requestsCount].timeString = malloc(BUF_LEN);
        strcpy(requests[requestsCount].timeString,getTimeString(now));
        requests[requestsCount].remoteAddress = malloc(BUF_LEN);
        strcpy(requests[requestsCount].remoteAddress,remoteAddress);
        requests[requestsCount].status = status;
        requests[requestsCount].sock = sock;
        requests[requestsCount].fileName = malloc(BUF_LEN);
        strcpy(requests[requestsCount].fileName,fileName);
        requests[requestsCount].fileSize = fileSize;
        requests[requestsCount].requestType = malloc(BUF_LEN);
        strcpy(requests[requestsCount].requestType,requestType);
        requests[requestsCount].quotedFirstLine = malloc(BUF_LEN);
        strcpy(requests[requestsCount].quotedFirstLine, temp);
        // fprintf(stderr, "ts = %llu \ntimeString = %s \nremoteAddress = %s \nstatus = %d \nsock = %d \nfileName = %s \nfileSize = %d \nrequestType = %s \n", 
        //     requests[requestsCount].ts,
        //     requests[requestsCount].timeString,
        //     requests[requestsCount].remoteAddress,
        //     requests[requestsCount].status,
        //     requests[requestsCount].sock,
        //     requests[requestsCount].fileName,
        //     requests[requestsCount].fileSize,
        //     requests[requestsCount].requestType);
        requestsCount++;
        pthread_mutex_unlock(&requests_access_lock);

        
        // if(logFile != NULL){
        //     pthread_mutex_lock(&log_lock);
        //     // fprintf(stderr, "%s %d\n", log_file, logFile);
        //     fprintf(logFile, "%s -  %s %s", remoteAddress, buff, clientMessage);
        //     fflush(logFile);
        //     pthread_mutex_unlock(&log_lock);
        // }
        
        //clear the message buffer
        memset(clientMessage, 0, BUF_LEN);
    }
     
    if(readLen == 0)
    {
        // puts("Client disconnected");
        // fflush(stdout);
    }
    else if(readLen == -1)
    {
        perror("recv failed");
    }
         
    return 0;
}

void
*queuer(void *arguments) 
{
    // fprintf(stderr, "reached queuer\n");
    bool sleeping = false;
    while(true)
    {
        pthread_mutex_lock(&requests_access_lock);
        pthread_mutex_lock(&available_access_lock);
        if(available.status == -1 && requestsCount > 0)
        {
            qsort((void*)requests, requestsCount, sizeof(requests[0]), comparator);
            available = requests[requestsCount-1];
            time_t now = time(0);
            available.timeString = malloc(BUF_LEN);
            strcpy(available.timeString,getTimeString(now));
            requestsCount--;
            // fprintf(stderr, "%d\n", requestsCount);
        }
        else{
            sleeping = true;
        }
        pthread_mutex_unlock(&available_access_lock);
        pthread_mutex_unlock(&requests_access_lock);
        if(sleeping)
        {
            usleep(300 * 1000);
            // for(int i=0;i<requestsCount;i++){
            //     fprintf(stderr, "%d[%s|%s] - ", requests[i].fileSize, requests[i].fileName, requests[i].requestType);
            // }
            // fprintf(stderr, "\n");
        }
    }
    return 0;
}

void
*executor(void *arguments)
{
    // int *sleepTime = (int *)arguments;
    // fprintf(stderr, "sleeping for %d\n", sleepTime);
    sleep(sleepTime);
    // fprintf(stderr, "awake\n");
    while(true)
    {
        pthread_mutex_lock(&available_access_lock);
        struct Entry elem = available;
        available.status = -1;
        pthread_mutex_unlock(&available_access_lock);
        if(elem.status == -1)
        {
            continue;
        }

        int status = elem.status;

        const char *lastModifiedTime = NULL;

        // if(status != 501 && status != 404)
        //     lastModifiedTime = getFileLastModifiedTime(elem.fileName);
        
        time_t now = time(0);
        char execRecievedTime[BUF_LEN];
        strcpy(execRecievedTime, getTimeString(now));

        char fileType[BUF_LEN];


        if(status == 501)
        {
            strcpy(fileType, "INVALID REQUEST");
        } 
        else if(status == 403)
        {
            strcpy(fileType, "INDEX NOT FOUND IN DIRECTORY");
        }
        else if(status == 404)
        {
            strcpy(fileType, "FILE NOT FOUND");
        }
        else if(strstr(elem.fileName, ".jpg") != NULL || strstr(elem.fileName, ".jpeg") != NULL || strstr(elem.fileName, ".png") != NULL)
        {
            strcpy(fileType, "image/jpg");
        }
        else if(strstr(elem.fileName, ".gif") != NULL)
        {
            strcpy(fileType, "image/gif");
        }
        else
        {
            strcpy(fileType, "text/html");
        }

        char retString[BUF_LEN];

        // if(status != 501 && status != 404)
        //     lastModifiedTime = getFileLastModifiedTime(elem.fileName);

        sprintf(retString, "Date: %s \nServer: %s \nLast-Modified: %s \nContent-Type: %s \nContent-Length: %ld\n\n", execRecievedTime, SERVER, getFileLastModifiedTime(elem.fileName), fileType, elem.fileSize);
        send(elem.sock, retString, strlen(retString), 0);

        if(status == 200 && strcmp(elem.requestType, "GET") == 0)
        {
            sendFile(elem.sock, elem.fileName, fileType);
        }
        else if(status == 403)
        {
            char *ret = "";
            DIR* dir = opendir(elem.fileName);
            struct dirent *ent;
            if (dir)
            {
                /* Directory exists. */
                chdir(dir);
                if (getcwd(cwd, sizeof(cwd)) != NULL)
                    // fprintf(stdout, "Current working dir: %s\n", cwd);
                    while ((ent = readdir (dir)) != NULL) 
                    {
                        char temp[strlen(ret) + 2 + strlen(ent->d_name)];
                        if(ent->d_name[0] == '.')
                            continue;
                        sprintf(temp, "%s\n%s ", ret, ent->d_name);
                        ret = malloc(strlen(temp));
                        strcpy(ret, temp);
                        // free(temp);
                        // fil = getFileMeta(ent->d_name);
                        // printf ("%s \t %d %s\n", ent->d_name, getFileSize(ent->d_name), getFileLastModifiedTime(ent->d_name));
                    }
                closedir (dir);
                sendData(elem.sock , ret);
                ret[strlen(ret)-1] = '\0';
                // fprintf(stderr, "%s\n", ret);
            }
            else
            {
                perror("error");
            }
        }

        // fprintf(stderr, "------executor------\nts = %llu \ntimeString = %s \nremoteAddress = %s \nstatus = %d \nsock = %d \nfileName = %s \nfileSize = %d \nrequestType = %s \n-------------------\n", 
        //     elem.ts,
        //     elem.timeString,
        //     elem.remoteAddress,
        //     elem.status,
        //     elem.sock,
        //     elem.fileName,
        //     elem.fileSize,
        //     elem.requestType);

        char logMessage[BUF_LEN];
        sprintf(logMessage, "%s - [%s] [%s] \"%s\" %d %ld", elem.remoteAddress, elem.timeString, execRecievedTime, elem.quotedFirstLine, elem.status, elem.fileSize);
        if(isDebug)
        {
            pthread_mutex_lock(&log_lock);
            fprintf(stderr, "%s\n", logMessage);
            pthread_mutex_unlock(&log_lock);
        }
        if(log_file != NULL)
        {
            pthread_mutex_lock(&log_lock);
            fprintf(logFile, "%s\n", logMessage);
            fflush(logFile);
            pthread_mutex_unlock(&log_lock);
        }
    }
    
    return 0;
}

/*
 * usage - print usage string and exit
 */

void
usage()
{
    fprintf(stderr, "usage: %s [-d] [-h] [-l file] [-p port] [-r dir] [-t time] [-n threadnum] [-s sched]\n", progname);
    exit(1);
}


int 
getFileSize(const char *filename) // path to file
{
    DIR* dir = opendir(directory);
    int size = -1;
    struct stat st_buf;
    int status;
    char *fullpath = malloc(strlen(directory) + strlen(filename) + 2);

    if (dir)
    {
        chdir(dir);
        sprintf(fullpath, "%s/%s", directory, filename);
        // fprintf(stderr, "%s\n", fullpath);
        status = stat (fullpath, &st_buf);
        if (status != 0) 
        {
            printf ("Error, errno = %s ", strerror(errno));
            return -1;
        }

        if (S_ISREG (st_buf.st_mode)) 
        {
            // FILE *p_file = NULL;
            // p_file = fopen(fullpath, "r");
            // fseek(p_file,0,SEEK_END);
            // size = ftell(p_file);
            // fclose(p_file);
            return st_buf.st_size;
        }
        if (S_ISDIR (st_buf.st_mode)) 
        {
            return -2;
        }
    }
    return size;
}

const char 
*getFileLastModifiedTime(const char *filename) // path to file
{
    DIR* dir = opendir(directory);
    int size = -1;
    struct stat st_buf;
    int status;
    char *fullpath = malloc(strlen(directory) + strlen(filename) + 2);

    if (dir)
    {
        chdir(dir);
        sprintf(fullpath, "%s/%s", directory, filename);
        // fprintf(stderr, "%s\n", fullpath);
        status = stat (fullpath, &st_buf);
        if(status != 0)
        {
            return NULL;
        }

        return getTimeString(st_buf.st_mtime);
    }
    return "not present";
    // return size;
}

int 
comparator(const void *p, const void *q) 
{
    if(strcmp(sched, "SJF") == 0){
        int pSize = ((struct Entry *)p)->fileSize;
        int qSize = ((struct Entry *)q)->fileSize;
        return (qSize - pSize);
    }
    else{
        int pTime = ((struct Entry *)p)->ts;
        int qTime = ((struct Entry *)q)->ts;
        return (qTime - pTime);
    }
}

const char *
getUserName()
{
  uid_t uid = geteuid();
  struct passwd *pw = getpwuid(uid);
  if (pw)
  {
    return pw->pw_name;
  }

  return "";
}

const char 
*getTimeString(time_t ts){
    static char timeString[100];
    strftime (timeString, 100, "%d/%b/%Y:%H:%M:%S", gmtime (&ts));
    return timeString;
}

unsigned long long
getTimeStamp(){
    struct timeval tv;

    gettimeofday(&tv, NULL);

    unsigned long long millisecondsSinceEpoch =
        (unsigned long long)(tv.tv_sec) * 1000 +
        (unsigned long long)(tv.tv_usec) / 1000;

    return millisecondsSinceEpoch;
}

const char
**getSplitString(const char *mainString){
    char ** res  = NULL;
    char *  p    = strtok (mainString, " ");
    int n_spaces = 0;


    /* split string and append tokens to 'res' */

    while (p) 
    {
      res = realloc (res, sizeof (char*) * ++n_spaces);

      if (res == NULL)
        exit (-1); /* memory allocation failed */

      res[n_spaces-1] = p;

      p = strtok (NULL, " ");
    }

    /* realloc one extra element for the last NULL */

    res = realloc (res, sizeof (char*) * (n_spaces+1));
    res[n_spaces] = NULL;
    res[n_spaces-1][strlen(res[n_spaces-1])-1] = '\0';

    /* print the result */

    // for (i = 0; i < (n_spaces); ++i)
    //   printf ("res[%d] = %s\n", i, res[i]);

    /* free the memory allocated */

    free (res);

    return res;
}

int
sendFile(int sock, const char *fileName, const char *fileType)
{
    DIR* dir = opendir(directory);
    int size = -1;
    struct stat st_buf;
    int status;
    char *fullpath = malloc(strlen(directory) + strlen(fileName) + 2);
    FILE *fs;

    if (dir)
    {
        chdir(dir);
        sprintf(fullpath, "%s/%s", directory, fileName);
        // fprintf(stderr, "%s\n", fullpath);
        status = stat (fullpath, &st_buf);
        if(status != 0)
        {
            return -1;
        }

        char buffer[BUF_LEN];

        if(strcmp(fileType, "text/html") == 0)
        {
            fs = fopen(fullpath, "r");            
        }
        else
        {
            fs = fopen(fullpath, "rb");
        }
        if(fs==NULL)
        {
            perror("\n[Server] File is not found on Server Directory");
            return -1;
        }

        bzero(buffer, BUF_LEN); 
        int bytes_written;
        int total_bytes_written; 

        while((bytes_written = fread(buffer, sizeof(char), BUF_LEN, fs))>0)
        {
            if(send(sock, buffer, bytes_written, 0) < 0)
            {
                puts("\n[Server] Error: Sending File Failed");
                return -1;
            }
            total_bytes_written += bytes_written;
            bzero(buffer, BUF_LEN);
        }
        return total_bytes_written;
    }
    return -1;
}

bool sendData(int sock, const char *data)
{
    int dataLength = strlen(data);
    int leftPacketLength = 0;
    int offset = 0;
    int sentPacketLength = 0;

    for(int leftDataLength=dataLength; leftDataLength>0; leftDataLength -= BUF_LEN) {

            leftPacketLength = (leftDataLength > BUF_LEN) ? BUF_LEN : leftDataLength;
            while(leftPacketLength > 0) {
                sentPacketLength = send(sock, data + offset, leftPacketLength, 0);
                if(sentPacketLength < 0) {
                    fprintf(stderr, "%s: Error while sending data to the socket: %d\n", __func__, sentPacketLength);
                    perror(errno);
                    return false;
                }
                offset += sentPacketLength;
                leftPacketLength -= sentPacketLength;
            }
        }

        if(offset != dataLength)
            return false;

        return true;
}

