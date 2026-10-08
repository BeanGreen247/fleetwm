#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
static double now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
int main(int c,char**v){
  size_t chunk=atoi(v[1]); int sv[2]; socketpair(AF_UNIX,SOCK_STREAM,0,sv);
  int sz=4<<20; setsockopt(sv[0],SOL_SOCKET,SO_SNDBUF,&sz,4); setsockopt(sv[1],SOL_SOCKET,SO_RCVBUF,&sz,4);
  fcntl(sv[1],F_SETFL,O_NONBLOCK);
  std::string msg(1<<20,'x'); std::string buf; char b[65536];
  double best=1e9;
  for(int r=0;r<9;r++){
    size_t off=0; while(off<msg.size()){ssize_t n=write(sv[0],msg.data()+off,msg.size()-off); if(n<=0)break; off+=n; if(off>=(size_t)(1<<20))break;}
    double t=now(); buf.clear();
    for(;;){ssize_t n=recv(sv[1],b,chunk,0); if(n>0){buf.append(b,n);continue;} break;}
    double d=now()-t; if(d<best)best=d;
  }
  printf("chunk %zu: 1 MB read in %.3f ms\n",chunk,best*1e3);}
