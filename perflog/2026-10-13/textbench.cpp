// A/B: draw_text / measure_text variants on cairo image surface
#include <cairo.h>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>
static void set_src(cairo_t* cr){ cairo_set_source_rgba(cr,.9,.9,.9,1); }
// A: current
static void applyA(cairo_t* cr,double px,bool b){cairo_select_font_face(cr,"Inter",CAIRO_FONT_SLANT_NORMAL,b?CAIRO_FONT_WEIGHT_BOLD:CAIRO_FONT_WEIGHT_NORMAL);cairo_set_font_size(cr,px);}
static double drawA(cairo_t* cr,const std::string& s,double x,double y,double px,bool b){applyA(cr,px,b);set_src(cr);cairo_move_to(cr,x,y);cairo_show_text(cr,s.c_str());cairo_new_path(cr);cairo_text_extents_t te;cairo_text_extents(cr,s.c_str(),&te);return te.x_advance;}
static double measA(cairo_t* cr,const std::string& s,double px,bool b){applyA(cr,px,b);cairo_text_extents_t te;cairo_font_extents_t fe;cairo_text_extents(cr,s.c_str(),&te);cairo_font_extents(cr,&fe);return te.x_advance+fe.ascent;}
// B: cached faces
static cairo_font_face_t* face(bool b){static cairo_font_face_t* f[2]={cairo_toy_font_face_create("Inter",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL),cairo_toy_font_face_create("Inter",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_BOLD)};return f[b];}
static void applyB(cairo_t* cr,double px,bool b){cairo_set_font_face(cr,face(b));cairo_set_font_size(cr,px);}
static double drawB(cairo_t* cr,const std::string& s,double x,double y,double px,bool b){applyB(cr,px,b);set_src(cr);cairo_move_to(cr,x,y);cairo_show_text(cr,s.c_str());cairo_new_path(cr);cairo_text_extents_t te;cairo_text_extents(cr,s.c_str(),&te);return te.x_advance;}
static double measB(cairo_t* cr,const std::string& s,double px,bool b){applyB(cr,px,b);cairo_text_extents_t te;cairo_font_extents_t fe;cairo_text_extents(cr,s.c_str(),&te);cairo_font_extents(cr,&fe);return te.x_advance+fe.ascent;}
// C: B + one glyph conversion shared by show and extents
static double drawC(cairo_t* cr,const std::string& s,double x,double y,double px,bool b){applyB(cr,px,b);set_src(cr);cairo_scaled_font_t* sf=cairo_get_scaled_font(cr);cairo_glyph_t* g=nullptr;int n=0;cairo_text_cluster_t* c=nullptr;int nc=0;cairo_text_cluster_flags_t fl;
  if(cairo_scaled_font_text_to_glyphs(sf,x,y,s.c_str(),(int)s.size(),&g,&n,&c,&nc,&fl)!=CAIRO_STATUS_SUCCESS)return 0;
  cairo_show_glyphs(cr,g,n);cairo_text_extents_t te;cairo_scaled_font_glyph_extents(sf,g,n,&te);cairo_glyph_free(g);cairo_text_cluster_free(c);return te.x_advance;}
// D: measure cache on top of B
struct K{std::string s;int px;bool b;bool operator==(const K&o)const{return px==o.px&&b==o.b&&s==o.s;}};
struct H{size_t operator()(const K&k)const{return std::hash<std::string>()(k.s)^(k.px*31+k.b);}};
static double measD(cairo_t* cr,const std::string& s,double px,bool b){static std::unordered_map<K,double,H> m;K k{s,(int)px,b};auto it=m.find(k);if(it!=m.end())return it->second;double v=measB(cr,s,px,b);m.emplace(std::move(k),v);return v;}
template<class F> static double ms(F f,int reps=7){std::vector<double> v;for(int r=0;r<reps;r++){auto t=std::chrono::steady_clock::now();f();v.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count());}std::sort(v.begin(),v.end());return v[v.size()/2];}
int main(){
  cairo_surface_t* s=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,1024,768);cairo_t* cr=cairo_create(s);
  std::vector<std::string> L;const char* w[]={"Documents","chromium","fleetwm-taskmgr","Memory","Performance 12%","Süßigkeiten.txt","Screenshot_2026-10-13_07-09-22.png","CPU","1.9 GB"};
  for(int i=0;i<200;i++)L.push_back(std::string(w[i%9])+(i%3?"":" "+std::to_string(i)));
  const int N=200;volatile double sink=0;
  #define DRAW(fn) [&]{for(int rep=0;rep<10;rep++){cairo_set_operator(cr,CAIRO_OPERATOR_CLEAR);cairo_paint(cr);cairo_set_operator(cr,CAIRO_OPERATOR_OVER);for(int i=0;i<N;i++)sink+=fn(cr,L[i],10+(i%4)*200,14+(i/4)*14,13,i%7==0);}}
  fprintf(stderr,"draw: 10 frames x 200 labels (ms per 10 frames, median of 7)\n");
  printf("A current            %.3f\n",ms(DRAW(drawA)));
  printf("B cached face        %.3f\n",ms(DRAW(drawB)));
  printf("C B + shared glyphs  %.3f\n",ms(DRAW(drawC)));
  #define MEAS(fn) [&]{for(int rep=0;rep<100;rep++)for(int i=0;i<N;i++)sink+=fn(cr,L[i],13,i%7==0);}
  fprintf(stderr,"measure: 100 x 200 calls\n");
  printf("measure A current    %.3f\n",ms(MEAS(measA)));
  printf("measure B cached face%.3f\n",ms(MEAS(measB)));
  printf("measure D cache      %.3f\n",ms(MEAS(measD)));
  double a=0,c=0;for(int i=0;i<N;i++){a+=drawA(cr,L[i],0,0,13,i%7==0);c+=drawC(cr,L[i],0,0,13,i%7==0);}printf("advance sums A %.3f C %.3f\n",a,c);
}
