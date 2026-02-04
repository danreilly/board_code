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
//#include "qregc.h"
#include <time.h>
#include "ini.h"
#include "util.h"
#include "h_vhdl_extract.h"
#include "h.h"
#include "tsd.h"
#include "hdl.h"


int is_cli=0;

lcl_iio_t lcl_iio;


#define QREGC_LINKED (0)
#define QNICLL_LINKED (0)

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



int chan_en_delme(struct iio_channel *ch) {
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


ini_val_t *tvars, *hvars, *vars_cfg_all;



int opt_dflt=0;
int opt_sync=0;

char mode=0;


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





void read_ini_files(void) {
  int e;
  char fname[64];
  e = ini_read("tvars.txt", &tvars);
  if (e)
    printf("ini err %d\n",e);
  e =  ini_read("cfg/ini_all.txt", &vars_cfg_all);
  if (e)
    printf("ini err %d\n",e);


  // hots specific
  strcpy(fname,"cfg/ini_");
  gethostname(fname+strlen(fname), sizeof(fname)-strlen(fname));
  fname[63]=0;
  strcat(fname, ".txt");
  printf("reading file %s\n", fname);
  e = ini_read(fname, &hvars);
  if (e) {
    printf("ERR: cant read ini file: code %d\n", e);
    printf("%s\n", ini_err_msg());
    //    return CMD_ERR_FAIL;
  }
  
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




//int save_fd;

int iio_dly_ms=0; // can leave global
int frames_per_iiobuf;
int frame_qty;
int search=0;



// lcl_iio_t lcl_iio={0};



/* 
int lcl_iio_open_delme() {
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
*/











  // TODO: make a "cap" option that uses tx_0.
  // could be used for IQ imbal calib.

int main(int argc, char *argv[]) {
  int i, j, e, opt_save=0;
int opt_srv=0;  
  char c;
  double d;
  int opt_periter=1, opt_ask_iter=0;
  for(i=1;i<argc;++i) {
    for(j=0; (c=argv[i][j]); ++j) {
      //      if (c=='c') opt_corr=1;
      if (c=='a') opt_dflt=1;
      else if (c=='s') {opt_dflt=1; mode='s';}
      else if (c=='q') {opt_dflt=1; mode='q';}
      else if (c=='c') {opt_dflt=1; mode='c';}
      else if (c=='n') {opt_dflt=1; mode='n';} // noise meas
      else if (c=='d') opt_srv=1; // run as demon
      else if (c=='v') opt_save=0;
      else if (c=='i') { opt_ask_iter=1; opt_periter=1;}
      else if (c=='a') opt_periter=0;
      else if (c!='-') {
	printf("USAGES:\n  tst d  = run as demon\n  tst n  = dont save to file\n  tst a = auto buf size calc\n  tst i = ask num iters\n tst d =use all defaults");
	return 1;}
    }
  }
  ini_opt_dflt = opt_dflt;


  read_ini_files();
  

  char *tty0_p, *tty1_p;
  e = ini_get_string(vars_cfg_all,"tty0", &tty0_p);
  if (e) tty0_p=0;
  e = ini_get_string(vars_cfg_all,"tty1", &tty1_p);
  if (e) tty1_p=0;
  if (qregs_init(tty0_p, tty1_p)) err("qregs fail");
  // printf("just called qregs init\n");
  //  qregs_print_adc_status();   printf("\n");


  if (opt_srv) {
    printf("running server\n");
    e=tsd_serve();
    return 0;
  }else {
    is_cli = ini_ask_yn(hvars, "connect to remote", "connect_to_remote",0);
    if (is_cli) {
      char *ia;
      if (ini_get_string(hvars,"remote_ipaddr", &ia)) {
	printf("ERR: ini_<host>.txt lacks remote_ipaddr value\n");
	return 0;
      }
      e=hdl_connect(ia);
      if (e) return 0;
    }
  }


  int tx_0;
  int frame_qty_to_tx;
  int cdm_en=0;
  int is_alice=0;
  int cipher_en=0;
  int decipher_en=0;
  int alice_syncing=0, alice_txing=0;
  lcl_iio_t *iio=&lcl_iio;
  
  if (st.tx_mem_circ) {
    printf("NOTE: tx_mem_circ = 1\n");
    is_alice=0;
  } else {
    is_alice = ini_ask_yn(tvars, "is_alice", "is_alice", 1);
  }

  if (mode=='q') {
    // even if not bob, alice can record in meta file.
    lookup_int("cipher_en",   &cipher_en);
    lookup_int("decipher_en", &decipher_en);
  }
  
  // For bob, this sets tx part to use an independent free-running sync
  // For alice, this uses the syncronizer
  // TODO: combine concept with set_sync_ref.
  qregs_halfduplex_is_bob(!is_alice);

  alice_syncing=0;
  alice_txing=0;
  cdm_en=0;
  if (mode=='s') {
    alice_syncing=1;
  }else if (mode=='q') {
    alice_txing=1;
  }else if (mode=='c') {
    cdm_en=1;
  }else {
    cdm_en  = ini_ask_yn(tvars, "do reflection tomo (CDM)", "cdm_en", 0);
    if (!cdm_en) {
      alice_syncing = ini_ask_yn(tvars, "is alice syncing", "alice_syncing", 1);
      alice_txing   = ini_ask_yn(tvars, "is alice txing", "alice_txing", 1);
    }
  }

  hdl_cdm_cfg_t cdm_cfg;
  hdl_cdm_cfg_t r_cdm_cfg;

  if (cdm_en) {
    mode='c';
    cdm_cfg.is_passive = is_alice;
    cdm_cfg.is_wdm = 0;
    
    //    d = ini_ask_num(tvars, "CDM frame pd (us)", "cdm_frame_pd_aus", 1);
    // i = qregs_dur_us2samps(d);
    ini_get_int(tvars, "cdm_frame_pd_asamps", &i);
    double us = qregs_dur_samps2us(i);
    printf("  prior frame pd %d asamps = %.3f us\n", i, us);
    i = ini_ask_num(tvars, "CDM frame pd (asamps)", "cdm_frame_pd_asamps", i);

    cdm_cfg.sym_len_asamps = st.osamp;
    cdm_cfg.frame_pd_asamps = i;
    cdm_cfg.probe_len_asamps = st.hdr_len_bits * st.osamp;
    cdm_cfg.num_iter = ask_num("num CDM iterations", "num_cdm_iter", 4);
    
    //    tsd_lcl_cdm_cfg(&cdm_cfg);
    //    printf("  num cdm passes %d\n", st.cdm_num_passes);
    //    printf("  txing %d probes\n",   st.cdm_probe_qty_to_tx);
    //    frame_qty_to_tx = st.cdm_probe_qty_to_tx;

    //    frame_qty = 1;
    // should get st.frame_pd_asamps/2
    // BUG only getting half what I expect
    //    iio->rx_buf_sz_asamps = st.frame_pd_asamps/2/2;
    //    iio->num_iter=1;
    //    iio->rx_num_bufs=1;
    //    if (iio->rx_buf_sz_asamps > ADC_N)
    //      err("BUG: use multiple rx iio bufs for cdm");
    //    frame_qty_to_tx = 2;

  }
  


  // INTERACTIVE ASKING HOW MANY FRAMES TO SAVE
  int max_frames_per_buf = (int)floor(ADC_N/st.frame_pd_asamps);
  // NOTE: here, by "buf" we mean a libiio buf, which has
  // its own set of constraints as determined by AD's libiio library.
  if (!cdm_en) {
    if (opt_periter) {
      // user explicitly ctls details of nested loops
      iio->num_iter = 1;
      if (opt_ask_iter)
	iio->num_iter = ask_nnum("num_iio_itr", 1);

      if (mode=='q')
	frame_qty_to_tx = is_alice?10:1862;
      //     frame_qty_req = is_alice?10:200;
      else
	frame_qty_to_tx = ask_num("frames per itr", "frames_per_itr", 10);

      i = round(8e-6 * st.asamp_Hz); // est round trip
      //    printf("6us = %d asamps\n", i);
      i = ceil((double)i/st.frame_pd_asamps);
      // printf(" = %d frames\n", i);
      if (!is_alice && alice_syncing)
	frame_qty = frame_qty_to_tx*2 + i + 8;
      else
	frame_qty = frame_qty_to_tx + i;
      printf("  would like to save %d frames worth\n", frame_qty);
      // round up num frames to save, to fit into rx dma (adc) bufs.
      printf("  max frames per buf %d", max_frames_per_buf);
      iio->rx_num_bufs = ceil((double)(frame_qty) / max_frames_per_buf);
      printf("  so num_bufs %d per itr\n", iio->rx_num_bufs);
      frames_per_iiobuf = ceil((double)(frame_qty) / iio->rx_num_bufs);
      frame_qty = iio->rx_num_bufs * frames_per_iiobuf;
      if (frame_qty != frame_qty_to_tx)
	printf("  actually SAVING %d frames per itr\n", frame_qty);
      iio->rx_buf_sz_bytes = frames_per_iiobuf * st.frame_pd_asamps*4;
      printf("  rxbuf_len_bytes %zd\n", iio->rx_buf_sz_bytes);
    
    } else {
      // AUTOCALC num iter, num bufs, ets.
      // TODO: cdm should use this option.
      // user specifies desired total number of frames to tx.
      // user wants to minimize the number of iterations.
      // and code translates that to num iter, num buffers, and buf len
      // it always uses four buffers or less per iter.

      frame_qty_to_tx = ask_nnum("frame_qty", max_frames_per_buf*4);
      iio->num_iter = ceil((double)frame_qty_to_tx / (max_frames_per_buf*4));
      printf(" num itr %d\n", iio->num_iter);
      frame_qty = ceil((double)frame_qty_to_tx / iio->num_iter); // per iter

      iio->rx_num_bufs = (int)ceil((double)frame_qty / max_frames_per_buf); // per iter
      printf(" num bufs per iter %d\n", iio->rx_num_bufs);
      frames_per_iiobuf = (int)(ceil((double)frame_qty / iio->rx_num_bufs));

      iio->rx_buf_sz_bytes = frames_per_iiobuf * st.frame_pd_asamps*4;
      printf("  rxbuf_len_bytes %zd\n", iio->rx_buf_sz_bytes);
   
      frame_qty = frames_per_iiobuf * iio->rx_num_bufs;
      printf(" frame qty per itr %d\n", frame_qty);
    }
  }
  
  // not a real param. can leave global.
  iio_dly_ms = ask_num("iio delay per itr (ms)", "iio_dly_ms", 0);


  if (cdm_en)
    search=0;
  else
    search = ini_ask_yn(tvars, "search for probe/pilot", "search", 1);
  

  tsd_lcl_iio_open(&lcl_iio);
  printf("done lcl iio open\n");


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
      p = iio_buffer_start(iio->dac_buf);
      if (!p) err("no buffer yet");
      memcpy(p, mem, mem_sz);
      // sz = iio_channel_write(dac_ch0, dac_buf, mem, mem_sz);
      // returned 256=DAC_N*2, makes sense
      printf("  filled dac_buf sz %zd bytes\n", mem_sz);

      qregs_zero_mem_raddr();

      sz = iio_device_get_sample_size(iio->dac);
      printf("  dac samp sz %zd\n", sz);
      data_sz_samps = (int)(mem_sz/sz);
      if (data_sz_samps*sz != mem_sz) {
	printf("BUG: bad snd sz");
      }
      sz = iio_buffer_push_partial(iio->dac_buf, data_sz_samps); // supposed to ret num bytes
      if (sz<0)
	printf("ERR: %d\n", sz);
      else printf("  pushed %zd bytes\n", sz);

      // This problem is solved
      i = mem_sz/8-2;
      h_w_fld(H_DAC_DMA_MEM_RADDR_LIM_MIN1, i);
      qregs_dbg_get_info(&j);
      printf("  DBG: set raddr lim %zd (dbg %d)\n", i, j);
    }
  }
  st.pilot_cfg.im_from_mem = hdr_preemph_en;
  qregs_cfg_pilot(&st.pilot_cfg, 0);
  


  tsd_setup_params_t params;
  params.is_alice        = is_alice;
  params.frame_qty_to_tx = frame_qty_to_tx;
  params.opt_save        = opt_save;
  params.opt_corr        = 0;
  params.cipher_en       = cipher_en;
  params.mode            = mode;
  params.alice_txing     = alice_txing;
  params.alice_syncing   = alice_syncing;
  params.decipher_en     = decipher_en;

  ini_save(tvars);
  
  //  e=tsd_first_action(&params);

  qregs_clr_adc_status();
  qregs_clr_tx_status();
  qregs_clr_corr_status();

  
  
  switch (mode) {
    ssize_t rx_buf_sz_bytes;
    case 'c':  
      printf("local cdm cfg\n");
      tsd_lcl_cdm_cfg(&cdm_cfg, &rx_buf_sz_bytes);

      if (is_cli) {
	printf("rem cdm cfg\n");

	r_cdm_cfg = cdm_cfg;
	r_cdm_cfg.is_passive=!cdm_cfg.is_passive;
	e=hdl_cdm_cfg(&r_cdm_cfg);
	if (e)
	  printf("ERR: hdl_cdm_cfg ret %d\n", e);
      }

      // if both decide to continue, set up passive first.

      if (cdm_cfg.is_passive) {
	tsd_lcl_cdm_go();
	if (is_cli) {
	  e=hdl_cdm_go();
	  if (e)
	    printf("ERR: remote start failed\n");
	}
      }else {

	if (is_cli) {
	  printf("tell cli to go\n");
	  e=hdl_cdm_go();
	  if (e)
	    printf("ERR: remote start failed\n");
	} else
	  prompt("is remote READY?");
	
	
	iio->num_iter=1;
	iio->rx_buf_sz_bytes = rx_buf_sz_bytes;
	iio->rx_num_bufs=1;
	if (iio->rx_buf_sz_bytes > MAX_RXBUF_SZ_BYTES)
	  err("BUG: use multiple rx iio bufs for cdm");
	

	e=tsd_iio_create_rxbuf(iio);
	if (e) {
	  printf("ERR: create rxbuf failed\n");
	  return -1;
	}
	
	printf("local go\n");	
	tsd_lcl_cdm_go();
	
	tsd_iio_read(iio);



	if (is_cli) {
	  printf("tell remote to stop\n");
	  e=hdl_cdm_stop();
	  if (e)
	    printf("ERR: remote stop failed\n");
	}


	tsd_lcl_cdm_stop();



	// I ought to be able to do this, but it causes illegal instr
	//	tsd_iio_destroy_rxbuf(iio);
	
	//	printf("seconed action\n");	
	//	e=tsd_second_action();

	
	

      }

      
  } // switch
  

  if (is_cli) {
    e=hdl_disconnect();
    if (e)
      printf("ERR: disconnect failed\n");
  }

  

  if (qregs_done()) err("qregs_done fail");

  ini_free(vars_cfg_all);
  ini_free(tvars);
  
  
}
