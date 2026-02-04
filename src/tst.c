// This does not test QNICLL or QREGD.
// rather, it tests libiio locally.


#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include <errno.h>
#include <iio.h>
#include "qregs.h"
#include "qregs_ll.h"
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "corr.h"
#include <time.h>
#include "ini.h"
#include "util.h"
#include "h_vhdl_extract.h"
#include "h.h"
#include "hdl.h"
//#include "tsd.h"

#define QNICLL_LINKED (0)
#define QREGC_LINKED (0)

#if QNICLL_LINKED
#include <qnicll.h>
  int use_qnicll=0;
  void crash(int e, char *str) {
// desc: crashes wih informative messages
    printf("ERR: %d\n", e);
    printf("     %s\n", qnicll_error_desc());
    if (errno)
      printf("     errno %d = %s\n", errno, strerror(errno));  
    exit(1);
  }

#endif

#if QREGC_LINKED
#include "qregc.h"
int use_qregc=0;
#endif

char errmsg[512];
void err(char *str) {
  printf("ERR: %s\n", str);
  printf("     errno %d\n", errno);
  exit(1);
}

#define NSAMP (1024)

int dbg_lvl=0;

#define NEW (1)



// check and crash if error
#define C(CALL)     {int e = CALL; if (e) crash(e, "");}



int chan_en(struct iio_channel *ch) {
  iio_channel_enable(ch);
  if (!iio_channel_is_enabled(ch))
    // TODO retun err code notjust fail
    err("chan not enabled");
  return 0;
}

void set_blocking_mode(struct iio_buffer *buf, int en) {
  int i;
  i=iio_buffer_set_blocking_mode(buf, en); // default is blocking.
  if (i) err("cant set blocking");
}


#define DAC_N (4095*2)
#define SYMLEN (4)
#define PAT_LEN (256/SYMLEN)
// #define ADC_N (1024*16*16*8*4)
//#define ADC_N (1024*16*16*8*4)
// #define ADC_N 4144000

#define ADC_N (2<<18)
#define MAX_RXBUF_SZ_BYTES (ADC_N * 4)

//  #define ADC_N (1024*16*32)


ini_val_t *tvars, *vars_cfg_all;

int opt_dflt=0;
int opt_sync=0;
int opt_qsdc=0;
char mode=0;
char *data_fname=0;
int opt_meas_noise=0, noise_dith=0;
int stream=0;

hdl_qsdc_cfg_t qsdc_cfg={0};


void lookup_int(char *var_name, int *i_p) {
  int e=ini_get_int(tvars, var_name, i_p);
  if (e)
    printf("WARN: %s undefined in tvars\n", var_name);
}



int ask_yn(char *prompt, char *var_name, int dflt) {
  return ini_ask_yn(tvars, prompt, var_name, dflt);
}
char *ask_str(char *prompt, char *var_name, char *dflt) {
  return ini_ask_str(tvars, prompt, var_name, dflt);  
}
double ask_num(char *prompt, char *var_name, double dflt) {
  return ini_ask_num(tvars, prompt, var_name, dflt);
}
double ask_nnum(char *var_name, double dflt) {
  return ask_num(var_name, var_name, dflt);
}










ssize_t read_file_into_buf(char *fname, void *buf, ssize_t buf_sz) {
  int i, fd = open(fname,O_RDWR);
  short int v;
  ssize_t rd_sz;
  if (fd<0) {
    snprintf(errmsg, 512, "read_file_into_buf() cant open file %s", fname);
    err(errmsg);
    return 0;
  }
  rd_sz = read(fd, buf, DAC_N);
  printf("  read %zd bytes from %s\n", rd_sz, fname);
  /*
  for(i=0;i<16;++i) {
    v=((char *)buf)[i];
    printf("%02x\n", (int)v&0xff);
   }
   printf("\n");
  */
  return rd_sz;
}

void prompt(char *prompt) {
  char buf[256];
  if (prompt && prompt[0])
    printf("%s > ", prompt);
  else
    printf("hit enter > ");
  scanf("%[^\n]", buf);
  getchar();
}


int min(int a, int b) {
  if (a<b) return a;
  return b;
}


double sq(double a) {
  return a*a;
}


// qregc level code calls this
static int remote_err_handler(char *str, int err) {
  printf("REMOTE ERR: %s\n", str);
  //  printf("BUG: st.errmsg_ll %s\n", st.errmsg_ll);
  return err;
}



int opt_save=1, opt_corr=0;
int save_fd;
int alice_syncing=0, alice_txing=0;


int num_iio_itr=1, iio_dly_ms=0;
int frames_per_iiobuf;
int rx_frame_qty;
int tx_frame_qty;
int search=0;
int is_alice=0;
int cdm_en=0;


// IIO state
// This is not always used.  The qregd does not always do
// local IIO accesses.  But it can.
typedef struct lcl_iio_struct {
  int    open; // 0 or 1

  struct iio_context *ctx;
  struct iio_device *dac, *adc;
  struct iio_channel *dac_ch0, *dac_ch1, *adc_ch0, *adc_ch1;
  struct iio_buffer *dac_buf, *adc_buf;
  void *adc_buf_p;

  ssize_t tx_buf_sz_bytes;
  ssize_t rx_buf_sz_bytes;
  int rx_num_bufs;

  int num_iter;
  
} lcl_iio_t;


lcl_iio_t lcl_iio={0};


void lcl_iio_create_dac_bufs(int sz_bytes) {
  size_t sz, dac_buf_sz;
  
  if (lcl_iio.tx_buf_sz_bytes)
    err("dac bufs already created");
  // prompt("will create dac buf");
  sz = iio_device_get_sample_size(lcl_iio.dac);
  // libiio sample size is 2 * number of enabled channels.
  dac_buf_sz = sz_bytes / sz;
  // NOTE: oddly, this create buffer seems to cause the dac to output
  //       a GHz sin wave for about 450us.
  lcl_iio.dac_buf = iio_device_create_buffer(lcl_iio.dac, dac_buf_sz, false);
  if (!lcl_iio.dac_buf) {
    sprintf(errmsg, "cant create dac bufer  nsamp %zu", dac_buf_sz);
    err(errmsg);
  }
  lcl_iio.tx_buf_sz_bytes = sz_bytes;
}


