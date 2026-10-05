"""Replay the actual CDC callback and normal/binary parser against backpressure.

The pre-change bytewise cdc_tx was compiled against a 1024-byte no-flow-control
FIFO: an 1800-byte reply retained only 1024 bytes and scheduled work1800times.
Its complete-response assertion failed. This fixture now exercises production
functions, while replacing only Zephyr/USB/storage boundaries.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

def function(source, name):
    import re
    m = re.search(r'^static [^\n]*\b' + re.escape(name) + r'\s*\([^;]*?\)\s*\{', source, re.M)
    if not m:
        raise AssertionError('Missing production function: ' + name)
    start = source.index('{', m.start())
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[m.start():end]

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bonsai_cdc.h"
#include "dual_mirror.h"
#include "dual_engine.h"
#define SP1_DUAL_DECK 1
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ARG_UNUSED(x) (void)(x)
#define BUILD_ASSERT(c,m) _Static_assert(c,m)
struct device {int id;};static struct device device;
static const struct device *cdc=&device;
struct usbd_context {int unused;};
enum {USBD_MSG_CONFIGURATION,USBD_MSG_RESET,USBD_MSG_VBUS_REMOVED,USBD_MSG_UDC_ERROR,
 USBD_MSG_STACK_ERROR,USBD_MSG_CDC_ACM_CONTROL_LINE_STATE};
struct usbd_msg {int type;union{int status;const struct device *dev;};};
#define UART_LINE_CTRL_DTR 1
static struct {uint32_t CYCCNT;} cycle_counter;
#define DWT (&cycle_counter)
static uint32_t g_dual_cdc_us,g_dual_cdc_max,g_dual_cdc_bytes;
struct ring_buf {uint8_t bytes[1024];uint32_t written,read;};
static struct ring_buf g_cdc_rx;
static uint32_t ring_buf_space_get(struct ring_buf *r){return 1024-(r->written-r->read);}
static uint32_t ring_buf_put(struct ring_buf *r,const uint8_t *p,uint32_t n){
 n=MIN(n,ring_buf_space_get(r));for(uint32_t i=0;i<n;i++)r->bytes[(r->written+i)%1024]=p[i];r->written+=n;return n;
}
static uint32_t ring_buf_get(struct ring_buf *r,uint8_t *p,uint32_t n){
 n=MIN(n,r->written-r->read);for(uint32_t i=0;i<n;i++)p[i]=r->bytes[(r->read+i)%1024];r->read+=n;return n;
}
static void ring_buf_reset(struct ring_buf *r){r->read=r->written=0;}
static unsigned irq_lock(void){return 0;}
static void irq_unlock(unsigned key){(void)key;}
static bool tx_enabled,rx_enabled,in_callback;
static uint32_t dtr=1;
static uint8_t driver_tx[1024],wire[32768],driver_rx[64];
static unsigned driver_count,wire_count,driver_rx_count,accept_limit=256,fills,enables;
static int uart_line_ctrl_get(const struct device *dev,int ctrl,uint32_t *out){(void)dev;(void)ctrl;*out=dtr;return 0;}
static void uart_irq_tx_enable(const struct device *dev){(void)dev;tx_enabled=true;enables++;}
static void uart_irq_tx_disable(const struct device *dev){(void)dev;tx_enabled=false;}
static void uart_irq_rx_enable(const struct device *dev){(void)dev;rx_enabled=true;}
static void uart_irq_rx_disable(const struct device *dev){(void)dev;rx_enabled=false;}
static int uart_irq_update(const struct device *dev){(void)dev;assert(in_callback);return 1;}
static int uart_irq_rx_ready(const struct device *dev){(void)dev;return rx_enabled&&driver_rx_count;}
static int uart_irq_tx_ready(const struct device *dev){(void)dev;return tx_enabled&&driver_count<1024;}
static int uart_fifo_read(const struct device *dev,uint8_t *p,int n){
 (void)dev;assert(in_callback);n=MIN((unsigned)n,driver_rx_count);memcpy(p,driver_rx,n);
 memmove(driver_rx,driver_rx+n,driver_rx_count-n);driver_rx_count-=n;return n;
}
static int uart_fifo_fill(const struct device *dev,const uint8_t *p,int n){
 (void)dev;assert(in_callback);fills++;n=MIN((unsigned)n,accept_limit);n=MIN((unsigned)n,1024-driver_count);
 memcpy(driver_tx+driver_count,p,n);driver_count+=n;return n;
}
static int64_t now=3000000000LL;
static int64_t k_uptime_get(void){return now;}
static uint32_t k_uptime_get_32(void){return (uint32_t)now;}
static void (*sleep_hook)(void);
static void k_msleep(int ms){now+=ms;if(sleep_hook){void (*hook)(void)=sleep_hook;sleep_hook=NULL;hook();}}
'''
ADAPTER_STUBS = r'''
#define NUM_SLOTS 16u
#define SLOT0_BLOCK 64u
#define TRACK_BLOCKS 100u
#define NTRK 4u
#define META_MAGIC 0x12345678u
static struct dd_engine dd;
static struct dd_ui_queue dd_ui;
static uint32_t dd_ui_until;
static char dd_ui_line[2048];
static unsigned g_xfer_mode,commit_count,status_count,load_count;
static _Atomic unsigned dd_storage_ok;
static uint32_t dd_capacity=100000;
static uint8_t dd_io[4096],dd_verify[4096];
static bool recording,g_emmc_ready=true;
static bool dual_record_busy(void){return recording;}
static bool dual_commit(void){commit_count++;return true;}
static bool dual_format(void){assert(!"unexpected format");return false;}
static bool emmc_read_blocks(unsigned block,uint8_t *p,unsigned n){(void)block;memset(p,0xa5,n*512);return true;}
static bool emmc_write_blocks(unsigned block,const uint8_t *p,unsigned n){(void)block;(void)p;(void)n;return true;}
static void dual_status(void){status_count++;const uint8_t data[]="{\"status\":1}\n";assert(cdc_tx(data,sizeof(data)-1));}
static void dual_library(void){const uint8_t data[]="{\"library\":1}\n";assert(cdc_tx(data,sizeof(data)-1));}
static void dual_host_load(const char *cmd){(void)cmd;load_count++;const uint8_t data[]="{\"loaded\":1}\n";assert(cdc_tx(data,sizeof(data)-1));}
'''
TESTS = r'''
static void callback(void){in_callback=true;cdc_rx_isr(cdc,NULL);in_callback=false;}
static void host_drain(void){assert(wire_count+driver_count<sizeof(wire));memcpy(wire+wire_count,driver_tx,driver_count);wire_count+=driver_count;driver_count=0;}
static void drain(void){
 static const unsigned sizes[]={0,1,17,64,256};unsigned turns=0;
 while(bonsai_cdc_pending(&g_cdc_tx_queue)||driver_count){
  assert(turns<20000);host_drain();accept_limit=sizes[turns++%5];callback();
 }
 assert(!tx_enabled);
}
static void send(const char *line){assert(ring_buf_put(&g_cdc_rx,(const uint8_t*)line,strlen(line))==strlen(line));}
static void reset(void){
 bonsai_cdc_reset(&g_cdc_tx_queue);ring_buf_reset(&g_cdc_rx);
 atomic_store(&g_cdc_online,true);atomic_store(&g_cdc_configured,true);atomic_store(&g_cdc_reset_pending,false);
 dtr=1;g_cdc_rx_paused=false;tx_enabled=false;rx_enabled=true;driver_count=driver_rx_count=wire_count=fills=enables=0;
 g_xfer_mode=0;recording=false;commit_count=status_count=load_count=0;memset(&dd_ui,0,sizeof(dd_ui));
 ++g_cdc_session;dual_transfer(); /* clear production parser for this session */
}
static void test_complete_large_response(void){
 reset();uint8_t source[1800],next[1472];for(unsigned i=0;i<sizeof(source);i++)source[i]=(uint8_t)(i*29);
 memset(next,'z',sizeof(next));assert(cdc_tx(source,sizeof(source)));assert(cdc_tx(next,sizeof(next)));
 assert(fills==0&&bonsai_cdc_pending(&g_cdc_tx_queue)==3272); /* storage admission did no USB IO */
 uint8_t saved[1800];memcpy(saved,source,sizeof(source));memset(source,0,sizeof(source));
 drain();assert(wire_count==3272&&!memcmp(wire,saved,1800)&&!memcmp(wire+1800,next,1472));
 assert(enables==2&&fills<200);
 assert(cdc_tx((const uint8_t*)"fresh",5)&&tx_enabled);drain();assert(!memcmp(wire+3272,"fresh",5));
}
static void test_mirror_busy_preserves_edges(void){
 reset();uint8_t padding[3000];memset(padding,'x',sizeof(padding));assert(cdc_tx(padding,sizeof(padding)));
 send("DDMIR?\n");dual_transfer();assert(dd_ui_until==(uint32_t)now+1000);
 uint16_t values[13];for(unsigned j=0;j<13;j++)values[j]=j<5?256:1000;
 dd_ui.seq=4000000000u;
 for(unsigned i=0;i<16;i++){values[0]=(i&1)?0:16;values[5]=(i&1)?0:1000;assert(dd_ui_push(&dd_ui,(uint32_t)now+i*8,values,true));}
 for(unsigned i=0;i<3;i++){now+=800;dual_transfer();assert(dd_ui.count==16&&dd_ui.lost==0);}
 drain();assert(wire_count==3000);dual_transfer();assert(dd_ui.count==0&&dd_ui.lost==0);
 drain();assert(wire_count>4024);wire[wire_count]=0;
 const char *cursor=(char*)wire+3000;
 for(unsigned i=0;i<16;i++){
  char expected[40];snprintf(expected,sizeof(expected),"[%u,",4000000001u+i);
  cursor=strstr(cursor,expected);assert(cursor);cursor++;
 }
 assert(!strcmp((char*)wire+wire_count-3,"]}\n"));
}
static void test_binary_order_and_bounded_commands(void){
 reset();send("DDSTAT?\nDDSTAT?\nDDLIB?\n");dual_transfer();assert(status_count==1&&g_cdc_rx.written-g_cdc_rx.read==15);
 dual_transfer();assert(status_count==2);dual_transfer();drain();
 assert(wire_count==40&&!memcmp(wire,"{\"status\":1}\n{\"status\":1}\n{\"library\":1}\n",40));
 reset();uint8_t telemetry[1800];memset(telemetry,'t',sizeof(telemetry));assert(cdc_tx(telemetry,sizeof(telemetry)));
 send("SP1XFER!P");dual_transfer();assert(!g_xfer_mode&&g_cdc_rx.written-g_cdc_rx.read==1);
 /* The software barrier clears before host draining. Driver FIFO is still
  * ordered: following binary data cannot jump its previously accepted bytes. */
 while(bonsai_cdc_pending(&g_cdc_tx_queue)){accept_limit=256;callback();if(driver_count==1024)host_drain();}
 assert(driver_count>0);dual_transfer();assert(g_xfer_mode);dual_transfer();drain();
 assert(wire_count==1828&&!memcmp(wire,telemetry,1800)&&!memcmp(wire+1800,"SP1!",4));
 send("S");dual_transfer();drain();assert(status_count==1);
 send("X");dual_transfer();drain();assert(!g_xfer_mode&&commit_count==1&&wire[wire_count-1]=='x');
}
static void test_first_connection_keeps_request(void){
 reset();atomic_store(&g_cdc_online,false);atomic_store(&g_cdc_configured,false);
 struct usbd_msg msg={.type=USBD_MSG_RESET};cdc_usb_event(NULL,&msg);
 msg.type=USBD_MSG_CONFIGURATION;msg.status=1;cdc_usb_event(NULL,&msg);
 memcpy(driver_rx,"DDSTAT?\n",8);driver_rx_count=8;
 callback(); /* host writes before the storage owner processes enumeration */
 dual_transfer();callback();dual_transfer();drain();
 assert(status_count==1&&wire_count==13&&!memcmp(wire,"{\"status\":1}\n",13));
}
static void reconnect_during_payload(void){
 dtr=0;struct usbd_msg msg={.type=USBD_MSG_CDC_ACM_CONTROL_LINE_STATE,.dev=cdc};cdc_usb_event(NULL,&msg);
 dtr=1;cdc_usb_event(NULL,&msg);
 memcpy(driver_rx,"DDSTAT?\n",8);driver_rx_count=8;callback();
}
static void test_interrupted_payload_does_not_reply_to_new_session(void){
 reset();send("SP1XFER!");dual_transfer();assert(g_xfer_mode);
 send("W");sleep_hook=reconnect_during_payload;dual_transfer();
 callback();dual_transfer();drain();
 assert(!g_xfer_mode&&commit_count==1&&status_count==1);
 assert(wire_count==13&&!memcmp(wire,"{\"status\":1}\n",13));
}
static void test_disconnect_and_rx_backpressure(void){
 reset();assert(cdc_tx((const uint8_t*)"old response",12));
 struct usbd_msg msg={.type=USBD_MSG_VBUS_REMOVED};cdc_usb_event(NULL,&msg);
 callback();assert(!driver_count&&!tx_enabled);assert(!cdc_session_sync());assert(!bonsai_cdc_pending(&g_cdc_tx_queue));
 msg.type=USBD_MSG_CONFIGURATION;msg.status=1;cdc_usb_event(NULL,&msg);assert(cdc_session_sync());dual_transfer();
 assert(cdc_tx((const uint8_t*)"new response",12));drain();assert(wire_count==12&&!memcmp(wire,"new response",12));
 uint8_t padding[1024]={0};assert(ring_buf_put(&g_cdc_rx,padding,1024)==1024);
 memset(driver_rx,0x5a,64);driver_rx_count=64;callback();assert(g_cdc_rx_paused&&!rx_enabled&&driver_rx_count==64);
 assert(ring_buf_get(&g_cdc_rx,padding,64)==64);cdc_rx_resume();assert(rx_enabled);callback();
 assert(driver_rx_count==0&&g_cdc_rx.written-g_cdc_rx.read==1024);
}
int main(int argc,char **argv){
 assert(argc==2);
 if(!strcmp(argv[1],"large"))test_complete_large_response();
 else if(!strcmp(argv[1],"mirror"))test_mirror_busy_preserves_edges();
 else if(!strcmp(argv[1],"binary"))test_binary_order_and_bounded_commands();
 else if(!strcmp(argv[1],"payload"))test_interrupted_payload_does_not_reply_to_new_session();
 else if(!strcmp(argv[1],"connect"))test_first_connection_keeps_request();
 else if(!strcmp(argv[1],"disconnect"))test_disconnect_and_rx_backpressure();
 else return 2;
 puts("actual CDC adapter replay passed");return 0;
}
'''

class CdcAdapterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        main = (ROOT/'firmware/src/main.c').read_text()
        adapter = (ROOT/'firmware/src/dual_firmware.inc').read_text()
        start = main.index('#ifdef SP1_DUAL_DECK\n/* Whole replies')
        end = main.index('/* A block command', start)
        source = HARNESS + main[start:end] + function(main, 'xfer_resync') + ADAPTER_STUBS
        source += '\n' + '\n'.join(function(adapter, name) for name in
                                   ('dual_mirror_listen', 'dual_mirror_reply', 'dual_transfer')) + TESTS
        src = Path(cls.tmp.name)/'adapter.c'
        src.write_text(source)
        cls.exe = Path(cls.tmp.name)/'adapter'
        subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                        '-fsanitize=address,undefined','-I',str(ROOT/'firmware/src'),
                        str(src),str(ROOT/'firmware/src/bonsai_cdc.c'),
                        str(ROOT/'firmware/src/dual_mirror.c'),'-o',str(cls.exe)],check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def replay(self, case):
        result = subprocess.run([str(self.exe),case],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr+result.stdout)

    def test_large_reply_partial_acceptance_and_new_enable(self): self.replay('large')
    def test_busy_mirror_retains_every_button_and_led_edge(self): self.replay('mirror')
    def test_binary_barrier_ordering_and_bounded_parser(self): self.replay('binary')
    def test_payload_disconnect_cannot_reply_in_new_session(self): self.replay('payload')
    def test_first_connection_keeps_early_request(self): self.replay('connect')
    def test_disconnect_and_rx_backpressure(self): self.replay('disconnect')
