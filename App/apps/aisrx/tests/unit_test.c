#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../decoder.h"
static unsigned seen;
static uint8_t result[64];static unsigned result_n;
static void got(const uint8_t *p,unsigned n){seen++;result_n=n;memcpy(result,p,n);}
static void put(uint8_t *p,unsigned start,unsigned count,uint32_t v){while(count--){unsigned i=start+count;p[i/8]=(p[i/8]&~(1u<<(7-i%8)))|((v&1u)<<(7-i%8));v>>=1;}}
static void flag(ais_decoder_t *d){for(unsigned i=0;i<8;i++)ais_bit(d,(0x7e>>i)&1,got);}
static void packet(ais_decoder_t *d,const uint8_t *p,unsigned n,bool bad){
 uint16_t c=0xffff;uint8_t bytes[80];assert(n<78);memcpy(bytes,p,n);
 for(unsigned i=0;i<n;i++){c^=p[i];for(unsigned k=0;k<8;k++)c=(c>>1)^((c&1)?0x8408:0);}
 c=~c;bytes[n]=c;bytes[n+1]=c>>8;if(bad)bytes[0]^=1;
 flag(d);unsigned ones=0;
 for(unsigned i=0;i<n+2;i++)for(unsigned k=0;k<8;k++){
  unsigned b=(bytes[i]>>k)&1;ais_bit(d,b,got);
  if(b){if(++ones==5){ais_bit(d,0,got);ones=0;}}else ones=0;
 }
 flag(d);
}
int main(void){
 uint8_t p[21]={0};ais_message_t m;
 put(p,0,6,1);put(p,8,30,366998680);put(p,50,10,123);
 put(p,61,28,(uint32_t)-73600000);put(p,89,27,20200000);
 put(p,116,12,2712);put(p,128,9,270);
 assert(ais_parse(p,21,&m));assert(m.mmsi==366998680 && m.lon==-73600000 && m.lat==20200000 && m.speed==123 && m.course==2712 && m.heading==270);
 assert(!ais_parse(p,20,&m));
 put(p,61,28,108600000);put(p,89,27,54600000);put(p,50,10,1023);put(p,116,12,3600);put(p,128,9,511);
 assert(ais_parse(p,21,&m) && m.lon==AIS_NA && m.lat==AIS_NA && m.speed==1023 && m.heading==511);
 put(p,0,6,18);put(p,46,10,25);put(p,57,28,12000000);put(p,85,27,(uint32_t)-6000000);put(p,112,12,450);put(p,124,9,45);
 assert(ais_parse(p,21,&m) && m.lon==12000000 && m.lat==-6000000 && m.speed==25 && m.course==450 && m.heading==45);
 put(p,8,30,0);assert(!ais_parse(p,21,&m));put(p,8,30,999999999);put(p,0,6,28);assert(!ais_parse(p,21,&m));put(p,0,6,3);
 ais_decoder_t d={0};packet(&d,p,21,false);assert(seen==1 && result_n==21 && !memcmp(p,result,21));
 packet(&d,p,21,true);assert(seen==1);
 uint8_t full[77];memset(full,0xff,sizeof full);packet(&d,full,77,false);assert(seen==1);
 packet(&d,p,21,false);assert(seen==2);
 for(unsigned i=0;i<30;i++)ais_bit(&d,1,got);
 packet(&d,p,21,false);assert(seen==3);
 /* A malformed/truncated frame must not prevent the next valid frame. */
 flag(&d);for(unsigned i=0;i<11;i++)ais_bit(&d,i&1,got);flag(&d);assert(seen==3);
 packet(&d,p,21,false);assert(seen==4);
 puts("parser, signed/NA positions, type18, CRC, destuff, overflow, abort/resync PASS");
}
