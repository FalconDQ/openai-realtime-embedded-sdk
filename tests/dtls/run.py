from pathlib import Path
import subprocess
import tempfile
import resource
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

base=Path(__file__).resolve().parent
project=base.parent.parent
upstream=project/'deps/libpeer/src'

def function(text,name):
    pos=text.index(name+'(')
    start=text.rfind('\n',0,pos)+1
    opening=text.index('{',pos)
    depth=1
    end=opening+1
    while depth:
        if text[end]=='{': depth+=1
        elif text[end]=='}': depth-=1
        end+=1
    return text[start:end]

prefix=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#define MBEDTLS_ERR_SSL_WANT_READ -100
#define MBEDTLS_ERR_SSL_WANT_WRITE -101
#define CONFIG_TLS_READ_TIMEOUT 3
#define PEER_CONNECTION_CONNECTED 1
#define PEER_CONNECTION_COMPLETED 2
typedef struct {int unused;} Agent;
typedef struct {void *user_data; int ssl;} DtlsSrtp;
typedef struct {int agent_ret; unsigned char agent_buf[32]; int state; Agent agent;} PeerConnection;
static int receives, reads;
int agent_recv(Agent *agent, unsigned char *buf, size_t len) {
    (void)agent;(void)buf;(void)len; receives++;return 7;
}
int mbedtls_ssl_read(int *ssl, unsigned char *buf, size_t len) {
    (void)ssl;(void)buf;(void)len;reads++;return reads<3?MBEDTLS_ERR_SSL_WANT_READ:7;
}
'''
suffix=r'''
int main(int argc,char **argv) {
    unsigned char buf[32];
    PeerConnection pc={0}; DtlsSrtp dtls={&pc,0};
    if(argc>1&&strcmp(argv[1],"receive")==0) {
        pc.state=PEER_CONNECTION_COMPLETED;pc.agent_ret=5;pc.agent_buf[0]=42;
        assert(peer_connection_dtls_srtp_recv(&dtls,buf,sizeof(buf))==5);
        assert(buf[0]==42);
        assert(peer_connection_dtls_srtp_recv(&dtls,buf,sizeof(buf))==MBEDTLS_ERR_SSL_WANT_READ);
        assert(receives==0);
        pc.state=PEER_CONNECTION_CONNECTED;
        assert(peer_connection_dtls_srtp_recv(&dtls,buf,sizeof(buf))==7);
        assert(receives==1);
    } else {
        assert(dtls_srtp_read(&dtls,buf,sizeof(buf))==MBEDTLS_ERR_SSL_WANT_READ);
        assert(reads==1);
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory() as temp:
    for variant in ['old','fixed']:
        directory=upstream if variant=='old' else project/'components/peer/overrides'
        source=prefix+function((directory/'peer_connection.c').read_text(),'peer_connection_dtls_srtp_recv')+'\n'+function((directory/'dtls_srtp.c').read_text(),'dtls_srtp_read')+suffix
        path=Path(temp)/(variant+'.c');path.write_text(source)
        binary=Path(temp)/variant
        subprocess.run(['gcc','-Wall','-Wextra','-Werror','-Wno-sign-compare',str(path),'-o',str(binary)],check=True)
        for case in ['receive','read']:
            result=subprocess.run([str(binary),case],capture_output=True)
            if variant=='old': assert result.returncode!=0
            else: assert result.returncode==0,result.stderr
            print(f'{variant} {case}: '+('fault reproduced' if variant=='old' else 'passed'))
