/* 
 * echoserveri.c - An iterative echo server 
 */ 
/* $begin echoserverimain */
#include "csapp.h"

#define NTHREADS 100
#define SBUFSIZE 100

typedef struct {
	int ID;
	int left_stock;
	int price;
	int readcnt;
	sem_t mutex, w;
} item;

struct node {
	struct node *leftChild;
	item it;
	struct node *rightChild;
};

typedef struct {
	int *buf;			/* Buffer array */
	int n;				/* Maximum number of slots */
	int front;			/* buf[(fonrt+1)%n] is first item */
	int rear;			/* buf[rear%n] is last item */
	sem_t mutex;		/* Protects accesses to buf */
	sem_t slots;		/* Counts available slots */
	sem_t items;		/* Counts available items */
} sbuf_t;

typedef struct node *treePointer;
treePointer root = NULL;
sbuf_t sbuf; /* Shared buffer of connected descriptors */
static sem_t mutex;

void echo(int connfd);

/* Tree functions */
treePointer modifiedSearch(treePointer root, treePointer node);
void insertNode(treePointer *root, treePointer node);
treePointer binarySearch(treePointer temp, int key); // Search node that has ID as key
void updateNode(FILE *fp, treePointer root);

/* Stock fuctions */
static void init_echo_cnt();
void echo_cnt(int connfd);
void show(int connfd, treePointer root);
void buy(int connfd, treePointer root, int ID, int num, char *buybuf);
void sell(int connfd, treePointer root, int ID, int num, char *sellbuf);

/* Thread functions */
void* thread(void *vargp);
void sbuf_init(sbuf_t *fp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);

char showbuf[MAXLINE];

int main(int argc, char **argv) 
{
	/* Error message */
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
	}

	/* Open txt file, store data in a node and insert it in the root */
	FILE *fp = fopen("stock.txt", "r");
	int ID, left_stock, price;

	while(!feof(fp)) {
		int scan = fscanf(fp, "%d%d%d", &ID, &left_stock, &price);
		scan++;

		treePointer node = (treePointer)malloc(sizeof(struct node));
		node->it.ID = ID;
		node->it.left_stock = left_stock;
		node->it.price = price;
		node->it.readcnt = 0;
		node->leftChild = NULL;
		node->rightChild = NULL;
		Sem_init(&node->it.mutex, 0, 1);
		Sem_init(&node->it.w, 0, 1);

		insertNode(&root, node);
	}
	fclose(fp);

	int i, listenfd, connfd;
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
	char client_hostname[MAXLINE], client_port[MAXLINE];
	pthread_t tid;

	Sem_init(&mutex, 0, 1);
	listenfd = Open_listenfd(argv[1]);

	sbuf_init(&sbuf, SBUFSIZE);
	for(i=0; i < NTHREADS; i++) /* Create worker threads */
		Pthread_create(&tid, NULL, thread, NULL);
	while (1) {
		clientlen = sizeof(struct sockaddr_storage); 
		connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
		printf("Connected to (%s, %s)\n", client_hostname, client_port);
		sbuf_insert(&sbuf, connfd); /* Insert connfd in buffer */
	}
	exit(0);
}
/* $end echoserverimain */

void* thread(void *vargp)
{
	Pthread_detach(pthread_self());

	while(1) {
		int connfd = sbuf_remove(&sbuf);	/* Remove connfd from buf */
		echo_cnt(connfd);					/* Service client */
		Close(connfd);
	}
}

void sbuf_init(sbuf_t *sp, int n)
{ // Create an empty, bounded, shared FIFO buffer with n slots
	sp->buf = Calloc(n, sizeof(int));
	sp->n = n;						/* Buffer holds max of n items */
	sp->front = sp->rear = 0;		/* Empty buffer iff front == rear */
	Sem_init(&sp->mutex, 0, 1);		/* Binary semaphore for locking */
	Sem_init(&sp->slots, 0, n);		/* Initially, buf has n empty slots */
	Sem_init(&sp->items, 0, 0);		/* Initially, buf has 0 items */
}

void sbuf_deinit(sbuf_t *sp)
{ // Clean up buffer sp
	Free(sp->buf);
}

void sbuf_insert(sbuf_t *sp, int item)
{ // Insert item onto the rear of shared buffer sp
	P(&sp->slots);								/* Wait for available slot */
	P(&sp->mutex);								/* Lock the buffer */
	sp->buf[(++sp->rear)%(sp->n)] = item;		/* Insert the item */
	V(&sp->mutex);								/* Unlock the buffer */
	V(&sp->items);								/* Announce available item */
}

int sbuf_remove(sbuf_t *sp)
{
	int item;
	P(&sp->items);								/* Wait for available item */
	P(&sp->mutex);								/* Lock the buffer */
	item = sp->buf[(++sp->front)%(sp->n)];		/* Remove the item */
	V(&sp->mutex);								/* Unlock the buffer */
	V(&sp->slots);								/* Announce available slot */
	return item;
}

