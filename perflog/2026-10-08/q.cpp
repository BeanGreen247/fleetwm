#include <string>
#include <chrono>
#include <cstdio>
#include <cstdlib>
static size_t sink;
static void handle(const std::string& l){ sink+=l.size(); }
static double now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
int main(int c,char**v){
  size_t total=atoi(v[1])*1024ul; std::string src; while(src.size()<total) src+="focus-changed 12345 title\n";
  for(int rep=0;rep<5;rep++){
  std::string b=src; double t=now(); size_t pos;
  while((pos=b.find('\n'))!=std::string::npos){ std::string l=b.substr(0,pos); b.erase(0,pos+1); handle(l);} double a=now()-t;
  b=src; t=now(); size_t head=0;
  while((pos=b.find('\n',head))!=std::string::npos){ handle(std::string(b,head,pos-head)); head=pos+1;} b.erase(0,head); double d=now()-t;
  printf("%zuKB erase-per-line %.6f s  head-offset %.6f s\n",total/1024,a,d);}
  return sink==1;}
