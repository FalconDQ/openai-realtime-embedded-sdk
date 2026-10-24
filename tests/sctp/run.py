from pathlib import Path
import subprocess, tempfile, resource
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
base=Path(__file__).resolve().parents[2]
def function(text,name):
    pos=text.index(name+'('); start=text.rfind('\n',0,pos)+1
    opening=text.index('{',pos);depth=1;end=opening+1
    while depth:
        if text[end]=='{':depth+=1
        elif text[end]=='}':depth-=1
        end+=1
    return text[start:end]
header=(base/'deps/libpeer/src/sctp.h').read_text()
header='\n'.join(line for line in header.splitlines() if not line.startswith('#include'))
# Leading newline: 'header' above is '\n'.join()ed, so it has no trailing one.
stats='\n'+(base/'components/peer/overrides/oai_peer_stats.h').read_text()+'\nOaiPeerStats oai_peer_stats;\n'
prefix=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <arpa/inet.h>
#define CONFIG_USE_USRSCTP 0
#define CONFIG_MTU 1300
#define LOGD(...) ((void)0)
#define LOGI(...) ((void)0)
#define LOGW(...) ((void)0)
#define LOGE(...) ((void)0)
typedef struct {int unused;} DtlsSrtp;
typedef struct {int unused;} Buffer;
'''
stubs=r'''
static int writes;
static unsigned char output[CONFIG_MTU];
static size_t output_len;
// File-scope state the fixed sctp_incoming_data declares just above itself
// in the real source; the text-extracted function body alone doesn't carry
// those declarations along, so they're stubbed here to match. Marked unused
// since the 'old' variant (compiled with the same stubs) never touches them.
static uint32_t s_sctp_rx_next_tsn __attribute__((unused));
static bool s_sctp_rx_tsn_valid __attribute__((unused));
#define SCTP_REORDER_SLOTS 4
#define SCTP_REORDER_MAX_DATA 1280
typedef struct {
  bool used;
  uint32_t tsn;
  uint16_t sid;
  uint16_t data_len;
  uint8_t data[SCTP_REORDER_MAX_DATA];
} SctpReorderSlot;
static SctpReorderSlot s_sctp_reorder[SCTP_REORDER_SLOTS] __attribute__((unused));
static uint32_t sctp_get_checksum(Sctp *s,const uint8_t*b,size_t n){(void)s;(void)b;(void)n;return 0;}
static int dtls_srtp_write(DtlsSrtp*d,const unsigned char*b,size_t n){(void)d;assert(n<=sizeof(output));memcpy(output,b,n);output_len=n;writes++;return n;}
'''
suffix=r'''
static char g_order[8];
static int g_order_n;
static void record_message(char *msg, size_t len, void *ud, uint16_t sid) {
  (void)ud; (void)sid; (void)len;
  if (g_order_n < (int)sizeof(g_order)) g_order[g_order_n++] = msg[0];
}
int main(int argc,char **argv){
  Sctp s={0};unsigned char packet[64]={0};
  assert(argc==2);
  SctpChunkCommon *chunk=(SctpChunkCommon*)(packet+sizeof(SctpHeader));
  if(strcmp(argv[1],"heartbeat")==0){
    chunk->type=SCTP_HEARTBEAT;chunk->length=htons(9);
    memcpy(packet+16,"hello",5);
    sctp_incoming_data(&s,(char*)packet,24);
    assert(writes==1 && output_len==24);
    assert(output[12]==SCTP_HEARTBEAT_ACK);
    assert(memcmp(output+16,"hello",5)==0);
  }else if(strcmp(argv[1],"zero")==0){
    chunk->type=SCTP_SACK;chunk->length=0;
    sctp_incoming_data(&s,(char*)packet,16);assert(writes==0);
  }else if(strcmp(argv[1],"oversize")==0){
    chunk->type=SCTP_DATA;chunk->length=htons(400);
    sctp_incoming_data(&s,(char*)packet,16);assert(writes==0);
  }else if(strcmp(argv[1],"padding")==0){
    chunk->type=SCTP_HEARTBEAT;chunk->length=htons(9);
    chunk=(SctpChunkCommon*)(packet+24);
    chunk->type=SCTP_HEARTBEAT_ACK;chunk->length=htons(4);
    sctp_incoming_data(&s,(char*)packet,28);
    assert(writes==1 && output[12]==SCTP_HEARTBEAT_ACK);
  }else if(strcmp(argv[1],"cookie")==0){
    chunk->type=SCTP_INIT_ACK;chunk->length=htons(20);
    sctp_incoming_data(&s,(char*)packet,32);assert(writes==0);
  }else if(strcmp(argv[1],"a_rwnd")==0){
    SctpDataChunk *dc=(SctpDataChunk*)chunk;
    dc->type=SCTP_DATA;dc->length=htons(16);dc->tsn=htonl(0);dc->ppid=htonl(0);
    sctp_incoming_data(&s,(char*)packet,28);
    assert(writes==1);
    uint32_t a_rwnd;memcpy(&a_rwnd,output+20,4);
    assert(ntohl(a_rwnd)>2);
  }else if(strcmp(argv[1],"bundle")==0){
    /* DATA chunk followed by a HEARTBEAT bundled in the same packet. */
    SctpDataChunk *dc=(SctpDataChunk*)chunk;
    dc->type=SCTP_DATA;dc->length=htons(16);dc->tsn=htonl(0);dc->ppid=htonl(0);
    SctpChunkCommon *hb=(SctpChunkCommon*)(packet+sizeof(SctpHeader)+16);
    hb->type=SCTP_HEARTBEAT;hb->length=htons(9);
    memcpy(packet+sizeof(SctpHeader)+16+4,"hello",5);
    sctp_incoming_data(&s,(char*)packet,sizeof(SctpHeader)+16+12);
    assert(writes==2);
  }else if(strcmp(argv[1],"tsn_gap")==0){
    /* First DATA (tsn=0) establishes the next expected TSN as 1. */
    SctpDataChunk *dc=(SctpDataChunk*)chunk;
    dc->type=SCTP_DATA;dc->length=htons(16);dc->tsn=htonl(0);dc->ppid=htonl(0);
    sctp_incoming_data(&s,(char*)packet,28);
    assert(writes==1);
    unsigned char first_ack[4];memcpy(first_ack,output+16,4);
    /* Second DATA jumps to tsn=5: a gap, not the expected tsn=1. */
    dc->tsn=htonl(5);
    sctp_incoming_data(&s,(char*)packet,28);
    assert(writes==2);
    /* Must still ack tsn=0, never the gapped tsn=5. */
    assert(memcmp(output+16,first_ack,4)==0);
  }else if(strcmp(argv[1],"tsn_recover")==0){
    /* Arrival order 100,102,103,101: must buffer the out-of-order ones and,
       once the gap (101) fills in, deliver everything exactly once in TSN
       order (100,101,102,103), then ack through 103. */
    s.onmessage=record_message;
    SctpDataChunk *dc=(SctpDataChunk*)chunk;
    dc->type=SCTP_DATA;dc->ppid=htonl(DATA_CHANNEL_PPID_DOMSTRING);
    const uint32_t tsns[4]={100,102,103,101};
    const char payload[4]={'A','C','D','B'};
    for(int i=0;i<4;i++){
      dc->tsn=htonl(tsns[i]);
      dc->length=htons(sizeof(SctpDataChunk)+1);
      dc->data[0]=payload[i];
      sctp_incoming_data(&s,(char*)packet,sizeof(SctpHeader)+sizeof(SctpDataChunk)+1);
    }
    assert(writes==4);
    assert(g_order_n==4);
    assert(g_order[0]=='A'&&g_order[1]=='B'&&g_order[2]=='C'&&g_order[3]=='D');
    uint32_t ack;memcpy(&ack,output+16,4);
    assert(ntohl(ack)==103);
  }else if(strcmp(argv[1],"reorder_full")==0){
    /* 100 in order, then 6 chunks past a gap at 101: four slots fill, the
       other two must be counted as dropped, and a duplicate of a held TSN
       must not be counted as a drop. */
    s.onmessage=record_message;
    SctpDataChunk *dc=(SctpDataChunk*)chunk;
    dc->type=SCTP_DATA;dc->ppid=htonl(DATA_CHANNEL_PPID_DOMSTRING);
    dc->length=htons(sizeof(SctpDataChunk)+1);dc->data[0]='x';
    const uint32_t tsns[8]={100,102,103,104,105,106,107,102};
    for(int i=0;i<8;i++){
      dc->tsn=htonl(tsns[i]);
      sctp_incoming_data(&s,(char*)packet,sizeof(SctpHeader)+sizeof(SctpDataChunk)+1);
    }
    assert(oai_peer_stats.sctp_out_of_order==7);
    assert(oai_peer_stats.sctp_reorder_full==2);
  }
  return 0;
}
'''
with tempfile.TemporaryDirectory() as temp:
  for variant in ['old','fixed']:
    src_text=(base/('deps/libpeer/src' if variant=='old' else 'components/peer/overrides')/'sctp.c').read_text()
    body=function(src_text,'sctp_incoming_data')
    if variant=='fixed':
      # The reorder buffer lives in two helper functions sctp_incoming_data
      # calls; 'old' (upstream) doesn't have them at all.
      body=function(src_text,'sctp_reorder_reset')+function(src_text,'sctp_reorder_flush')+body
    text=prefix+header+stats+stubs+body+suffix
    c=Path(temp)/(variant+'.c');c.write_text(text);exe=Path(temp)/variant
    subprocess.run(['gcc','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    for case in ['heartbeat','zero','oversize','padding','cookie','a_rwnd','bundle','tsn_gap','tsn_recover','reorder_full']:
      try:
        result=subprocess.run([str(exe),case],capture_output=True,timeout=1)
        passed=result.returncode==0
      except subprocess.TimeoutExpired:passed=False
      if variant=='fixed':assert passed,case
      elif case in ['heartbeat','zero','padding','a_rwnd','bundle','tsn_gap','tsn_recover','reorder_full']:assert not passed,case
      print(variant,case,'passed' if passed else 'fault reproduced')
