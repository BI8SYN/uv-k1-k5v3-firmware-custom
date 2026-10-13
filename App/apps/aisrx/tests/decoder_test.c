#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../decoder.h"
static unsigned frames, sample_index;
static bool verbose;
static ais_message_t last;
static void got(const uint8_t *p,unsigned n) { ais_message_t m; if(ais_parse(p,n,&m)){last=m;frames++;if(verbose){printf("%u ",sample_index);for(unsigned i=0;i<n;i++)printf("%02x",p[i]);printf("\n");}} }
int main(int argc,char **argv) {
    if(argc>1) {
        verbose=argc>2;FILE *f=fopen(argv[1],"rb");assert(f);ais_decoder_t d={0};int v;
        while((v=fgetc(f))!=EOF){ais_sample(&d,(int8_t)v,got);sample_index++;}
        fclose(f);printf("frames=%u last=%u type=%u\n",frames,last.mmsi,last.type);return frames?0:1;
    }
    uint8_t p[21]={0};ais_message_t m;
    assert(!ais_parse(p,21,&m));
    puts("parser rejects invalid MMSI PASS");
}
