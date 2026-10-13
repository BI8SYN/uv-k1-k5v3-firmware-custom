#include <assert.h>
#include <stdio.h>
#include <string.h>
#define AIS_HOST_TEST
#include "../aisrx_app.c"
static uint32_t hw[12],clock_cycles,sysval,selected_freq;
static unsigned scenario,sample_number,wave_n;
static bool exit_sent;
static int16_t wave[4096];
static unsigned level;
static const uint32_t addresses[]={0xe000e014,0x40012400,0x40012408,0x40012414,0x40012438,0x40012450,0x50000000,0x40007400,0x40007404,0x40007408,0x4002103c};
/* Synthetic NRZI/HDLC input: no captured data or runtime replay assets. */
static void bit(unsigned b){if(!b)level^=1;for(unsigned i=0;i<5;i++){assert(wave_n<4096);wave[wave_n++]=level?640:-640;}}
static void flag(void){for(unsigned i=0;i<8;i++)bit((0x7e>>i)&1);}
static void put(uint8_t *p,unsigned start,unsigned count,uint32_t v){while(count--){unsigned i=start+count;p[i/8]|=(v&1u)<<(7-i%8);v>>=1;}}
static void make_wave(void){
 uint8_t p[23]={0};put(p,0,6,1);put(p,8,30,366998680);put(p,50,10,123);
 put(p,61,28,(uint32_t)-70800000);put(p,89,27,20400000);put(p,116,12,2712);put(p,128,9,270);
 uint16_t c=0xffff;for(unsigned i=0;i<21;i++){c^=p[i];for(unsigned k=0;k<8;k++)c=(c>>1)^((c&1)?0x8408:0);}c=~c;p[21]=c;p[22]=c>>8;
 flag();flag();flag();unsigned ones=0;
 for(unsigned i=0;i<23;i++)for(unsigned k=0;k<8;k++){unsigned b=(p[i]>>k)&1;bit(b);if(b){if(++ones==5){bit(0);ones=0;}}else ones=0;}
 flag();
}
volatile uint32_t *ais_test_reg(uint32_t a){
 if(a==0xe000e018){clock_cycles+=100;sysval=479999-clock_cycles%480000;return &sysval;}
 if(a==0x40012400 && scenario==5){hw[1]=0;clock_cycles+=1000;}
 if(a==0x40012400 && scenario==4)hw[1]=sample_number>=100 && sample_number<200?0:2;
 if(a==0x40012408 && scenario==4 && sample_number>=100 && sample_number<200)sample_number++;
 if(a==0x40012400 && scenario==4 && sample_number==500){clock_cycles+=3000;sample_number++;}
 if(a==0x40012400 && scenario==1)hw[1]=sample_number<2000?0:2;
 if(a==0x40012400 && scenario==2 && sample_number==1000){clock_cycles+=3000;sample_number++;}
 if(a==0x40012450){unsigned n=sample_number++;hw[5]=2048;if(scenario>=3 && n>=1200 && n<1200+wave_n)hw[5]+=wave[n-1200];}
 if(a==0x40012408 && scenario==1 && sample_number<2000)sample_number++;
 for(unsigned i=0;i<11;i++)if(addresses[i]==a)return hw+i;
 assert(0);return hw;
}
static uint16_t bk[256],saved[256];static unsigned step,draws,freq_draws;static uint8_t fb[7][128],status[128];
static uint8_t key(void){
 if(scenario==5){if(clock_cycles>=6000u*48000u && !exit_sent){exit_sent=true;return APP_KEY_EXIT;}return APP_KEY_INVALID;}
 if(scenario>=3){static const uint8_t keys[]={19,19,19,13,19,19,19};assert(step<sizeof keys);return keys[step++];}
 static const uint8_t keys[]={1,19,2,19,5,19,0,19,0,19,10,19,14,19,13,19,19,19};
 assert(step<sizeof keys);uint8_t result=keys[step++];return result==APP_KEY_STAR && scenario?APP_KEY_INVALID:result;
}
static void delay(uint32_t ms){clock_cycles+=ms*48000;}
static uint32_t ticks(void){return clock_cycles/48000;}
static uint32_t freq(void){return selected_freq;}
static uint16_t readreg(uint8_t r){return r==0x0c?2:bk[r];}
static void writereg(uint8_t r,uint16_t v){assert(r!=0x38 && r!=0x39 && r!=0x30 && r!=0x33);bk[r]=v;}
static void af(uint8_t m){assert(m==APP_AF_FM);bk[0x47]=m;}
static void audio(bool on){if(on)assert(g.speaker);}
static void nothing(void){}
static void battery_sample(void){assert((ADC_SQR3&31)==8);}
static void text(const char *s,uint8_t x,uint8_t e,uint8_t y){(void)e;assert(x+strlen(s)*7<=128 && y<7);assert(!strstr(s,"experimental"));if(g.count && y==1)assert(!strcmp(s,"LAT +034.00000"));if(g.count && y==2)assert(!strcmp(s,"LON -118.00000"));}
static void tiny(const char *s,uint8_t x,uint8_t y,bool st,bool fill){(void)fill;assert(x+strlen(s)*4<=128 && y+6<=(st?8:56));assert(!strstr(s,"Test") && !strstr(s,"REPLAY"));}
static void inverse(const char *s,uint8_t x,uint8_t y,bool st,bool fill,uint8_t end){assert(end==x+strlen(s)*4 && x>=2);tiny(s,x,y*8+1,st,fill);}
static void display_freq(const char *s,uint8_t x,uint8_t row,bool st){assert(!st && row==4 && x==0);char expected[16];snprintf(expected,sizeof expected,"%u.%03u",selected_freq/100000u,(selected_freq%100000u)/100);assert(!strcmp(s,expected));freq_draws++;}
static void blit(void){draws++;}
static uint16_t asset(uint16_t off,void *p,uint16_t n){(void)off;(void)p;(void)n;assert(!"production app must not read assets");return 0;}
int main(void){
 make_wave();
 const uint32_t frequencies[]={43613750,16197500,16202500,100000000};
 for(unsigned f=0;f<4;f++)for(scenario=0;scenario<6;scenario++){
 exit_sent=false;selected_freq=frequencies[f];step=draws=freq_draws=clock_cycles=sample_number=0;
 for(unsigned i=0;i<256;i++)bk[i]=saved[i]=i*13+17;
 for(unsigned i=0;i<11;i++)hw[i]=i*31+4;
 hw[0]=479999;hw[1]=2;hw[4]=8;hw[5]=2048;uint32_t old[11];memcpy(old,hw,sizeof old);
 app_api_t api={.get_key=key,.ticks_ms=ticks,.delay_ms=delay,.rx_freq=freq,.bk_read=readreg,.bk_write=writereg,.set_af=af,.audio_path=audio,.asset_read=asset,.print_normal=text,.print_bold=text,.print_tiny=tiny,.print_inverse=inverse,.display_freq=display_freq,.display_clear=nothing,.status_clear=nothing,.draw_battery=nothing,.battery_sample=battery_sample,.backlight_on=nothing,.backlight_update=nothing,.blit_full=blit,.blit_status=nothing,.fb=fb,.status_line=status};
 app_main(&api);assert(draws>0 && freq_draws>0 && g.freq==selected_freq);
 if(scenario==3 || scenario==4)assert(g.count>=1 && g.msg.mmsi==366998680);
 if(scenario==1)assert(g.timeouts>0);
 if(scenario==2)assert(g.late>0);
 if(scenario==4)assert(g.late>0 && g.timeouts>0);
 if(scenario==5)assert(g.span==0 && g.timeouts>0);
 for(unsigned i=0;i<256;i++)assert(bk[i]==saved[i]);
 for(unsigned i=0;i<11;i++)if(i!=1 && i!=2 && i!=8)assert(hw[i]==old[i]);
 }
 puts("selected VFO unchanged, no assets, synthetic RX, recovery, UI and cleanup PASS");
}