int lcl_iio_open() {
  lcl_iio_t *p=&lcl_iio;
  ssize_t sz, buf_sz;
  void *rval;
  int e;

  if (p->open)
    err("BUG: iio already open");

  
  p->ctx = iio_create_local_context();
  if (!p->ctx)
    err("cannot get context");
  e=iio_context_set_timeout(p->ctx, 3000); // in ms
  if (e) err("cant set timo");
  p->dac = iio_context_find_device(p->ctx, "axi-ad9152-hpc");
  if (!p->dac)
    err("cannot find dac");
  p->adc = iio_context_find_device(p->ctx, "axi-ad9680-hpc");
  if (!p->adc)
    err("cannot find adc");
  //ch = iio_device_get_channel(dev, 0);
  // channels voltage0 and voltage1 of dac are "scan elements"
  p->dac_ch0 = iio_device_find_channel(p->dac, "voltage0", true); // by name or id
  if (!p->dac_ch0)
    err("dac lacks channel");
  p->dac_ch1 = iio_device_find_channel(p->dac, "voltage1", true); // by name or id
  if (!p->dac_ch1)
    err("dac lacks channel");
  e=chan_en(p->dac_ch0);
  if (e) return e;
  e=chan_en(p->dac_ch1);
  if (e) return e;
  
  //  iio_channel_disable(dac_ch1);  
  p->adc_ch0 = iio_device_get_channel(p->adc, 0);
  if (!p->adc_ch0)  err("adc lacks ch 0");
  p->adc_ch1 = iio_device_get_channel(p->adc, 1);
  if (!p->adc_ch1)  err("adc lacks ch 1");
  // must enable channel befor creating buffer
  // sample size is 2 * number of enabled channels
  e=chan_en(p->adc_ch0);
  if (e) return e;
  e=chan_en(p->adc_ch1);
  if (e) return e;
  //  sz = iio_device_get_sample_size(adc);
  //  printf("adc samp size %zd\n", sz); // is 4 when 2 chans en
}

// measurements stored here
int *times_s=0;
double *corr=0;

