from pathlib import Path
import subprocess, tempfile, resource
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
project=Path(__file__).resolve().parents[2]
def function(text,name):
    pos=text.index(name+'(');begin=text.rfind('\n',0,pos)+1;start=text.index('{',pos);end=start+1;depth=1
    while depth:
        if text[end]=='{':depth+=1
        elif text[end]=='}':depth-=1
        end+=1
    return text[begin:end]
prefix=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <assert.h>
#define CONFIG_IPV6 0
#define AGENT_POLL_TIMEOUT 1
#define LOGE(...) ((void)0)
typedef struct {int unused;} Address;
typedef struct {int fd;} UdpSocket;
typedef struct {UdpSocket udp_sockets[2];} Agent;
static int timeout_seen, receives;
int select_mock(int nfds,fd_set*r,fd_set*w,fd_set*e,struct timeval*t){
 (void)w;(void)e;assert(nfds==5);assert(FD_ISSET(4,r));assert(!FD_ISSET(0,r));
 timeout_seen=t->tv_usec;return 0;
}
int udp_socket_recvfrom(UdpSocket*s,Address*a,uint8_t*b,int n){(void)s;(void)a;(void)b;(void)n;receives++;return 0;}
#define select select_mock
'''
with tempfile.TemporaryDirectory() as temp:
  for variant in ['old','fixed']:
    directory='deps/libpeer/src' if variant=='old' else 'components/peer/overrides'
    text=(project/directory/'agent.c').read_text()
    body=function(text,'agent_socket_recv')
    call='agent_socket_recv(&agent,&address,buf,sizeof(buf)'+(',0)' if variant=='fixed' else ')')
    main='int main(void){Agent agent={{{4},{0}}};Address address;uint8_t buf[32];'+call+';assert(timeout_seen==0);assert(receives==0);'
    if variant=='fixed':main+='agent_socket_recv(&agent,&address,buf,sizeof(buf),1000);assert(timeout_seen==1000);'
    main+='return 0;}'
    c=Path(temp)/(variant+'.c');c.write_text(prefix+body+main);binary=Path(temp)/variant
    subprocess.run(['gcc','-Wall','-Wextra','-Werror','-Wno-sign-compare',str(c),'-o',str(binary)],check=True)
    r=subprocess.run([str(binary)],capture_output=True)
    if variant=='old':assert r.returncode!=0
    else:assert r.returncode==0,r.stderr
    print(variant+': '+('unnecessary wait reproduced' if variant=='old' else 'media poll does not wait; handshake timeout preserved'))
