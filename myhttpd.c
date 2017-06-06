/*
 * soc.c - program to open sockets to remote machines
 *
 * $Author: kensmith $
 * $Id: soc.c 6 2009-07-03 03:18:54Z kensmith $
 */

static char svnid[] = "$Id: soc.c 6 2009-07-03 03:18:54Z kensmith $";

#define	BUF_LEN	8192

typedef int bool;
#define true 1
#define false 0

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>
#include	<sys/types.h>
#include	<sys/socket.h>
#include	<netdb.h>
#include	<netinet/in.h>
#include	<inttypes.h>
#include 	<time.h>
#include 	<dirent.h>
#include 	<sys/types.h>
#include 	<sys/stat.h>
#include 	<unistd.h>
#include 	<errno.h>
#include	<pwd.h>



char *progname;
char buf[BUF_LEN];

struct header {
    uint32_t len;
    unsigned char type;
    char name[100];
};

struct Entry
{
	int ts;
	char fileName[100];
	char requestType[100];
	long fileSize;
	int sock;
} requests[1000] ;

void usage();
int setup_client();
int setup_server();
// struct header *getFileMeta(const char *path);
int getFileSize(const char *filename);
int comparator(const void *p, const void *q);
const char *getUserName();



int s, sock, ch, server, done, bytes, aflg;
int soctype = SOCK_STREAM;
int sleepTime;
int threadNum;
int logFile;
char *host = NULL;
char *log_file = NULL;
char *port = NULL;
char *directory = NULL;
char cwd[1024];
char *sched = NULL;
char buff[100];
bool isDebug;
pthread_mutex_t access_lock;
char *username;


extern char *optarg;
extern int optind;

