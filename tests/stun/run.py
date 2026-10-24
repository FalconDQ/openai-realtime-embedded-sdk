from pathlib import Path
import subprocess, tempfile, resource
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
project=Path(__file__).resolve().parents[2]
def function(text,name):
    pos=text.index(name+'(');begin=text.rfind('\n',0,pos)+1;opening=text.index('{',pos);end=opening+1;depth=1
    while depth:
        if text[end]=='{':depth+=1
        elif text[end]=='}':depth-=1
        end+=1
    return text[begin:end]
header=(project/'deps/libpeer/src/stun.h').read_text()
header='\n'.join(x for x in header.splitlines() if not x.startswith('#include'))
prefix=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <arpa/inet.h>
#define LOGD(...) ((void)0)
#define LOGE(...) ((void)0)
typedef struct {int unused;} Address;
'''
stubs=r'''
static int hashes;
void utils_get_hmac_sha1(const char* data,size_t length,const char* key,size_t keylen,unsigned char* output){
  (void)data;(void)key;(void)keylen;
  if(length>STUN_ATTR_BUF_SIZE)exit(99);
  hashes++;memset(output,0xa5,20);
}
void stun_get_mapped_address(char*v,uint8_t*m,Address*a){(void)v;(void)m;(void)a;}
'''
suffix=r'''
static void attr(uint8_t*b,size_t pos,uint16_t type,uint16_t size){
  StunAttribute*a=(StunAttribute*)(b+pos);a->type=htons(type);a->length=htons(size);
}
int main(int argc,char**argv){
  assert(argc==2);uint8_t b[256]={0};StunHeader*h=(StunHeader*)b;
  h->type=htons(STUN_METHOD_BINDING);h->magic_cookie=htonl(MAGIC_COOKIE);
  size_t size=20;
  if(strcmp(argv[1],"fingerprint_only")==0){
    size=28;h->length=htons(8);attr(b,20,STUN_ATTR_TYPE_FINGERPRINT,4);
    uint32_t fp;stun_calculate_fingerprint((char*)b,20,&fp);memcpy(b+24,&fp,4);
    assert(stun_msg_is_valid(b,size,"test")==-1);assert(hashes==0);
  }else if(strcmp(argv[1],"valid")==0 || strcmp(argv[1],"no_fingerprint")==0){
    int fp=strcmp(argv[1],"valid")==0;size=fp?52:44;h->length=htons(size-20);
    attr(b,20,STUN_ATTR_TYPE_MESSAGE_INTEGRITY,20);memset(b+24,0xa5,20);
    if(fp){attr(b,44,STUN_ATTR_TYPE_FINGERPRINT,4);uint32_t f;stun_calculate_fingerprint((char*)b,44,&f);memcpy(b+48,&f,4);}
    assert(stun_msg_is_valid(b,size,"test")==0);assert(hashes==1);
    b[24]^=1;assert(stun_msg_is_valid(b,size,"test")==-1);
  }else if(strcmp(argv[1],"truncated")==0){
    h->length=htons(8);assert(stun_msg_is_valid(b,20,"test")==-1);
  }else if(strcmp(argv[1],"invalid_attribute")==0){
    h->length=htons(8);attr(b,20,STUN_ATTR_TYPE_NONCE,80);
    assert(stun_msg_is_valid(b,28,"test")==-1);
  }
  return 0;
}
'''
with tempfile.TemporaryDirectory() as temp:
  for variant in ['old','fixed']:
    source=project/('deps/libpeer/src' if variant=='old' else 'components/peer/overrides')/'stun.c'
    text=source.read_text();begin=text.index('uint32_t CRC32_TABLE');end=text.index('};',begin)+2
    bodies=text[begin:end]+'\n'
    if variant=='fixed':bodies+=function(text,'stun_attributes_valid')+'\n'
    for name in ['stun_parse_msg_buf','stun_calculate_fingerprint','stun_msg_is_valid']:bodies+=function(text,name)+'\n'
    c=Path(temp)/(variant+'.c');c.write_text(prefix+header+stubs+bodies+suffix);exe=Path(temp)/variant
    subprocess.run(['gcc','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    for case in ['valid','no_fingerprint','fingerprint_only','truncated','invalid_attribute']:
      r=subprocess.run([str(exe),case],capture_output=True,timeout=3)
      if variant=='fixed':assert r.returncode==0,(case,r.stderr)
      if variant=='old' and case=='fingerprint_only':assert r.returncode==99,r.stderr
      print(variant,case,'passed' if r.returncode==0 else 'fault reproduced')
