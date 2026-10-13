/* AIS RX — receive-only experimental overlay. Apache-2.0.
 * PA4 ADC/bias and raw receive setup follow the upstream APRS/EPIRB apps.
 * Live reception remains unverified without an RF source. */
#include "../app_api.h"
#include <stddef.h>
void *memset(void *p,int v,size_t n){uint8_t *b=p;while(n--)*b++=v;return p;}
void *memcpy(void *p,const void *q,size_t n){uint8_t *d=p;const uint8_t *s=q;while(n--)*d++=*s++;return p;}
#include "decoder.h"
#ifdef AIS_HOST_TEST
extern volatile uint32_t *ais_test_reg(uint32_t address);
#define REG(a) (*ais_test_reg(a))
#else
#define REG(a) (*(volatile uint32_t *)(a))
#endif
#define SYST_LOAD REG(0xe000e014u)
#define SYST_VAL REG(0xe000e018u)
#define ADC_SR REG(0x40012400u)
#define ADC_CR2 REG(0x40012408u)
#define ADC_SMPR3 REG(0x40012414u)
#define ADC_SQR3 REG(0x40012438u)
#define ADC_DR REG(0x40012450u)
#define MODER REG(0x50000000u)
#define DAC_CR REG(0x40007400u)
#define DAC_TRIG REG(0x40007404u)
#define DAC_DHR REG(0x40007408u)
#define RCC REG(0x4002103cu)
static struct {
 const app_api_t *a;ais_decoder_t d;ais_message_t msg;
 uint32_t cyc,prev,freq;uint16_t count,late,timeouts,span;
 uint8_t key,replay,speaker,dirty,diag;
} g;
#define A (g.a)
static uint32_t cycles(void){uint32_t v=SYST_VAL;g.cyc+=v<=g.prev?g.prev-v:g.prev+SYST_LOAD+1-v;g.prev=v;return g.cyc;}
static void got(const uint8_t *p,unsigned n){ais_message_t m;if(ais_parse(p,n,&m)){g.msg=m;if(g.count<65535u)g.count++;g.dirty=1;}}
static void value(char *p,uint32_t n,unsigned digits){while(digits){p[--digits]='0'+n%10;n/=10;}}
static void row(const char *s,unsigned y){A->print_normal(s,0,127,y);}
static void coord(int32_t raw,unsigned y,bool lon){
 char s[]="LAT ---";
 if(raw==AIS_NA){s[1]=lon?'O':'A';s[2]=lon?'N':'T';row(s,y);return;}
 char text[]="LAT +000.00000";text[1]=lon?'O':'A';text[2]=lon?'N':'T';
 if(raw<0){text[4]='-';raw=-raw;}
 value(text+5,(uint32_t)raw/600000u,3);value(text+9,((uint32_t)raw%600000u)/6u,5);row(text,y);
}
static void draw(void){
 A->display_clear();A->status_clear();
 A->print_inverse("AIS RX",2,0,true,true,26);
 A->print_tiny(g.replay?"REPLAY":"LIVE",32,1,true,true);
 if(g.speaker){static const uint8_t sp[]={28,28,62,127,0,34,28,65,34,28};for(unsigned i=0;i<10;i++)A->status_line[59+i]=sp[i];}
 A->draw_battery();
 char s[]="MMSI 000000000";
 if(g.count){value(s+5,g.msg.mmsi,9);row(s,0);coord(g.msg.lat,1,false);coord(g.msg.lon,2,true);
  char v[]="SOG---.- COG---.-";
  if(g.msg.speed<1023){value(v+3,g.msg.speed/10,3);v[7]='0'+g.msg.speed%10;}
  if(g.msg.course<3600){value(v+12,g.msg.course/10,3);v[16]='0'+g.msg.course%10;}
  row(v,3);
 }else {row("WAIT AIS / CRC",0);row("RX experimental",2);}
 char t[]="T:00 HDG:--- OK:00000";value(t+2,g.count?g.msg.type:0,2);
 if(g.count && g.msg.heading<360)value(t+9,g.msg.heading,3);
 value(t+16,g.count,5);A->print_tiny(t,0,33,false,true);
 if(g.diag){char diag[]="L00000 T00000 P000";value(diag+1,g.late,5);value(diag+8,g.timeouts,5);value(diag+15,g.span,3);row(diag,5);}
 else row(g.freq==16197500u?"161.975 MHz":"162.025 MHz",5);
 A->print_tiny(g.freq==16197500u?"1:AIS1":"2:AIS2",2,49,false,true);
 A->print_tiny("0:Spk 5:Test *:Clr",34,49,false,true);
 A->blit_status();A->blit_full();g.dirty=0;
}
static void tune(uint32_t f){
 g.freq=f;A->bk_write(0x38,f);A->bk_write(0x39,f>>16);uint16_t r=A->bk_read(0x30);A->bk_write(0x30,0);A->bk_write(0x30,r);
 memset(&g.d,0,sizeof g.d);A->delay_ms(20);
}
static void replay(void){
 g.replay=1;g.count=0;A->audio_path(false);
 for(unsigned f=0;f<2;f++){
  memset(&g.d,0,sizeof g.d);int8_t buf[128];
  for(unsigned off=0;off<1792;off+=128){
   if(A->asset_read(f*1792+off,buf,128)!=128)break;
   for(unsigned i=0;i<128;i++)ais_sample(&g.d,buf[i],got);
  }
 }
 draw();
}
#ifndef AIS_HOST_TEST
__attribute__((section(".text.entry"),used))
#endif
void app_main(const app_api_t *api){
 memset(&g,0,sizeof g);g.a=api;g.key=APP_KEY_INVALID;
 uint32_t old[]={ADC_SQR3,ADC_SMPR3,MODER,DAC_CR,DAC_DHR,RCC};
 const uint8_t ids[]={0x2b,0x73,0x43,0x38,0x39,0x47,0x33};uint16_t saved[7];
 for(unsigned i=0;i<7;i++)saved[i]=A->bk_read(ids[i]);
 A->audio_path(false);A->set_af(APP_AF_FM);
 A->bk_write(0x2b,(saved[0]|0x0700u)&~7u);A->bk_write(0x73,saved[1]|0x10u);
 A->bk_write(0x43,0x7e28u); /* widest equal strong/weak RX filters */
 A->bk_write(0x33,(saved[6]|4u)&~8u); /* VHF LNA on, UHF LNA off */
 A->battery_sample();
 MODER|=3u<<8;RCC|=1u<<29;DAC_CR=0x3fu;DAC_DHR=2048;DAC_TRIG=1;
 ADC_SMPR3=(old[1]&~(7u<<12))|(((old[1]>>24)&7u)<<12);ADC_SQR3=(old[0]&~31u)|4u;
 tune(16197500u);A->backlight_on();draw();
 g.prev=SYST_VAL;uint32_t next=0,last_house=0,last_draw=A->ticks_ms();unsigned busy=0;int32_t lo=4095,hi=0;
 for(;;){
  uint32_t now=cycles();while((int32_t)(now-next)<0)now=cycles();
  if(!g.replay && now-next>=1000u){g.late++;g.d.active=0;next=now;}
  next+=1000u;
  if(!g.replay){ADC_CR2|=(1u<<22)|(1u<<20);
  unsigned wait=0;while(!(ADC_SR&2u) && ++wait<1000u){}
  if(wait==1000u){g.timeouts++;g.d.active=0;}else{
   int32_t x=ADC_DR&4095u;if(x<lo)lo=x;if(x>hi)hi=x;ais_sample(&g.d,x-2048,got);
  }}
  if(g.cyc-last_house<2400000u)continue;
  last_house=g.cyc;
  if(g.d.active && g.d.n && ++busy<10)continue;
  busy=0;
  uint8_t key=A->get_key();
  if(key!=g.key && key<APP_KEY_INVALID){
   if(key==APP_KEY_EXIT || key==APP_KEY_PTT)break;
   if(key==APP_KEY_1 || key==APP_KEY_2){g.replay=0;g.count=0;tune(key==APP_KEY_1?16197500u:16202500u);g.dirty=1;}
   if(key==APP_KEY_0){g.speaker=!g.speaker;g.dirty=1;}
   if(key==APP_KEY_STAR){g.count=g.late=g.timeouts=0;g.dirty=1;}
   if(key==APP_KEY_5)replay();
   if(key==APP_KEY_MENU){g.diag=!g.diag;g.dirty=1;}
  }
  g.key=key;A->backlight_update();
  A->audio_path(!g.replay && g.speaker && (A->bk_read(0x0c)&2u));
  if(A->ticks_ms()-last_draw>=5000u){
   g.span=(hi-lo)>999?999:hi-lo;lo=4095;hi=0;
   ADC_SQR3=old[0];ADC_SMPR3=old[1];A->battery_sample();
   ADC_SMPR3=(old[1]&~(7u<<12))|(((old[1]>>24)&7u)<<12);ADC_SQR3=(old[0]&~31u)|4u;
   g.dirty=1;last_draw=A->ticks_ms();
  }
  if(g.dirty)draw();
  g.d.active=0;g.prev=SYST_VAL;next=g.cyc; /* reset across intentional housekeeping gaps */
 }
 A->audio_path(false);
 for(unsigned release=0;release<3;){release=A->get_key()==APP_KEY_INVALID?release+1:0;A->delay_ms(10);}
 ADC_SQR3=old[0];ADC_SMPR3=old[1];MODER=old[2];DAC_CR=old[3];DAC_DHR=old[4];RCC=old[5];
 for(unsigned i=0;i<7;i++)A->bk_write(ids[i],saved[i]);
}
