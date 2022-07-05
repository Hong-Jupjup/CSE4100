/* 
 * echoserveri.c - An iterative echo server 
 */ 
/* $begin echoserverimain */
#include "csapp.h"

typedef struct {
	int ID;
	int left_stock;
	int price;
	int readcnt;
} item;

struct node {
	struct node *leftChild;
	item it;	
	struct node *rightChild;
};

typedef struct pool {				/* Represents a pool of connected descriptors */
	int maxfd;						/* Largest descriptor in read_set */
	fd_set read_set;				/* Set of all active descriptors */
	fd_set ready_set;				/* Subset of descriptors ready for reading */
	int nready;						/* Number of ready descriptors fron select */
	int maxi;						/* High water index into client array */
	int clientfd[FD_SETSIZE];		/* Set of active descriptors */
	rio_t clientrio[FD_SETSIZE];	/* Set of active read buffers */
} pool;

typedef struct node* treePointer;
treePointer root = NULL;

void echo(int connfd);
void init_pool(int listenfd, pool *p);
void add_client(int connfd, pool *p);
void check_clients(pool *p);
treePointer modifiedSearch(treePointer root, treePointer node);
void insertNode(treePointer *root, treePointer node);
//void inorder(treePointer ptr);
treePointer binarySearch(treePointer temp, int key); // Search node that has ID as key
void updateNode(FILE *fp, treePointer root);

/* Stock functions */
char* show(int connfd, treePointer root, char *showbuf);
void buy(int connfd, treePointer root, int ID, int num, char *buybuf);
void sell(int connfd, treePointer root, int ID, int num, char *sellbuf);


int main(int argc, char **argv) 
{
	/* Error message */
	if(argc != 2) {
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

		insertNode(&root, node);
	}
	fclose(fp);

    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
    char client_hostname[MAXLINE], client_port[MAXLINE];
	static pool pool;

    listenfd = Open_listenfd(argv[1]);
	init_pool(listenfd, &pool);

    while (1) {
		/* Wait for listening/connected descriptor(s) to become ready */
		pool.ready_set = pool.read_set;
		pool.nready = Select(pool.maxfd + 1, &pool.ready_set, NULL, NULL, NULL);

		/* If listening descriptor ready, add new client to pool */
		if(FD_ISSET(listenfd, &pool.ready_set)) {
			clientlen = sizeof(struct sockaddr_storage); 
			connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
			Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
			printf("Connected to (%s, %s)\n", client_hostname, client_port);
			add_client(connfd, &pool);
		}

		/* Echo a text line from each ready connected descriptor */
		check_clients(&pool);

    }
    exit(0);
}
/* $end echoserverimain */

void init_pool(int listenfd, pool *p)
{
	/* Initially, there are no connected descriptors */
	int i;
	p->maxi = -1;
	for(i=0; i<FD_SETSIZE; i++) {
		p->clientfd[i] = -1;
	}

	/* Initially, listenfd is only member of select read set */
	p->maxfd = listenfd;
	FD_ZERO(&p->read_set);
	FD_SET(listenfd, &p->read_set);
}

void add_client(int connfd, pool *p)
{
	int i;
	p->nready--;
	for(i=0; i<FD_SETSIZE; i++) /* Find an available slot */
		if(p->clientfd[i] < 0) {
			/* Add connected descriptor to the pool */
			p->clientfd[i] = connfd;
			Rio_readinitb(&p->clientrio[i], connfd);

			/* Add the descriptor to descriptor set */
			FD_SET(connfd, &p->read_set);

			/* Update max descriptor and pool high water mark */
			if(connfd > p->maxfd)
				p->maxfd = connfd;
			if(i > p->maxi)
				p->maxi = i;
			break;
		}
	if(i == FD_SETSIZE) /* Couldn't find an empty slot */
		app_error("add_client error: Too many clients");
}

void check_clients(pool *p)
{
	int i, connfd, n;
	char buf[MAXLINE];
	rio_t rio;
	char cmd[MAXLINE]; // show, buy, sell, exit
	int ID, num; // When using buy or sell command

	char showbuf[MAXLINE] = {'\0'};
	char showres[MAXLINE];
	char buybuf[MAXLINE] = {'\0'};
	char sellbuf[MAXLINE] = {'\0'};

	for(i=0; (i <= p->maxi) && (p->nready > 0); i++) {
		connfd = p->clientfd[i];
		rio = p->clientrio[i];
		/* If the descriptor is ready, echo a text line from it */
		if((connfd > 0) && (FD_ISSET(connfd, &p->ready_set))) {
			Rio_readinitb(&rio, connfd);
			p->nready--;
			if(((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) && (strcmp(buf, "exit\n"))) {
				/* When the buf has a command except "exit" */

				/* Initialize to zero */
				cmd[0] = '\0'; ID = 0; num = 0;
				sscanf(buf, "%s%d%d", cmd, &ID, &num);
				printf("server received %d bytes\n", n);

				/* Command show */
				if(!strcmp(cmd, "show")) {
					strcpy(showres, "show\n");
					strcat(showres, show(connfd, root, showbuf));
					Rio_writen(connfd, showres, MAXLINE);
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

				/* The other commands */
				else { // Just print the buf on the screen
					Rio_writen(connfd, buf, MAXLINE);
				}
			}

			/* EOF detected or Command exit, remove descriptor from pool */
			else {
				Rio_writen(connfd, buf, MAXLINE);
				Close(connfd);
				FD_CLR(connfd, &p->read_set);
				p->clientfd[i] = -1;

				FILE *fp = fopen("stock.txt", "w");
				updateNode(fp, root);
				fclose(fp);
				exit(0);
			}
		}
	}
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
	treePointer ptr, temp = modifiedSearch(*root, node);
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

/*
void inorder(treePointer ptr)
{
	if(ptr) {
		inorder(ptr->leftChild);
		printf("%d ", ptr->it.ID);
		inorder(ptr->rightChild);
	}
}
*/

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

char* show(int connfd, treePointer root, char *showbuf)
{ // Similar to inorder
	char buf[MAXLINE];
	if(root != NULL) {
		show(connfd, root->leftChild, showbuf);
		sprintf(buf, "%d %d %d\n", root->it.ID, root->it.left_stock, root->it.price);
		strcat(showbuf, buf);
		show(connfd, root->rightChild, showbuf);
	}
	return showbuf;
}

void buy(int connfd, treePointer root, int ID, int num, char *buybuf)
{
	treePointer temp = binarySearch(root, ID);
	char buf[MAXLINE];

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
}

void sell(int connfd, treePointer root, int ID, int num, char *sellbuf)
{
	treePointer temp = binarySearch(root, ID);
	char buf[MAXLINE];

	temp->it.left_stock += num;
	sprintf(buf, "[sell] success\n");
	strcat(sellbuf, buf);
	Rio_writen(connfd, sellbuf, MAXLINE);
}

void updateNode(FILE *fp, treePointer root)
{
	if(root != NULL) {
		updateNode(fp, root->leftChild);
		fprintf(fp, "%d %d %d\n", root->it.ID, root->it.left_stock, root->it.price);
		updateNode(fp, root->rightChild);
	}
}