treePointer modifiedSearch(treePointer root, treePointer node)
{
	treePointer parent = NULL;
	while(root) {
		if(node->it.ID == root->it.ID)
			return NULL;
		parent = root;
		if(node->it.ID < root->it.ID)
			root = root->leftChild;
		else
			root = root->rightChild;
	}
	return parent;
}

void insertNode(treePointer *root, treePointer node)
{
	treePointer ptr,temp = modifiedSearch(*root, node);
	if(temp || !(*root)) {
		ptr = (treePointer)malloc(sizeof(*ptr));
		ptr = node;
		if(*root) {
			if(node->it.ID < temp->it.ID)
				temp->leftChild = ptr;
			else
				temp->rightChild = ptr;
		}
		else
			*root = ptr;
	}
}

treePointer binarySearch(treePointer temp, int key)
{
	while(temp != NULL && temp->it.ID != key) {
		if(temp->it.ID > key)
			temp = temp->leftChild;
		else if(temp->it.ID < key)
			temp = temp->rightChild;
	}
	return temp;
}

static void init_echo_cnt()
{
	Sem_init(&mutex, 0, 1);
}

void echo_cnt(int connfd)
{
	int n;
	char buf[MAXLINE];
	rio_t rio;
	char cmd[MAXLINE]; // show, buy, sell, exit
	int ID, num; // When using buy or sell command
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	Pthread_once(&once, init_echo_cnt);
	Rio_readinitb(&rio, connfd);

	while(((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0)) {
		/* When the buf has a command except "exit" */
		char buybuf[MAXLINE] = {'\0'};
		char sellbuf[MAXLINE] = {'\0'};

		/* Initialize to zero */
		cmd[0] = '\0'; ID = 0; num = 0;
		sscanf(buf, "%s%d%d", cmd, &ID, &num);

		printf("server received %d bytes\n", n);

		/* Command show */
		if(!strcmp(cmd, "show")) {
			strcpy(showbuf, "show\n");
			show(connfd, root);
			Rio_writen(connfd, showbuf, MAXLINE);
		}

		/* Command buy */
		else if(!strcmp(cmd, "buy")) {
			strcpy(buybuf, buf);
			buy(connfd, root, ID, num, buybuf);
		}

		/* Command sell */
		else if(!strcmp(cmd, "sell")) {
			strcpy(sellbuf, buf);
			sell(connfd, root, ID, num, sellbuf);
		}

		/* Command exit */
		else if(!strcmp(cmd, "exit")) {
			Rio_writen(connfd, buf, MAXLINE);
			break;
		}

		/* The other commands */
		else { // Just print the buf on the screen
			Rio_writen(connfd, buf, MAXLINE);
		}
	}

	FILE *fp = fopen("stock.txt", "w");
	updateNode(fp, root);
	fclose(fp);
//	exit(0);

}

void show(int connfd, treePointer root)
{ // Similar to inorder
	char buf[MAXLINE];
	if(root != NULL) {
		show(connfd, root->leftChild);

		P(&root->it.mutex);
		root->it.readcnt++;
		if(root->it.readcnt == 1) /* First in */
			P(&root->it.w);
		V(&root->it.mutex);

		sprintf(buf, "%d %d %d\n", root->it.ID, root->it.left_stock, root->it.price);
		strcat(showbuf, buf);

		P(&root->it.mutex);
		root->it.readcnt--;
		if(root->it.readcnt == 0) /* Last out */
			V(&root->it.w);
		V(&root->it.mutex);

		show(connfd, root->rightChild);
	}
}

void buy(int connfd, treePointer root, int ID, int num, char *buybuf)
{
	treePointer temp = binarySearch(root, ID);
	char buf[MAXLINE];

	P(&temp->it.w);
	if(temp->it.left_stock < num) {
		sprintf(buf, "Not enough left stocks\n");
		strcat(buybuf, buf);
		Rio_writen(connfd, buybuf, MAXLINE);
	}
	else {
		temp->it.left_stock -= num;
		sprintf(buf, "[buy] success\n");
		strcat(buybuf, buf);
		Rio_writen(connfd, buybuf, MAXLINE);
	}
	V(&temp->it.w);
}

void sell(int connfd, treePointer root, int ID, int num, char *sellbuf)
{
	treePointer temp = binarySearch(root, ID);
	char buf[MAXLINE];

	P(&temp->it.w);
	temp->it.left_stock += num;
	sprintf(buf, "[sell] success\n");
	strcat(sellbuf, buf);
	Rio_writen(connfd, sellbuf, MAXLINE);
	V(&temp->it.w);
}

void updateNode(FILE *fp, treePointer root)
{
	if(root != NULL) {
		updateNode(fp, root->leftChild);
		fprintf(fp, "%d %d %d\n", root->it.ID, root->it.left_stock, root->it.price);
		updateNode(fp, root->rightChild);
	}
}
