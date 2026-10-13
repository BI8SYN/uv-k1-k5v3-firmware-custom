#include <assert.h>
#include <stdio.h>
#include <string.h>
#define AIS_HOST_TEST
#include "../aisrx_app.c"
static uint32_t hw[12],clock_cycles,sysval;
static unsigned scenario, sample_number;
static uint8_t assets[3584];
static const uint32_t addresses[]={0xe000e014,0x40012400,0x40012408,0x40012414,0x40012438,0x40012450,0x50000000,0x40007400,0x40007404,0x40007408,0x4002103c};
volatile uint32_t *ais_test_reg(uint32_t a){
 if(a==0xe000e018){clock_cycles+=100;sysval=479999-clock_cycles%480000;return &sysval;}
 if(a==0x40012400 && scenario==4)hw[1]=sample_number>=100 && sample_number<200?0:2;
 if(a==0x40012408 && scenario==4 && sample_number>=100 && sample_number<200)sample_number++;
 if(a==0x40012400 && scenario==4 && sample_number==500){clock_cycles+=3000;sample_number++;}
 if(a==0x40012400 && scenario==1){hw[1]=sample_number<2000?0:2;}
 if(a==0x40012400 && scenario==2 && sample_number==1000){clock_cycles+=3000;sample_number++;}
 if(a==0x40012450){
  unsigned n=sample_number++;
  if(scenario>=3 && n>=1200 && n<1200+1792)hw[5]=2048+(int8_t)assets[n-1200]*8;
  else hw[5]=2048;
 }
 if(a==0x40012408 && scenario==1 && sample_number<2000)sample_number++;
 for(unsigned i=0;i<11;i++)if(addresses[i]==a)return hw+i;
 assert(0);return hw;
}
static uint16_t bk[256],saved[256];static unsigned step,draws;static bool replay_seen;static uint8_t fb[7][128],status[128];
static uint8_t key(void){
 if(scenario>=3){static const uint8_t keys[]={19,19,19,13,19,19,19};assert(step<sizeof keys);return keys[step++];}
 static const uint8_t keys[]={5,19,1,19,2,19,0,19,0,19,10,19,13,19,19,19};
 assert(step<sizeof keys);return keys[step++];
}
static void delay(uint32_t ms){clock_cycles+=ms*48000;}
static uint32_t ticks(void){return clock_cycles/48000;}
static uint16_t readreg(uint8_t r){return r==0x0c?2:bk[r];}
static void writereg(uint8_t r,uint16_t v){bk[r]=v;}
static void af(uint8_t m){assert(m==APP_AF_FM);bk[0x47]=m;}
static void audio(bool on){if(on)assert(g.speaker && !g.replay);}
static void nothing(void){}
static void battery_sample(void){assert((ADC_SQR3&31)==8);}
static bool heading_seen, frequency_seen;
static void text(const char *s,uint8_t x,uint8_t e,uint8_t y){(void)e;assert(x+strlen(s)*7<=128 && y<7);if(strstr(s,"161.975"))frequency_seen=true;if(g.replay && y==1)assert(!strcmp(s,"LAT +060.16971"));if(g.replay && y==2)assert(!strcmp(s,"LON +024.97456"));}
static void tiny(const char *s,uint8_t x,uint8_t y,bool st,bool fill){(void)fill;assert(x+strlen(s)*4<=128 && y+6<=(st?8:56));if(strstr(s,"HDG:"))heading_seen=true;}
static void inverse(const char *s,uint8_t x,uint8_t y,bool st,bool fill,uint8_t end){assert(end==x+strlen(s)*4 && x>=2);tiny(s,x,y*8+1,st,fill);}
static void blit(void){draws++;if(g.replay){assert(g.count==2 && g.msg.mmsi==230991740);replay_seen=true;}}
static uint16_t asset(uint16_t off,void *p,uint16_t n){assert(off+n<=sizeof assets);memcpy(p,assets+off,n);return n;}
int main(void){
 FILE *f=fopen("tests/long-beach.s8","rb");assert(f);assert(fread(assets,1,1792,f)==1792);fclose(f);
 f=fopen("tests/helsinki.s8","rb");assert(f);assert(fread(assets+1792,1,1792,f)==1792);fclose(f);
 for(scenario=0;scenario<5;scenario++){
 step=draws=clock_cycles=sample_number=0;replay_seen=heading_seen=frequency_seen=false;
 for(unsigned i=0;i<256;i++)bk[i]=saved[i]=i*13+17;
 for(unsigned i=0;i<11;i++)hw[i]=i*31+4;
 hw[0]=479999;hw[1]=2;hw[4]=8;hw[5]=2048;uint32_t old[11];memcpy(old,hw,sizeof old);
 app_api_t api={.get_key=key,.ticks_ms=ticks,.delay_ms=delay,.bk_read=readreg,.bk_write=writereg,.set_af=af,.audio_path=audio,.asset_read=asset,.print_normal=text,.print_tiny=tiny,.print_inverse=inverse,.display_clear=nothing,.status_clear=nothing,.draw_battery=nothing,.battery_sample=battery_sample,.backlight_on=nothing,.backlight_update=nothing,.blit_full=blit,.blit_status=nothing,.fb=fb,.status_line=status};
 app_main(&api);
 if(scenario<3)assert(replay_seen && draws>=6 && step==16 && heading_seen && frequency_seen);
 else assert(g.count>=1 && g.msg.mmsi==366998680);
 if(scenario==1)assert(g.timeouts>0);
 if(scenario==2)assert(g.late>0);
 if(scenario==4)assert(g.late>0 && g.timeouts>0);
 for(unsigned i=0;i<256;i++)assert(bk[i]==saved[i]);
 for(unsigned i=0;i<11;i++)if(i!=1 && i!=2 && i!=8)assert(hw[i]==old[i]);
 }
 puts("app replay, tuning, audio, key release, UI bounds and hardware restore PASS");
}
