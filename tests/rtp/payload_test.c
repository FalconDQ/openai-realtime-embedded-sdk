#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "../../components/peer/overrides/rtp_payload.h"

int main(void) {
  size_t offset=0,length=0;
  uint8_t plain[15]={0x80};
  assert(peer_rtp_payload(plain,sizeof(plain),&offset,&length));
  assert(offset==12&&length==3);
  // One CSRC, one 32-bit extension, three codec bytes, four padding bytes.
  uint8_t extended[31]={0xb1};
  extended[16]=0xbe;extended[17]=0xde;extended[19]=1;
  extended[24]=0x11;extended[25]=0x22;extended[26]=0x33;extended[30]=4;
  assert(peer_rtp_payload(extended,sizeof(extended),&offset,&length));
  assert(offset==24&&length==3&&extended[offset]==0x11);
  // The old parser always passed offset 12, leaking extension bytes to Opus.
  assert(offset!=12);
  extended[19]=255;
  assert(!peer_rtp_payload(extended,sizeof(extended),&offset,&length));
  extended[19]=1;extended[30]=0;
  assert(!peer_rtp_payload(extended,sizeof(extended),&offset,&length));
  extended[30]=8;
  assert(!peer_rtp_payload(extended,sizeof(extended),&offset,&length));
  extended[30]=4;extended[0]=0x71;
  assert(!peer_rtp_payload(extended,sizeof(extended),&offset,&length));
  assert(!peer_rtp_payload(plain,11,&offset,&length));
  plain[0]=0x8f;
  assert(!peer_rtp_payload(plain,sizeof(plain),&offset,&length));
  uint8_t empty[12]={0x80};
  assert(!peer_rtp_payload(empty,sizeof(empty),&offset,&length));
  return 0;
}