int first_action(void) {
  int num_dev, i, j, k, n, e;
  char name[32], attr[32], c;
  int pat[PAT_LEN] = {1,1,1,1,0,0,0,0,1,0,1,0,0,1,0,1,
       1,0,1,0,1,1,0,0,1,0,1,0,0,1,0,1,
       1,0,1,0,1,1,0,0,1,0,1,0,0,1,0,1,
       0,1,0,1,0,0,1,1,0,1,0,1,1,0,1,0};
  ssize_t sz, tx_sz, sz_rx;
  const char *r;
  ssize_t left_sz, mem_sz;
  double x, y;

  // cat iio:device3/scan_elements/out_voltage0_type contains
  // le:s15/16>>0 so I think it's short int.  By default 1233333333 Hz
  short int mem[DAC_N];
  //  short int rx_mem_i[ADC_N], rx_mem_q[ADC_N]; 
  //  short int corr[ADC_N];


  int p_i;
  double d;
  long long int ll;

  int use_lfsr=1;
  //  int  hdr_preemph_en=0;

  int tx_always=0;
  int tx_0=0;  
  int max_frames_per_buf;

  
  //  meas_noise = ask_yn("meas_noise", "meas_noise", 0);
  if (mode=='n') {
    tx_0=1;
    mem_sz=0;
    tx_always=0;
  }else {
    tx_0 = (int)ini_ask_yn(tvars,"tx nothing", "tx_0", 0);
    //    use_lfsr = (int)ini_ask_num(tvars, "use lfsr", "use_lfsr", 1);
    // qregs_set_lfsr_rst_st(0x50f);
    //tx_always = ini_ask_yn(tvars, "tx_always", "tx_always", 0);
  }

  qregs_set_meas_noise(noise_dith);

  e=ini_get_int(vars_cfg_all, "use_lfsr", &i);  
  use_lfsr = !tx_0 && (e||i);
  qregs_set_use_lfsr(use_lfsr);


  qregs_set_save_after_init(0);
  qregs_set_save_after_pwr(0);
  qregs_set_save_after_hdr(0);


  //  qregs_set_tx_always(0); // set for real further down.
  qregs_search_en(0); // recover from prior crash if we need to.
  qregs_txrx_new(0,0);  
  qregs_set_tx_pilot_pm_en(!tx_0);
  qregs_set_alice_txing(0);
  qregs_get_avgpwr(&i,&j,&k); // just to clr ADC dbg ctrs


 
  if (is_alice) {
    qregs_sync_status_t sstat;
    qregs_get_sync_status(&sstat);
    if (!sstat.locked)
      err("synchronizer unlocked");
  }

  qregs_set_tx_same_hdrs(!cdm_en);


  if (is_alice) {

    qregs_set_save_after_hdr(1);
    // I tried this 7:12pm to try to prevent the timo
    // no change.
    //    qregs_set_save_after_dmareq(1);

      
#if QNICLL_LINKED
    use_qnicll=ini_ask_yn(tvars, "use qnicll", "use_qnicll",0);
    if (use_qnicll) {
      char *h = ask_str("remote host", "remote_host", "");
      qnicll_init_info_libiio_t libiio_init;
      //	strcpy(libiio_init.ipaddr,"10.0.0.5");
      strcpy(libiio_init.ipaddr, h);
      C(qnicll_init(&libiio_init));
      C(qnicll_set_mode(QNICLL_MODE_QSDC));
    }
#endif


#if QREGC_LINKED
    use_qregc=ini_ask_yn(tvars, "use qregc","use_qregc",0);
    if (use_qregc) {
      qregc_connect("10.0.0.5", &remote_err_handler);
      qregc_iioopen();
    }
#endif
    qregs_set_alice_syncing(alice_syncing);
      
  }else { // not alice
    qregs_set_alice_syncing(0);
  }

  
  // qregs_set_sync_ref('p'); // ignored if bob
  //  qregs_sync_resync();

  
  char cond;
  if (is_alice)
    if (!alice_txing && !alice_syncing)
      cond='r';
    else
      cond='h';
  else
    cond ='r'; // r=tx when rxbuf rdy
  qregs_set_tx_go_condition(cond);
  printf(" using tx_go condition %c\n", st.tx_go_condition);



  if (search) {
    i=j=k=0;
    lookup_int("init_pwr_thresh", &i);
    qregs_dbg_set_init_pwr(i);
    // printf("  init_pwr_thresh %d\n", st.init_pwr_thresh);
    lookup_int("hdr_pwr_thresh", &j);
    lookup_int("hdr_corr_thresh",&k);
    qregs_set_hdr_det_thresh(j, k);
    //  printf("  pilot_pwr_thresh %d\n", st.hdr_pwr_thresh);
    // printf("  pilot_corr_thresh %d\n", st.hdr_corr_thresh);
  }

  
   
  qregs_set_frame_qty(tx_frame_qty);
  if (st.frame_qty !=tx_frame_qty) {
    printf("ERR: actually tx frame qty %d not %d\n", st.frame_qty, tx_frame_qty);
  }



  
  //  sz = iio_device_get_sample_size(dac);
  // DAC sample size is 2 bytes per channel.  if 2 chans enabled, sz is 4.
  //  printf("dac samp size %zd\n", sz);




  // in half-duplex FPGA, alice can't store IM in mem

  //  st.pilot_cfg.im_from_mem = hdr_preemph_en;
  //  qregs_cfg_pilot(&st.pilot_cfg, 0);



  // IIO provides iio_channel_convert_inverse(dac_ch0,  dst, mem);
  // but for dac3 zcu106 basically there is no conversion

  // sys/module/industrialio_buffer_dma/paraneters/max_bloxk_size is 16777216
  // iio_device_set_kernel_buffers_count(?
  //  i = iio_device_get_kernel_buffers_count(dev);
  //  printf("num k bufs %d\n", i);
  


  ini_write("tvars.txt", tvars);

  
  if (opt_save) {
    save_fd = open("out/d.raw", O_CREAT | O_WRONLY | O_TRUNC, S_IRWXO);
    if (save_fd<0) err("cant open d.txt");
  }

  printf("mem sz %zd  %d %d\n", mem_sz, alice_txing, is_alice);

  
  memset(mem, 0, sizeof(mem));
  mem_sz=0;
  if (!mem_sz && is_alice && alice_txing) {
    if (0)  {
      printf("writing x0a55... to mem\n");
      mem_sz = (int)((DAC_N/2)/32*32);
      // size of channel is DAC_N*2*2 bytes
      for (i=0; i<mem_sz/2; ++i)
	mem[i]=0xaa55;
    }else {
      if (!data_fname) {
        data_fname=ini_ask_str(tvars, "data file",
			       "data_file","src/data.bin");
        ini_save(tvars);
      }
      mem_sz=read_file_into_buf(data_fname, mem, sizeof(mem));
    }
    int data_len_syms = (int)mem_sz * 8 / (st.qsdc_data_cfg.is_qpsk?2:1) *
      st.qsdc_data_cfg.bit_dur_syms;
    printf("total data len %d symbols", data_len_syms);
    int data_len_frames = (int)ceil((double)data_len_syms
				    * st.qsdc_data_cfg.symbol_len_asamps
				    / st.qsdc_data_cfg.data_len_asamps);
    printf("   =  %d frames\n", data_len_frames);
  }
  if (mem_sz>0) {

    if (0) {
    ssize_t dac_buf_sz;
    // prompt("will create dac buf");  
    sz = iio_device_get_sample_size(lcl_iio.dac);
    // libiio sample size is 2 * number of enabled channels.
    dac_buf_sz = mem_sz / sz;
    // NOTE: oddly, this create buffer seems to cause the dac to output
    //       a GHz sin wave for about 450us.
    lcl_iio.dac_buf = iio_device_create_buffer(lcl_iio.dac, dac_buf_sz, false);
    if (!lcl_iio.dac_buf) {
      sprintf(errmsg, "cant create dac bufer  nsamp %zu", dac_buf_sz);
      err(errmsg);
    }
    }
    
    // printf("DBG: per-chan step %zd\n", iio_buffer_step(dac_buf));  
    void *p;
    p = iio_buffer_start(lcl_iio.dac_buf);
    if (!p) err("no buffer yet");
    memcpy(p, mem, mem_sz);
    // sz = iio_channel_write(dac_ch0, dac_buf, mem, mem_sz);
    // returned 256=DAC_N*2, makes sense
    printf("  filled dac_buf sz %zd bytes\n", mem_sz);
  }


  //  qregs_set_tx_always(tx_always);


  if (num_iio_itr) {
    if (times_s) free(times_s);
    times_s = (int *)malloc(sizeof(int)*num_iio_itr);
  }
  if (opt_corr) {
    sz = sizeof(double) * st.frame_pd_asamps;
    printf("frame_pd_asamps %d\n", st.frame_pd_asamps);
      
    // printf("will init size %zd  dbg %zd\n", sz, sizeof(double));
    if (corr) free(corr);
    corr = (double *)malloc (sz);
    if (!corr) err("cant malloc");
    memset((void *)corr, 0, sz);
    corr_init(st.hdr_len_bits, st.frame_pd_asamps);
  }


  // DONE: make cipher prime NOT be dependent on cipher_en
  // going low.  Then we can just keep it high all the time.

  qregs_qsdc_other_cfg_t oth={0};
  lookup_int("cipher_en",   &oth.cipher_en);
  lookup_int("decipher_en", &oth.decipher_en);
  if (!is_alice && (mode=='q')) {
    printf("cipher_en %d  \t decipher_en %d\n",
	   oth.cipher_en, oth.decipher_en);
  }
  // for now:
  //    oth.stream = ask_yn("stream qsdc", "qsdc_stream", 0);
  oth.m=2;
  oth.dbg_cipher_same=0;
  oth.cipher_symlen_asamps = st.osamp;
  //  qsdc_cfg.is_bob = !is_alice;
  qregs_qsdc_other_cfg(&oth);
  

  // sinusoid before willpush    
  //    prompt("will push");
  if (mem_sz) {
    size_t data_sz_samps;

    qregs_zero_mem_raddr();

    set_blocking_mode(lcl_iio.dac_buf, true); // default is blocking.

    sz = iio_device_get_sample_size(lcl_iio.dac);
    data_sz_samps = (int)(mem_sz/sz);
    tx_sz = iio_buffer_push_partial(lcl_iio.dac_buf, data_sz_samps); // supposed to ret num bytes
    printf("  pushed %zd bytes\n", tx_sz);

    i = mem_sz/8-2;
    h_w_fld(H_DAC_DMA_MEM_RADDR_LIM_MIN1, i);
    qregs_dbg_get_info(&j);
    printf("  DBG: set raddr lim %d (dbg %d)\n", i, j);

  }

  // this must be done after pushing the dma data, because it primes qsdc.
  qregs_set_alice_txing(is_alice && alice_txing);


  if (!is_alice) {
    qregs_set_sync_ref('t');
    qregs_qsdc_track_pilots(0);
    //    qregs_qsdc_track_pilots(1);  // not ready to do this yet.
    if (mode!='n') {
      printf("new thing, bob resync for syncref t\n");
      qregs_sync_resync();
    }
  }
   

  //  qregs_set_cdm_en(cdm_en); // now done in qregs_cfg_cdm.
  

  qregs_clr_adc_status();
  qregs_clr_tx_status();
  qregs_clr_corr_status();
}


