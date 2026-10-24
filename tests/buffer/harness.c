#include "buffer.h"
static unsigned int pool[262144];
static size_t used;
void *memcpy(void *dst, const void *src, size_t n) {
  unsigned char *d=dst; const unsigned char *s=src;
  for(size_t i=0;i<n;i++)d[i]=s[i]; return dst;
}
void *calloc(size_t count,size_t size) {
  size_t bytes=count*size,words=(bytes+3)/4+4;
  if(used+words>262144)return NULL;
  void *p=pool+used;for(size_t i=0;i<words;i++)pool[used+i]=0;
  used+=words;return p;
}
void free(void *p) {(void)p;}
static unsigned int random_state=1234567;
static unsigned int next_random(void) {
  random_state=random_state*1664525u+1013904223u;return random_state;
}
int run_test(void) {
  Buffer *rb=buffer_new(128);
  for(int i=0;i<16;i++)rb->data[rb->size+i]=0xa5;
  unsigned char data[60];
  int sizes[1024],ids[1024],head=0,tail=0;
  for(int step=0;step<50000;step++) {
    if((next_random()%3)!=0) {
      int size=(next_random()%55)+1;
      for(int i=0;i<size;i++)data[i]=(unsigned char)(tail+i);
      int result=buffer_push_tail(rb,data,size);
      if(result>0) {sizes[tail%1024]=size;ids[tail%1024]=tail;tail++;}
    } else {
      int size=0;uint8_t *p=buffer_peak_head(rb,&size);
      if(p) {
        if(head==tail||size!=sizes[head%1024])return 10;
        for(int i=0;i<size;i++)if(p[i]!=(unsigned char)(ids[head%1024]+i))return 11;
        buffer_pop_head(rb);head++;
      } else if(head!=tail)return 12;
    }
    if(rb->head<0||rb->head>=rb->size||rb->tail<0||rb->tail>=rb->size)return 13;
    for(int i=0;i<16;i++)if(rb->data[rb->size+i]!=0xa5)return 19;
  }
  while(head!=tail) {
    int size;uint8_t *p=buffer_peak_head(rb,&size);
    if(!p||size!=sizes[head%1024])return 14;
    for(int i=0;i<size;i++)if(p[i]!=(unsigned char)(ids[head%1024]+i))return 15;
    buffer_pop_head(rb);head++;
  }
  buffer_clear(rb);
  for(int i=0;i<10000;i++) {
    for(int j=0;j<12;j++)data[j]=(unsigned char)i;
    if(buffer_push_tail(rb,data,12)!=12)return 16;
    int size;uint8_t *p=buffer_peak_head(rb,&size);
    if(!p||size!=12||p[0]!=(unsigned char)i)return 17;
    buffer_pop_head(rb);
    if(rb->head<0||rb->head>=rb->size||rb->tail<0||rb->tail>=rb->size)return 18;
  }
  return 0;
}
int main(void) { return run_test(); }