int
main(int argc,char *argv[])
{
	username = getUserName();
	fd_set ready;

	// default initializations
	port = "8080";
	sched = "FCFS";
	sleepTime = 60;
	threadNum = 4;
	isDebug = false;


	// initalize lock
	pthread_mutex_init ( &access_lock, NULL);


	struct header *fil;
	struct dirent *ent;

	struct sockaddr_in msgfrom;
	int msgsize;
	union {
		uint32_t addr;
		char bytes[4];
	} fromaddr;

	if ((progname = rindex(argv[0], '/')) == NULL)
		progname = argv[0];
	else
		progname++;
	while ((ch = getopt(argc, argv, "dhl:p:r:t:n:s:")) != -1)
		switch(ch) {
			case 'd':
				isDebug = true;	/* print address in output */
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
		fprintf(stderr, "%d %d %d %d %d\n", argc, server, host, htons(atoi(port)), optind);
	argc -= optind;

	DIR* dir = opendir(directory);
	if (dir)
	{
	    /* Directory exists. */
	    chdir(dir);
	    if (getcwd(cwd, sizeof(cwd)) != NULL)
       		fprintf(stdout, "Current working dir: %s\n", cwd);
	    while ((ent = readdir (dir)) != NULL) {
	    	if(ent->d_name[0] == '.')
	    		continue;
	    	// fil = getFileMeta(ent->d_name);
			printf ("%s \t %d\n", ent->d_name, getFileSize(ent->d_name));
		}
		closedir (dir);
	}

	if(log_file)
	{
		logFile = open(log_file, "w+");
	}

	if (argc != 0)
		usage();
	if (!server && (host == NULL || port == NULL))
		usage();
	if (server && host != NULL)
		usage();
/*
 * Create socket on local host.
 */
	if ((s = socket(AF_INET, soctype, 0)) < 0) {
		perror("socket");
		exit(1);
	}
	if (!server)
		sock = setup_client();
	else
		sock = setup_server();
/*
 * Set up select(2) on both socket and terminal, anything that comes
 * in on socket goes to terminal, anything that gets typed on terminal
 * goes out socket...
 */
	while (!done) {
		FD_ZERO(&ready);
		FD_SET(sock, &ready);
		FD_SET(fileno(stdin), &ready);
		if (select((sock + 1), &ready, 0, 0, 0) < 0) {
			perror("select");
			exit(1);
		}
		if (FD_ISSET(fileno(stdin), &ready)) {
			if ((bytes = read(fileno(stdin), buf, BUF_LEN)) <= 0)
				done++;
			send(sock, buf, bytes, 0);
		}
		msgsize = sizeof(msgfrom);
		if (FD_ISSET(sock, &ready)) {
			if ((bytes = recvfrom(sock, buf, BUF_LEN, 0, (struct sockaddr *)&msgfrom, &msgsize)) <= 0) {
				done++;
			} else if (aflg) {
				fromaddr.addr = ntohl(msgfrom.sin_addr.s_addr);
				fprintf(stderr, "%d.%d.%d.%d: ", 0xff & (unsigned int)fromaddr.bytes[0],
			    	0xff & (unsigned int)fromaddr.bytes[1],
			    	0xff & (unsigned int)fromaddr.bytes[2],
			    	0xff & (unsigned int)fromaddr.bytes[3]);
			}
			time_t now = time (0);
			strftime (buff, 100, "%Y-%m-%d %H:%M:%S.000", localtime (&now));
			fprintf(stdout, "%s %s", buff, buf);
			// write(fileno(stdout), buf, bytes);
		}
	}
	return(0);
}

/*
 * setup_client() - set up socket for the mode of soc running as a
 *		client connecting to a port on a remote machine.
 */

int
setup_client() {

	struct hostent *hp, *gethostbyname();
	struct sockaddr_in serv;
	struct servent *se;

/*
 * Look up name of remote machine, getting its address.
 */
	if ((hp = gethostbyname(host)) == NULL) {
		fprintf(stderr, "%s: %s unknown host\n", progname, host);
		exit(1);
	}
/*
 * Set up the information needed for the socket to be bound to a socket on
 * a remote host.  Needs address family to use, the address of the remote
 * host (obtained above), and the port on the remote host to connect to.
 */
	serv.sin_family = AF_INET;
	memcpy(&serv.sin_addr, hp->h_addr, hp->h_length);
	if (isdigit(*port))
		serv.sin_port = htons(atoi(port));
	else {
		if ((se = getservbyname(port, (char *)NULL)) < (struct servent *) 0) {
			perror(port);
			exit(1);
		}
		serv.sin_port = se->s_port;
	}
/*
 * Try to connect the sockets...
 */
	if (connect(s, (struct sockaddr *) &serv, sizeof(serv)) < 0) {
		perror("connect");
		exit(1);
	} else
		fprintf(stderr, "Connected...\n");
	return(s);
}

/*
 * setup_server() - set up socket for mode of soc running as a server.
 */

int
setup_server() {
	struct sockaddr_in serv, remote;
	struct servent *se;
	int newsock, len;

	len = sizeof(remote);
	memset((void *)&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	if (port == NULL)
		serv.sin_port = htons(0);
	else if (isdigit(*port))
		serv.sin_port = htons(atoi(port));
	else {
		if ((se = getservbyname(port, (char *)NULL)) < (struct servent *) 0) {
			perror(port);
			exit(1);
		}
		serv.sin_port = se->s_port;
	}
	if (bind(s, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
		perror("bind");
		exit(1);
	}
	if (getsockname(s, (struct sockaddr *) &remote, &len) < 0) {
		perror("getsockname");
		exit(1);
	}
	fprintf(stderr, "Port number is %d\n", ntohs(remote.sin_port));
	listen(s, 1);
	newsock = s;
	if (soctype == SOCK_STREAM) {
		fprintf(stderr, "Entering accept() waiting for connection.\n");
		newsock = accept(s, (struct sockaddr *) &remote, &len);
		fprintf(stderr, "Connection recieved from %d.%d.%d.%d\n",
			  remote.sin_addr.s_addr&0xFF,
			  (remote.sin_addr.s_addr&0xFF00)>>8,
			  (remote.sin_addr.s_addr&0xFF0000)>>16,
			  (remote.sin_addr.s_addr&0xFF000000)>>24);
	}
	return(newsock);
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
        if (status != 0) {
            printf ("Error, errno = %s ", strerror(errno));
            return 1;
        }

        // Tell us what it is then exit.

        if (S_ISREG (st_buf.st_mode)) {
            // printf ("%s is a regular file. ", fullpath);
            FILE *p_file = NULL;
            p_file = fopen(fullpath, "r");
            fseek(p_file,0,SEEK_END);
            size = ftell(p_file);
            fclose(p_file);
        }
        if (S_ISDIR (st_buf.st_mode)) {
            // printf ("%s is a directory. ", fullpath);
        }
	}
	return size;
}

int 
comparator(const void *p, const void *q) 
{
    if(strcmp(sched, "SJF") == 0){
    	int pSize = ((struct Entry *)p)->fileSize;
    	int qSize = ((struct Entry *)q)->fileSize;
    	return (pSize - qSize);
    }
    else{
    	int pTime = ((struct Entry *)p)->ts;
    	int qTime = ((struct Entry *)q)->ts;
    	return (pTime - qTime);
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