int second_action(void) {
  int itr, b_i, p_i, e, i;
  int t0_s;
  void *adc_buf_p;
  ssize_t rx_buf_sz_asamps, sz;
  ssize_t left_sz;    
  int refill_err=0;
  // qregs_print_adc_status();	  

  // for cdm:
  // should call tsd_lcl_cdm_go
  if (cdm_en) {
    h_w_fld(H_DAC_HDR_SECOND_IM_IS_PROBE, 1);
    qregs_set_tx_same_hdrs(0);
  }
  
  t0_s = time(0);

  //  if (!is_alice) {  
  //    printf("BUG WORKAROUND\n");
  //    h_w_fld(H_ADC_CTL2_EXT_FRAME_PD_MIN1_CYCS, 0);
  //  }
  
  for (itr=0; !num_iio_itr || (itr<num_iio_itr); ++itr) {

    if (num_iio_itr>1)
      printf("itr %d: time %ld (s)\n", itr, time(0)-t0_s);
    if (times_s && num_iio_itr)
      *(times_s + itr) = (int)time(0);

    if (is_alice) {
      // actually I think this could go before or after
      // iiodev create buffer.
      printf("a txrx\n");

      // not using search
      // qregs_search_and_txrx(0, 1);
      //      qregs_txrx_new(1,alice_rxing);
      qregs_txrx_new(1,1);

      // 7:20pm just trying this
      //      if (!alice_rxing)
      //	lcl_iio.rx_num_bufs=0;
      // if (!alice_rxing) exit(0);
      
    }
    //    else { // IS BOB
    //          qregs_search_en(search);
    //          qregs_txrx(1);
    //       }

    // This allocs ADC bufs and starts RX DMA
    sz = iio_device_get_sample_size(lcl_iio.adc);  // sz=4;
    rx_buf_sz_asamps = lcl_iio.rx_buf_sz_bytes / sz;
    printf("create rx bufs sz %zd bytes\n", lcl_iio.rx_buf_sz_bytes);
    lcl_iio.adc_buf = iio_device_create_buffer(lcl_iio.adc,
					       rx_buf_sz_asamps,
					       false);

    if (!lcl_iio.adc_buf)
      err("cant make adc buffer");
      // printf("made adc buf size %zd asamps\n", (ssize_t)buf_len_asamps);
      // creating ADC buffer commences DMA in hdl


    if (!is_alice) {

      // can go after createbuffer.
      // could it go before?
      qregs_search_en(search);

      // cant do this till I change hdl quanet_adc line 1643
      //      qregs_txrx_new((mode!='n'),1);
      qregs_txrx_new(1,1);

      // prompt("READY? ");
      // printf("after txrx then create\n");	
      // qregs_print_settings();
      //qregs_print_adc_status();	      
    }
    for(b_i=0; b_i<lcl_iio.rx_num_bufs; ++b_i) {
      void *p;
	
      sz = iio_buffer_refill(lcl_iio.adc_buf);
      // printf("refilled %zd\n", sz);
      
      //      qregs_print_adc_status();
      if (sz<0) {
	iio_strerror(-sz, errmsg, 512);
	printf("\nIIO_BUFFER_REFILL ERR: %s\n", errmsg);
	qregs_print_hdr_det_status(); // this prints it all
	sprintf(errmsg, "cant refill rx bufer %d", b_i);
	refill_err=1;
	err(errmsg);
      }else
        printf("refilled buf\n");
      
      if (sz != lcl_iio.rx_buf_sz_bytes)
	printf("tried to rx %ld but got %ld bytes\n",
	       lcl_iio.rx_buf_sz_bytes, sz);

      // pushes double the dac_buf size.
      //qregs_print_adc_status();

	
      // iio_buffer_start can return a non-zero ptr after a refill.
      adc_buf_p = iio_buffer_start(lcl_iio.adc_buf);
      if (!adc_buf_p) err("iio_buffer_start returned 0");
      p = iio_buffer_end(lcl_iio.adc_buf);
      // printf(" size %zd\n", p - adc_buf_p);

      
      if (opt_corr && !cdm_en) {  // If correlating in C
	for(p_i=0; p_i<frames_per_iiobuf; ++p_i) {
	  //	  printf("p %d\n",p_i);
	  p = adc_buf_p + sizeof(short int)*2*p_i*st.frame_pd_asamps;
	  // printf("offset %zd\n",p - adc_buf_p);
	  corr_accum(corr, adc_buf_p + sizeof(short int)*2*p_i*st.frame_pd_asamps);
	}
      }


      if (opt_save && save_fd) {
	left_sz = sz;
	while(left_sz>0) {
	  sz = write(save_fd, adc_buf_p, left_sz);
	  if (sz<=0) err("write failed");
	  if (sz == left_sz) break;
	  printf("tried to write %zd but wrote %zd\n", left_sz, sz);
	  left_sz -= sz;
	  adc_buf_p = (void *)((char *)adc_buf_p + sz);
	}
      }
	
      

    } // for b_i
    
      // qregs_print_adc_status();
    
      // qregs_print_sync_status();
      
    if (search) {
      // prompt("OK");
      //      qregs_print_hdr_det_status();
      //      prompt("DATA SAVED");
      usleep(1000*200);
      qregs_print_hdr_det_status();
      usleep(1000*200);
      qregs_search_en(0);
    }
    qregs_txrx_new(0,0);



#if QNICLL_LINKED
    if (is_alice && use_qnicll) {
      C(qnicll_bob_sync_go(0));
      C(qnicll_done());
    }
#endif    
    
    if (opt_corr && !cdm_en) {
      corr_find_peaks(corr, rx_frame_qty);
    }
    
    iio_buffer_destroy(lcl_iio.adc_buf);
    usleep(iio_dly_ms * 1000);
   
    
  } // for iio itr

    
  //    printf("out of itr loop %d\n",  use_qnicll);
  qregs_set_alice_txing(0);


  int tx_always = st.tx_always;
  qregs_frame_pwrs_t pwrs={0};
  if (!refill_err && !is_alice && alice_txing) {
    qregs_set_tx_always(1);
    usleep(1000);
    e= qregs_measure_frame_pwrs(&pwrs);
    if (e)
      printf("err: no rsp from RP!\n");
    else {
      printf("  hdr/body  %.1f dB\n", pwrs.ext_rat_dB);
      printf("  body/mean %.1f dB\n", pwrs.body_rat_dB);
    }
    qregs_set_tx_always(tx_always);
  }


  usleep(1); // AD fifo funny thing workaround
  qregs_set_cdm_en(0);



  /*
  for(j=i-10; j<i; ++j)
    printf("\t%d", rx_mem[j]);
  printf("\n");
  for(j=0; (i<ADC_N)&&(j<16); ++i,++j)
    printf("\t%d", rx_mem[i]);
  printf("\n");
  */
  if (opt_save) {
    FILE *fp;
    char hostname[32];
    close(save_fd);
    gethostname(hostname, sizeof(hostname));
    hostname[31]=0;
    fp = fopen("out/r.txt","w");
    //  fprintf(fp,"sfp_attn_dB = %d;\n",   sfp_attn_dB);
    fprintf(fp,"host = '%s';\n",       hostname);
    fprintf(fp,"tst_sync = %d;\n",     1);
    fprintf(fp,"asamp_Hz = %lg;\n",    st.asamp_Hz);
    fprintf(fp,"use_lfsr = %d;\n",     st.use_lfsr);
    fprintf(fp,"lfsr_rst_st = '%x';\n", st.lfsr_rst_st);
    fprintf(fp,"meas_noise = %d;\n",   (mode=='n'));
    fprintf(fp,"cdm_en = %d;\n",       cdm_en);
    fprintf(fp,"cdm_num_iter = %d;\n",  st.cdm_cfg.num_iter);
    fprintf(fp,"noise_dith = %d;\n",   noise_dith);
    fprintf(fp,"tx_always = %d;\n",    st.tx_always);
    // printf("phase est %d\n", st.phase_est_en);
    fprintf(fp,"phase_est_en = %d;\n",    st.phase_est_en);
    fprintf(fp,"tx_hdr_twopi = %d;\n", st.tx_hdr_twopi);
    fprintf(fp,"tx_mem_circ = %d;\n",  st.tx_mem_circ);
    fprintf(fp,"tx_same_cipher = %d;\n", st.tx_same_cipher);
    fprintf(fp,"is_alice = %d;\n",    is_alice);
    if (is_alice) 
      fprintf(fp,"rx_same_hdrs = 1;\n");
    else
      fprintf(fp,"rx_same_hdrs = %d;\n", st.tx_same_hdrs);
    fprintf(fp,"alice_syncing = %d;\n", alice_syncing);
    fprintf(fp,"alice_txing = %d;\n", alice_txing);
    fprintf(fp,"search = %d;\n",       search);
    fprintf(fp,"stream = %d;\n",       stream);
    fprintf(fp,"osamp = %d;\n",        st.osamp);
    fprintf(fp,"cipher_m = %d;\n",     st.cipher_m);
    fprintf(fp,"cipher_en = %d;\n",      st.cipher_en);
    fprintf(fp,"decipher_en = %d;\n",    st.decipher_en);
    fprintf(fp,"cipher_symlen_asamps = %d;\n", st.cipher_symlen_asamps);
    fprintf(fp,"tx_pilot_pm_en = %d;\n",  st.tx_pilot_pm_en);
    fprintf(fp,"frame_qty = %d;\n",    st.frame_qty);
    fprintf(fp,"frame_pd_asamps = %d;\n", st.frame_pd_asamps);
    fprintf(fp,"round_trip_asamps = %d;\n", st.round_trip_asamps);

    fprintf(fp,"init_pwr_thresh = %d;\n", st.init_pwr_thresh);
    fprintf(fp,"hdr_pwr_thresh = %d;\n", st.hdr_pwr_thresh);
    fprintf(fp,"hdr_corr_thresh = %d;\n", st.hdr_corr_thresh);
    fprintf(fp,"sync_dly_asamps = %d;\n", st.sync_dly_asamps);

    fprintf(fp,"qsdc_data_is_qpsk = %d;\n", st.qsdc_data_cfg.is_qpsk);
    fprintf(fp,"qsdc_data_pos_asamps = %d;\n", st.qsdc_data_cfg.pos_asamps);
    fprintf(fp,"qsdc_data_len_asamps = %d;\n", st.qsdc_data_cfg.data_len_asamps);
    fprintf(fp,"qsdc_symbol_len_asamps = %d;\n", st.qsdc_data_cfg.symbol_len_asamps);
    fprintf(fp,"qsdc_bit_dur_syms = %d;\n", st.qsdc_data_cfg.bit_dur_syms);
    fprintf(fp,"m11 = %g;\n", st.rebal.m11);
    fprintf(fp,"m12 = %g;\n", st.rebal.m12);
    fprintf(fp,"hdr_len_bits = %d;\n", st.hdr_len_bits);
    fprintf(fp,"data_hdr = 'i_adc q_adc';\n");
    //    fprintf(fp,"data_len_samps = %d;\n", cap_len_samps);
    fprintf(fp,"data_in_other_file = 2;\n");
    fprintf(fp,"num_iio_itr = %d;\n", num_iio_itr);
    fprintf(fp,"time = %d;\n", (int)time(0));
    fprintf(fp,"ext_rat_dB = %.1f;\n", pwrs.ext_rat_dB);
    fprintf(fp,"body_rat_dB = %.1f;\n", pwrs.body_rat_dB);
    fprintf(fp,"itr_times = [");
    if (num_iio_itr) {
      for(i=0;i<num_iio_itr;++i)
	fprintf(fp," %d", *(times_s+i)-*(times_s));
    }
    fprintf(fp, "];\n");
    fclose(fp);

    printf("wrote out/r.txt and p.raw\n");
  }



  
  //  fp = fopen("dg.txt","w");
  //  for(i=0; i<ADC_N; ++i) {
  //    fprintf(fp, "%g %d\n", (double)i/1233333333*1e9, rx_mem[i]);
  //  }
  //  fclose(fp);

  return 0;
  
  // correlation
  /*  
  {
    double s;
    for(i=0;i<ADC_N-DAC_N;++i) {
      s=0;
      for(j=0;j<DAC_N;++j) {
	s += mem[j]*rx_mem[i+j];
	corr[i]=s;
      }
    }
    fp = fopen("c.txt","w");
    for(i=0; i<ADC_N-DAC_N; ++i)
      fprintf(fp, "%g %d\n", (double)i/1233333333*1e9, corr[i]);
    fclose(fp);
  }
  
  fp = fopen("d.p","w");
  fprintf(fp,"set title 'samples';\n");
  fprintf(fp,"set xlabel 'time (ns)';\n");
  fprintf(fp,"set ylabel 'amplitude (adc)';\n");
  fprintf(fp,"plot 'dg.txt' with points pointtype 7;\n");
  fprintf(fp,"set label 1 '%.3f' at %.3f,%.3f point pointtype 7 lc rgb 'red';\n",x,x,y);
  fclose(fp);
  */  
  
  //   iio_context_destroy(ctx);
  return 0;
}





  // TODO: make a "cap" option that uses tx_0.
  // could be used for IQ imbal calib.

