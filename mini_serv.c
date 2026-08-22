#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct s_c{int id,fd;char*b;struct s_c*n;}t_c;
t_c*C=0;int M=0,I=0;fd_set R,W;
void fe(){write(2,"Fatal error\n",12);exit(1);}
void br(int s,char*m){for(t_c*x=C;x;x=x->n)if(x->id!=s&&FD_ISSET(x->fd,&W))send(x->fd,m,strlen(m),0);}
void ad(int f){t_c*x=calloc(1,sizeof(t_c));if(!x)fe();x->id=I++;x->fd=f;x->n=C;C=x;FD_SET(f,&R);FD_SET(f,&W);if(f>M)M=f;char b[50];sprintf(b,"server: client %d just arrived\n",x->id);br(-1,b);}
void rm(t_c*p,t_c*x){char b[50];sprintf(b,"server: client %d just left\n",x->id);br(-1,b);FD_CLR(x->fd,&R);FD_CLR(x->fd,&W);close(x->fd);free(x->b);p?p->n=x->n:(C=x->n);free(x);}
void pb(t_c*x){char*l;while((l=strstr(x->b,"\n"))){*l=0;char o[1024];sprintf(o,"client %d: %s\n",x->id,x->b);br(x->id,o);memmove(x->b,l+1,strlen(l+1)+1);}}

int main(int ac,char**av){
	if(ac!=2){write(2,"Wrong number of arguments\n",26);return 1;}
	int s=socket(AF_INET,SOCK_STREAM,0);if(s<0)fe();
	struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(0x7F000001);a.sin_port=htons(atoi(av[1]));
	if(bind(s,(void*)&a,sizeof(a))<0)fe();
	if(listen(s,100)<0)fe();
	FD_ZERO(&R);FD_ZERO(&W);FD_SET(s,&R);M=s;
	while(1){fd_set x=R,y=W;if(select(M+1,&x,&y,0,0)<0)fe();
		if(FD_ISSET(s,&x)){int f=accept(s,0,0);if(f>=0)ad(f);}
		t_c*p=0,*c=C;while(c){if(FD_ISSET(c->fd,&x)){char b[1024];int n=recv(c->fd,b,1023,0);
			if(n<=0){t_c*d=c;c=c->n;rm(p,d);continue;}
			b[n]=0;char*n_=realloc(c->b,(c->b?strlen(c->b):0)+n+1);if(!n_)fe();c->b=n_;if(!c->b){c->b=malloc(1);if(!c->b)fe();*c->b=0;}strcat(c->b,b);pb(c);}
		p=c;c=c->n;}}
}