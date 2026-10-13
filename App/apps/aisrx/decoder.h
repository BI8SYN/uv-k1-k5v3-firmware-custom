#ifndef AIS_DECODER_H
#define AIS_DECODER_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define AIS_MAX 64u
#define AIS_NA INT32_MAX
typedef struct {uint32_t mmsi; int32_t lon,lat; uint16_t speed,course,heading; uint8_t type;} ais_message_t;
typedef struct {
 int32_t dc,prev; uint32_t phase;
 uint16_t crc,n;
 uint8_t level,window,delay,ones,byte,bits,active;
 uint8_t data[AIS_MAX];
} ais_decoder_t;
static uint32_t ais_field(const uint8_t *p,unsigned start,unsigned count) {
 uint32_t v=0; while(count--) {v=(v<<1)|((p[start>>3]>>(7-(start&7)))&1);start++;}return v;
}
static int32_t ais_signed(const uint8_t *p,unsigned start,unsigned count) {
 uint32_t v=ais_field(p,start,count);return (int32_t)(v<<(32-count))>>(32-count);
}
static bool ais_parse(const uint8_t *p,unsigned n,ais_message_t *m) {
 if(n<5) return false;
 m->type=ais_field(p,0,6);m->mmsi=ais_field(p,8,30);
 if(!m->type || m->type>27 || !m->mmsi || m->mmsi>999999999u)return false;
 m->lon=m->lat=AIS_NA;m->speed=1023;m->course=3600;m->heading=511;
 unsigned x;
 if(m->type<=3) {if(n!=21)return false;x=61;m->speed=ais_field(p,50,10);}
 else if(m->type==18){if(n!=21)return false;x=57;m->speed=ais_field(p,46,10);}
 else return true;
 m->lon=ais_signed(p,x,28);m->lat=ais_signed(p,x+28,27);
 if(m->lon>108000000 || m->lon< -108000000)m->lon=AIS_NA;
 if(m->lat>54000000 || m->lat< -54000000)m->lat=AIS_NA;
 m->course=ais_field(p,x+55,12);m->heading=ais_field(p,x+67,9);
 return true;
}
static void ais_bit(ais_decoder_t *d,uint8_t b,void (*got)(const uint8_t *,unsigned)) {
 d->window=(d->window>>1)|(b<<7);
 if(d->window==0x7e){
  if(d->active && !d->bits && d->n>=7 && d->crc==0xf0b8)got(d->data,d->n-2);
  d->n=d->bits=d->byte=d->ones=d->delay=0;d->crc=0xffff;d->active=1;return;
 }
 if(d->delay<8)d->delay++;
 if(d->delay<8 || !d->active)return;
 b=d->window&1;
 if(b){if(++d->ones>=6){d->active=0;return;}}
 else {if(d->ones==5){d->ones=0;return;}d->ones=0;}
 d->byte|=b<<d->bits;
 if(++d->bits==8){
  if(d->n==AIS_MAX){d->active=0;return;}
  uint16_t c=d->crc^d->byte;
  for(unsigned i=0;i<8;i++)c=(c>>1)^((c&1)?0x8408:0);
  d->crc=c;d->data[d->n++]=d->byte;d->bits=d->byte=0;
 }
}
static inline void ais_sample(ais_decoder_t *d,int32_t sample,void (*got)(const uint8_t *,unsigned)) {
 d->dc+=((sample*256)-d->dc)/256;
 int32_t v=sample-d->dc/256;
 if(!v)v=d->prev;
 d->phase+=13107u;
 if((v>0)!=(d->prev>0))d->phase+=(39322-(int32_t)d->phase)>>2;
 d->prev=v;
 if(d->phase>=65536u){d->phase-=65536u;uint8_t lv=v>0;ais_bit(d,lv==d->level,got);d->level=lv;}
}
#endif