int main(int argc, char *argv[]) {
  int i, j, e;
  char c;
  double d;
  int opt_periter=1, opt_ask_iter=0, opt_go=0;

  mode=0;
  for(i=1;i<argc;++i) {
    if (mode=='q') {
      // mode has been set.next might be fname
      data_fname=argv[i];
    }else {
      for(j=0; (c=argv[i][j]); ++j) {
	//      if (c=='c') opt_corr=1;
	if (c=='d') opt_dflt=1;
	else if (c=='s') {opt_dflt=1; mode='s';}
	else if (c=='q') {opt_dflt=1; mode='q';}
	else if (c=='c') {opt_dflt=1; mode='c';}
	else if (c=='n') {opt_dflt=1; mode='n';}
	else if (c=='g') opt_go=1;
	else if (c=='v') opt_save=0;
	else if (c=='i') { opt_ask_iter=1; opt_periter=1;}
	else if (c=='a') opt_periter=0;
	else if (c!='-') {
	  printf("USAGES:\n  tst c  = compute correlations\n  tst n  = dont save to file\n  tst a = auto buf size calc\n  tst i = ask num iters\n tst d =use all defaults");
	return 1;}
      }
    }
  }
  ini_opt_dflt = opt_dflt;



  e = ini_read("tvars.txt", &tvars);
  if (e)
    printf("err reading tvars.txt err %d\n",e);
  e =  ini_read("cfg/ini_all.txt", &vars_cfg_all);
  if (e)
    printf("err reading ini_all.txt err %d\n",e);


  char *tty0_p, *tty1_p;
  e = ini_get_string(vars_cfg_all,"tty0", &tty0_p);
  if (e) tty0_p=0;
  e = ini_get_string(vars_cfg_all,"tty1", &tty1_p);
  if (e) tty1_p=0;

  if (qregs_init(tty0_p,tty1_p)) err("qregs fail");
  // printf("just called qregs init\n");
  //  qregs_print_adc_status();   printf("\n");



  is_alice=0;
  alice_syncing=0;
  alice_txing=0;
  cdm_en=0;



  if (mode)
    printf("mode %c\n",mode);
  else {
    char *s=ini_ask_str(tvars, "mode (qncs)", "mode", "n");
    mode=s[0];
  }

  
  if (st.tx_mem_circ) {
    printf("NOTE: tx_mem_circ = 1\n");
    is_alice=0;
    alice_syncing=0;
    qregs_set_alice_syncing(0);
  } else {
    is_alice = ini_ask_yn(tvars, "is_alice", "is_alice", 1);
  }
  
  if (mode=='n') {
    opt_meas_noise=1;
    noise_dith=(int)ini_ask_num(tvars, "noise_dith", "noise_dith", 1);
  }
  
  
  // For bob, this sets tx part to use an independent free-running sync
  // For alice, this uses the syncronizer
  // TODO: combine concept with set_sync_ref.
  qregs_halfduplex_is_bob(!is_alice);


  printf("mode %c\n", mode);
  
  if (mode=='s') {
    printf("alice syncing\n");
    alice_syncing=1;
  }else if (mode=='q') {
    printf("alice txing\n");
    alice_txing=1;
  }else if (mode=='n') {

  }else if (mode=='c') {
    cdm_en=1;
  }



  if (!cdm_en) {
    ini_get_double(tvars, "frame_pd_us", &d);
    i = qregs_dur_us2samps(d);
    qregs_set_frame_pd_asamps(i);
  }


  // INTERACTIVE ASKING HOW MANY FRAMES TO SAVE
  int max_frames_per_buf = (int)floor(ADC_N/st.frame_pd_asamps);
  // NOTE: here, by "buf" we mean a libiio buf, which has
  // its own set of constraints as determined by AD's libiio library.
  if (opt_periter) {
    // user explicitly ctls details of nested loops
    num_iio_itr = 1;
    if (opt_ask_iter)
      num_iio_itr = ask_nnum("num_iio_itr", num_iio_itr);


    if (mode=='c')
      tx_frame_qty = 2;
    else if (mode=='q') {
      tx_frame_qty = ask_num("frames per itr", "frames_per_itr", 10);
    }else if (mode=='s') {
      tx_frame_qty = 2;
    }else if (mode=='n') {
      tx_frame_qty = 2;
    //     frame_qty_req = is_alice?10:200;
    }else
      tx_frame_qty = ask_num("frames per itr", "frames_per_itr", 10);




    qregs_set_init();

    

    // MOVE ALL CDM CONFIG HERE
    if (cdm_en) {
      int req_frame_pd_asamps;
      hdl_cdm_cfg_t cdm_cfg;

      //    d = ini_ask_num(tvars, "CDM frame pd (us)", "cdm_frame_pd_aus", 1);
      // i = qregs_dur_us2samps(d);
      ini_get_int(tvars, "cdm_frame_pd_asamps", &i);
      double us = qregs_dur_samps2us(i);
      printf("  prior cdm frame pd %d asamps = %.3f us\n", i, us);
      i = ini_ask_num(tvars, "CDM frame pd (asamps)",
		      "cdm_frame_pd_asamps", i);
      req_frame_pd_asamps = i;
      cdm_cfg.frame_pd_asamps = i;
      printf("  osamp %d\n", st.osamp);
      cdm_cfg.sym_len_asamps = st.osamp;
      cdm_cfg.num_iter  = ask_num("num CDM iterations", "num_cdm_iter", 4);
      i = ask_num("probe len (bits)", "cdm_probe_len_bits", 32);
      cdm_cfg.probe_len_asamps = i*cdm_cfg.sym_len_asamps;
      cdm_cfg.stream = ask_yn("stream", "cdm_stream", 4);
      
      qregs_set_cdm_cfg(&cdm_cfg, &lcl_iio.rx_buf_sz_bytes);


      if (req_frame_pd_asamps!=st.frame_pd_asamps)
	printf("    frame pd ACTUALY %d asamps\n", st.frame_pd_asamps);
      
      printf("  probe_len_asamps %d\n", st.cdm_cfg.probe_len_asamps);
      printf("  num cdm passes %d\n",   st.cdm_num_passes);
      printf("  num iter %d\n", st.cdm_cfg.num_iter);
      printf("  txing %d probes, rxing %zd bytes per corrvector\n",
	     st.cdm_probe_qty_to_tx, lcl_iio.rx_buf_sz_bytes);
      tx_frame_qty = st.cdm_probe_qty_to_tx * (stream?4:1);
      // TODO: for stream use INDFEFINITE

      rx_frame_qty=0; // todo: elim this global
      
      // should get st.frame_pd_asamps/2
      // used to have bug where only getting half what I expect
      
      lcl_iio.num_iter=1;
      lcl_iio.rx_num_bufs=stream?2:1;
      if (lcl_iio.rx_buf_sz_bytes > MAX_RXBUF_SZ_BYTES)
	err("BUG: use multiple rx iio bufs for cdm");
      
      
    } else if (mode=='n'){
      printf("NOISE MEAS\n");
      
      tx_frame_qty = 2;
      rx_frame_qty = 50;
      printf("  would like to save %d frames worth\n", rx_frame_qty);
      // round up num frames to save, to fit into rx dma (adc) bufs.
      lcl_iio.rx_num_bufs = ceil((double)(rx_frame_qty) / max_frames_per_buf);
      printf("  so num_bufs %d per itr\n", lcl_iio.rx_num_bufs);
      frames_per_iiobuf = ceil((double)(rx_frame_qty) / lcl_iio.rx_num_bufs);
      rx_frame_qty = lcl_iio.rx_num_bufs * frames_per_iiobuf;
      if (rx_frame_qty != tx_frame_qty)
	printf("  actually SAVING %d frames per itr\n", rx_frame_qty);
      lcl_iio.rx_buf_sz_bytes = frames_per_iiobuf * st.frame_pd_asamps*4;
      printf("  rxbuf_len_bytes %zd\n", lcl_iio.rx_buf_sz_bytes);

      
    } else{
      
      i = round(64e-6 * st.asamp_Hz); // est round trip
      printf("round trip = %d asamps", i);
      i = ceil((double)i/st.frame_pd_asamps); // est round asamps
      printf(" = %d frames\n", i);

      // printf("is_alice %d  alice_syncing %d\n", is_alice, alice_syncing);
      
      if (!is_alice && alice_syncing)
        rx_frame_qty = tx_frame_qty*2 + i + 8;
      else
        rx_frame_qty = tx_frame_qty + i;
      printf("  would like to tx %d and save %d frames worth\n",
	     tx_frame_qty, rx_frame_qty);
      // round up num frames to save, to fit into rx dma (adc) bufs.
      printf("  max frames per buf %d", max_frames_per_buf);
      lcl_iio.rx_num_bufs = ceil((double)(rx_frame_qty) / max_frames_per_buf);
      printf("  so num_bufs %d per itr\n", lcl_iio.rx_num_bufs);
      frames_per_iiobuf = ceil((double)(rx_frame_qty) / lcl_iio.rx_num_bufs);
      rx_frame_qty = lcl_iio.rx_num_bufs * frames_per_iiobuf;
      if (rx_frame_qty != tx_frame_qty)
	printf("  actually SAVING %d frames per itr\n", rx_frame_qty);
      lcl_iio.rx_buf_sz_bytes = frames_per_iiobuf * st.frame_pd_asamps*4;
      printf("  rxbuf_len_bytes %zd\n", lcl_iio.rx_buf_sz_bytes);
    }
    
  } else {

    // AUTOCALC num iter, num bufs, ets.
    // TODO: cdm should use this option.
    // user specifies desired total number of frames to tx.
    // user wants to minimize the number of iterations.
    // and code translates that to num iter, num buffers, and buf len
    // it always uses four buffers or less per iter.

    tx_frame_qty = ask_nnum("frame_qty", max_frames_per_buf*4);
    num_iio_itr = ceil((double)tx_frame_qty / (max_frames_per_buf*4));
    printf(" num itr %d\n", num_iio_itr);
    rx_frame_qty = ceil((double)tx_frame_qty / num_iio_itr); // per iter

    lcl_iio.rx_num_bufs = (int)ceil((double)rx_frame_qty / max_frames_per_buf); // per iter
    printf(" num bufs per iter %d\n", lcl_iio.rx_num_bufs);
    frames_per_iiobuf = (int)(ceil((double)rx_frame_qty / lcl_iio.rx_num_bufs));

    lcl_iio.rx_buf_sz_bytes = frames_per_iiobuf * st.frame_pd_asamps*4;
    printf("  rxbuf_len_asamps %zd\n", lcl_iio.rx_buf_sz_bytes);
   
    rx_frame_qty = frames_per_iiobuf * lcl_iio.rx_num_bufs;
    printf(" frame qty per itr %d\n", rx_frame_qty);
  }
  
  iio_dly_ms = 0;
  if (num_iio_itr>1) {
    iio_dly_ms = ask_num("delay per itr (ms)", "dly_ms", 0);
    printf("test will last %d s\n", num_iio_itr*iio_dly_ms/1000);
  }

  if (cdm_en || (mode=='n'))
    search=0;
  else
    search = ini_ask_yn(tvars, "search for probe/pilot", "search", 1);
  

  lcl_iio_open();
  lcl_iio_create_dac_bufs(2048);

  int hdr_preemph_en = 0;
  // for now I have to be able to turn off hdr_preemph
  // If I want to send the test sinusoid.
  if (!st.tx_mem_circ && !is_alice) {
    hdr_preemph_en = ini_ask_yn(tvars, "use IM preemphasis in pilot",
				"hdr_preemph_en", 1);
    if (hdr_preemph_en) {
      char *pilot_preemph_fname_p;
      short int mem[DAC_N];
      size_t data_sz_samps, mem_sz, sz;
      e = ini_get_string(vars_cfg_all,"qsdc_im_preemph_fname", &pilot_preemph_fname_p);
      if (e) err("missing qsdc_im_preemph_fname in cfg/ini_all.txt");
      mem_sz=read_file_into_buf(pilot_preemph_fname_p, mem, sizeof(mem));
      sz = st.frame_pd_asamps * 2;
      if (mem_sz > sz) {
	printf("WARN: file sz %zd is too long. truncating to %zd\n", mem_sz, sz);
	mem_sz = sz;
      }
      
      void *p;
      p = iio_buffer_start(lcl_iio.dac_buf);
      if (!p) err("no buffer yet");
      memcpy(p, mem, mem_sz);
      // sz = iio_channel_write(dac_ch0, dac_buf, mem, mem_sz);
      // returned 256=DAC_N*2, makes sense
      printf("  filled dac_buf sz %zd bytes\n", mem_sz);

      qregs_zero_mem_raddr();

      sz = iio_device_get_sample_size(lcl_iio.dac);
      printf("  dac samp sz %zd\n", sz);
      data_sz_samps = (int)(mem_sz/sz);
      if (data_sz_samps*sz != mem_sz) {
	printf("BUG: bad snd sz");
      }
      sz = iio_buffer_push_partial(lcl_iio.dac_buf, data_sz_samps); // supposed to ret num bytes
      if (sz<0)
	printf("ERR: IIO error %zd\n", sz);
      else printf("  pushed %zd bytes\n", sz);

      i = mem_sz/8-2;
      h_w_fld(H_DAC_DMA_MEM_RADDR_LIM_MIN1, i);
      qregs_dbg_get_info(&j);
      printf("  DBG: set raddr lim %d (dbg %d)\n", i, j);
    }
  }
  st.pilot_cfg.im_from_mem = hdr_preemph_en;
  qregs_cfg_pilot(&st.pilot_cfg, 0);
  
  
  
  e=first_action();
  if (!opt_go)
    prompt("READY? ");
  e=second_action();

  if (qregs_done()) err("qregs_done fail");

  ini_save(tvars);
  ini_free(vars_cfg_all);
  ini_free(tvars);
  
  
}
