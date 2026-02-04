
// tsd.c
// 
// responds to "commands" sent via TCPIP.
// Each command comes in a packet.  First is a uint32 containing
// the byte length of the command, followed by the command as a string.
//
// each command is implemented by a function called cmd_*
// defined in this code.
//
// If the cmd function succeeds it returns a z
//
//
// If the cmd function cannot do the command for some reason,
// it puts the reason into the global errmsg
// and returns a non-zero integer (one of CMD_ERR*).


#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <math.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "qregs.h"
#include "qregs_ll.h"
#include "h.h"
#include "h_vhdl_extract.h"
#include "cmd.h"
#include "tsd.h"
#include "parse.h"
#include "util.h"
#include "ini.h"

#include <unistd.h>
#include <linux/reboot.h>
#include <pthread.h>

//extern ssize_t read_file_into_buf(char *fname, void *buf, ssize_t buf_sz);

#define TSD_ERRMSG_LEN (1024)
char tsd_errmsg[TSD_ERRMSG_LEN];
int  tst_err;

char *tsd_get_last_errmsg(void) {
  return tsd_errmsg;
}
int tsd_err(int code, char *msg) {
  if (msg!=tsd_errmsg)
    strcpy(tsd_errmsg, msg);
  return code;
}
int tsd_err_fail(char *msg) {
  return tsd_err(HDL_ERR_FAIL, msg);
}





#define QREGD_PORT (5000)

#define DO(CALL) { \
  int e; \
  e = CALL; \
  if (e) return e; \
  }


static ini_val_t *tvars=0, *vars_cfg_all=0;


#define IIO_ADC_MAX_ASAMPS (2<<18)



int qna_connected=0;

char rbuf[1024]; // responses
char rxbuf[1024]; // rx
char txbuf[1024]; // rx

tsd_state_t tsd_st={0};
hdl_cdm_cfg_t cdm_cfg={0}; // get rid of this
hdl_loop_cfg_t loop_cfg;//={0}; // get rid of this
hdl_noise_cfg_t noise_cfg;//={0}; // get rid of this


extern int iio_dly_ms; // can leave global



//static void dbg(char *str) {
//  printf("DBG: %s\n", str);
//}


static int err(int errcode, char *msg){
  strcpy(tsd_errmsg, msg);
  return errcode;
}

static int err_fail(char *msg){
  strcpy(tsd_errmsg, msg);
  return HDL_ERR_FAIL;
}

static int err_bug(char *msg){
  strcpy(tsd_errmsg, msg);
  return HDL_ERR_BUG;
}


// TODO: clean this up.
// "commands" sprintf a response in cmdrsp. and return CMD_ERR*
char cmdrsp[1024];
char qnarsp[1024];
int cmd_err(int code, char *msg) {
  sprintf(qnarsp, "%d %s", code, msg);
  return code;
}
int cmd_err_fail(char *msg) {
  return cmd_err(CMD_ERR_FAIL, msg);
}
int cmd_err_bug(char *msg) {
  return cmd_err(CMD_ERR_BUG, msg);
}
int cmd_qerr(char *msg) {
  // command reports error from qregs  
  sprintf(tsd_errmsg, "%s\n%s", qregs_last_errmsg(), msg);
  return CMD_ERR_FAIL;
}



int save_fd=0;
// measurements stored here
int *times_s=0;
double *corr=0;


tsd_setup_params_t tsd_params={0};




void tsd_set_data_handler(tsd_data_handler_fn_t *fn) {
  tsd_st.data_handler_fn = fn;  
}



// Many tsd local functions and commands
// depend on looking up default params from ini files:
static void lookup_int(char *var_name, int *i_p) {
  int e=ini_get_int(tvars, var_name, i_p);
  if (e)
    printf("WARN: %s undefined in tvars\n", var_name);
}




int tsd_init(void)
{
  int e;


  gethostname(tsd_st.hostname, sizeof(tsd_st.hostname));
  tsd_st.hostname[64]=0;
  
  e =  ini_read("tvars.txt", &tvars);
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

  if (qregs_init(tty0_p,tty1_p))
    return err_fail("qregs_init");

  //  qregs_print_adc_status();   printf("\n");

  return 0;
}

int tsd_done(void)
{
  ini_free(tvars);
  ini_free(vars_cfg_all);
  
  if (qregs_done()) printf("qregs_done fail\n");
  //    assert(zcu_xport_set_dest("169.254.135.160") == 0);
  //    assert(zcu_xport_send(samples, 4096) == 0);
  return 0;
}


int tsd_iio_create_rxbuf(lcl_iio_t *iio) {
// This allocs ring of ADC bufs and starts RX DMA
  ssize_t rx_buf_sz_asamps, sz;

  if (iio->adc_buf) {
    printf("ERR: iio rxbuf already created!\n");
    return 0;
  }
  
  sz = iio_device_get_sample_size(iio->adc);  // sz=4;
  printf("adc samp sz %zd\n",sz);
  rx_buf_sz_asamps = iio->rx_buf_sz_bytes / sz;
  printf("create rx bufs sz %zd bytes\n", iio->rx_buf_sz_bytes);
  iio->adc_buf = iio_device_create_buffer(iio->adc,
					  rx_buf_sz_asamps,
					  false);
  if (!iio->adc_buf)
    return err_fail("cant make adc buffer");

  // printf("made adc buf size %zd asamps\n", (ssize_t)buf_len_asamps);
  // creating ADC buffer commences DMA in hdl
  return 0;
}

void tsd_iio_destroy_rxbuf(lcl_iio_t *iio) {
  printf("iio_buffer_destroy(adc)\n");  
  iio_buffer_destroy(iio->adc_buf);
  iio->adc_buf=0;
}






#if 0
int tsd_iio_read(lcl_iio_t *iio) {
  int itr, b_i, p_i, e, i;
  int t0_s;
  void *adc_buf_p;
  ssize_t rx_buf_sz, sz;
  ssize_t left_sz;    
  int refill_err=0;
  //  lcl_iio_t *iio=&st.lcl_iio;  
  // qregs_print_adc_status();	  

  t0_s = time(0);

  
  // This is the recieve data loop
  for (itr=0; !iio->num_iter || (itr<iio->num_iter); ++itr) {

    if (iio->num_iter>1)
      printf("itr %d: time %ld (s)\n", itr, time(0)-t0_s);
    if (times_s && iio->num_iter)
      *(times_s + itr) = (int)time(0);
    
    for(b_i=0; b_i<iio->rx_num_bufs; ++b_i) {
      void *p;

      printf("will refill\n");
      sz = iio_buffer_refill(iio->adc_buf);
      printf("refilled %zd\n", sz);
      
      //      qregs_print_adc_status();
      if (sz<0) {
	iio_strerror(-sz, tsd_errmsg, 512);
	printf("\nREFILL ERR: %s\n", tsd_errmsg);
	qregs_print_hdr_det_status(); // this prints it all
	sprintf(tsd_errmsg, "cant refill rx bufer %d", b_i);
	refill_err=1;
	err(tsd_errmsg);
      }
      //      prompt("refilled buf");
      
      if (sz != iio->rx_buf_sz_bytes)
	printf("tried to rx %ld but got %ld bytes\n",
	       iio->rx_buf_sz_bytes, sz);
      // pushes double the dac_buf size.
      //qregs_print_adc_status();

	
      // iio_buffer_start can return a non-zero ptr after a refill.
      adc_buf_p = iio_buffer_start(iio->adc_buf);
      if (!adc_buf_p) err("iio_buffer_start returned 0");
      p = iio_buffer_end(iio->adc_buf);
      // printf(" size %zd\n", p - adc_buf_p);

      
      if (tsd_params.opt_corr) {  // If correlating in C
	int frames_per_iiobuf = iio->rx_buf_sz_bytes / (st.frame_pd_asamps*4);
	for(p_i=0; p_i<frames_per_iiobuf; ++p_i) {
	  //	  printf("p %d\n",p_i);
	  p = adc_buf_p + sizeof(short int)*2*p_i*st.frame_pd_asamps;
	  // printf("offset %zd\n",p - adc_buf_p);
	  corr_accum(corr, adc_buf_p + sizeof(short int)*2*p_i*st.frame_pd_asamps);
	}
      }


      if (tsd_params.opt_save && save_fd) {
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
      
    if (tsd_params.search) {
      // prompt("OK");
      //      qregs_print_hdr_det_status();
      //      prompt("DATA SAVED");
      usleep(1000*200);
      qregs_print_hdr_det_status();
      usleep(1000*200);
      qregs_search_en(0);
    }
    qregs_txrx(0);



#if QNICLL_LINKED
    if (is_alice && use_qnicll) {
      C(qnicll_bob_sync_go(0));
      C(qnicll_done());
    }
#endif    
    
    if (tsd_params.opt_corr) {
      corr_find_peaks(corr, tsd_params.frame_qty_to_tx);
    }
    
    iio_buffer_destroy(iio->adc_buf);
    usleep(iio_dly_ms * 1000);
   
    
  } // for iio itr

    
  //    printf("out of itr loop %d\n",  use_qnicll);
  //  qregs_set_alice_txing(0);


  int tx_always = st.tx_always;
  qregs_frame_pwrs_t pwrs={0};
  if (!refill_err && !tsd_params.is_alice && tsd_params.alice_txing) {
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


  //  usleep(1); // AD fifo funny thing workaround
  //  qregs_set_cdm_en(0,0);


  /*
  for(j=i-10; j<i; ++j)
    printf("\t%d", rx_mem[j]);
  printf("\n");
  for(j=0; (i<ADC_N)&&(j<16); ++i,++j)
    printf("\t%d", rx_mem[i]);
  printf("\n");
  */
  if (tsd_params.opt_save) {
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
    fprintf(fp,"meas_noise = %d;\n",   (tsd_params.mode=='n'));
    fprintf(fp,"cdm_en = %d;\n",       (tsd_params.mode=='c'));
    fprintf(fp,"cdm_num_iter = %d\n",  st.cdm_cfg.num_iter);
    fprintf(fp,"noise_dith = %d;\n",   (tsd_params.mode=='n'));
    fprintf(fp,"tx_always = %d;\n",    st.tx_always);
    fprintf(fp,"tx_hdr_twopi = %d;\n", st.tx_hdr_twopi);
    fprintf(fp,"tx_mem_circ = %d;\n",  st.tx_mem_circ);
    fprintf(fp,"tx_same_cipher = %d;\n", st.tx_same_cipher);
    fprintf(fp,"is_alice = %d;\n",    tsd_params.is_alice);
    if (tsd_params.is_alice) 
      fprintf(fp,"rx_same_hdrs = 1;\n");
    else
      fprintf(fp,"rx_same_hdrs = %d;\n", st.tx_same_hdrs);
    fprintf(fp,"alice_syncing = %d;\n", tsd_params.alice_syncing);
    fprintf(fp,"alice_txing = %d;\n",  tsd_params.alice_txing);
    fprintf(fp,"search = %d;\n",       tsd_params.search);
    fprintf(fp,"osamp = %d;\n",        st.osamp);
    fprintf(fp,"cipher_m = %d;\n",     st.cipher_m);
    fprintf(fp,"cipher_en = %d;\n",    tsd_params.cipher_en);
    fprintf(fp,"decipher_en = %d;\n",  tsd_params.decipher_en);
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
    fprintf(fp,"num_iio_itr = %d;\n", iio->num_iter);
    fprintf(fp,"time = %d;\n", (int)time(0));
    fprintf(fp,"ext_rat_dB = %.1f;\n", pwrs.ext_rat_dB);
    fprintf(fp,"body_rat_dB = %.1f;\n", pwrs.body_rat_dB);
    fprintf(fp,"itr_times = [");
    if (iio->num_iter) {
      for(i=0;i<iio->num_iter;++i)
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
#endif



int tsd_rd_pkt(int soc, char *buf, int buf_sz) {
// returns num bytes read  
  char len_buf[4];
  int l;
  ssize_t sz;
  sz=read(soc, (void *)len_buf, 4);
  if (sz==0) return 0; // end of file. soc closed
  if (sz<4) {
    printf("ERR: tsd_rd_pkt(): read len failed\n");
    return 0;
  }
  l = (int)ntohl(*(uint32_t *)len_buf);
  l = MIN(buf_sz, l);
  sz = read(soc, (void *)buf, l);
  if (sz<l) {
    printf("ERR: tsd_rd_pkt(): read body failed\n");
  }
  return sz;
}

int tsd_wr_pkt(int soc, char *buf, int pkt_sz) {
// returns error code  
  char len_buf[4];
  uint32_t l;
  ssize_t sz;
  l = htonl(pkt_sz);
  // printf("wr %d\n", pkt_sz);
  sz = write(soc, (void *)&l, 4);
  if (sz<4) return err_fail("write pktsz failed");

  
  sz = write(soc, (void *)buf, pkt_sz);
  if (sz<pkt_sz) {
    printf("size %zd\n", sz);
    printf("pkt_sz %d\n", pkt_sz);
    return err_fail("write pkt body failed");
  }
  return 0;
}

int tsd_wr_str(int soc, char *str) {
  int l = strlen(str);
  return tsd_wr_pkt(soc, str, l);
}

int check(char *buf, char *key, int *param) {
  int i, n;
  int kl=strlen(key);
  if (!strncmp(buf, key, kl)) {
    if (param) {
      n=sscanf(buf+kl, "%d", param);
      if (n!=1) return 0;
    }
    return 1;
  }
  return 0;
}






#define IIO_THREAD_CMD_EXIT (0)
#define IIO_THREAD_CMD_CAP  (1)
#define IIO_THREAD_DBG (1)


ssize_t forwarding_buf_sz_bytes = 1596000*3;
static char forwarding_buf[1596000*3];


int tsd_iio_forward(lcl_iio_t *iio)
{
  char *p;
  void *adc_buf_p;
  ssize_t tot=0, sz;
  int b_i, e;

  p=(char *)forwarding_buf;
  for(b_i=0; b_i < iio->rx_num_bufs; ++b_i) {
    // printf("call refill\n");
    sz = iio_buffer_refill(iio->adc_buf);
    //      qregs_print_adc_status();
    if (sz<0) {
      sprintf(tsd_errmsg, "cant refill adc bufer %d", b_i);
      e=HDL_ERR_FAIL;
      break;
    }
    if (sz==0) {
      printf("ERR: got 0 bytes from ADC\n");
      break;
    }

    if (sz != iio->rx_buf_sz_bytes)
      printf("tried to refill %d but got %d\n", iio->rx_buf_sz_bytes, sz);
    // pushes double the dac_buf size.
    //qregs_print_adc_status();
    // printf("refilled buf %zd\n", sz);


    // iio_buffer_start can return a non-zero ptr after a refill.
    adc_buf_p = iio_buffer_start(iio->adc_buf);
    if (!adc_buf_p) {
      sprintf(tsd_errmsg, "iio_buffer_start returned 0");
      printf("FATAL: iio_buffer_start returned 0");
      e=HDL_ERR_FAIL;
      break;
    }
    
    // p = iio_buffer_end(adc_buf);
    // printf(" size %zd\n", p - adc_buf_p);
    if (tot+sz > forwarding_buf_sz_bytes) {
      printf("BUG: will exceed expeceted size\n");
      sprintf(tsd_errmsg, "iio_fwd will exceed expected data size");
      e=HDL_ERR_BUG;
      break;
    }

    if (0) { // b_i==0) { // monitor for dbg
      short int *pp=(int16_t *)adc_buf_p;
      int i;
      for(i=0;i<64;i+=2) {
         printf("%7d  %7d\n", pp[i], pp[i+1]);
      }
    }
    
    memcpy(p, adc_buf_p, sz);
    p   += sz;
    tot += sz;
  }
  if (!tot) return HDL_ERR_FAIL;
 

  return tsd_st.data_handler_fn((uint16_t *)forwarding_buf,
				tot/sizeof(int16_t));
}

int tsd_iio_cap(lcl_iio_t *iio) {
  int b_i;
  ssize_t left_sz, sz, sz_wr=0;
  void *adc_buf_p;
  int e=0;
  int fd;

  
  fd = open("out/d.raw", O_CREAT | O_WRONLY | O_TRUNC, S_IRWXO);
  if (fd<0) return tsd_err_fail("cant open d.raw");

  
  for(b_i=0; b_i<iio->rx_num_bufs; ++b_i) {
    
    sz = iio_buffer_refill(iio->adc_buf);
    //      qregs_print_adc_status();
    if (sz<0) {
      sprintf(tsd_errmsg, "cant refill adc bufer %d", b_i);
      e=HDL_ERR_FAIL;
      break;
    }
    if (sz==0) {
      printf("ERR: got 0 bytes from ADC\n");
      break;
    }

    //      prompt("refilled buf");
    if (sz != iio->rx_buf_sz_bytes)
      printf("tried to refill %d but got %d\n", iio->rx_buf_sz_bytes, sz);
    // pushes double the dac_buf size.
    //qregs_print_adc_status();


    // iio_buffer_start can return a non-zero ptr after a refill.
    adc_buf_p = iio_buffer_start(iio->adc_buf);
    if (!adc_buf_p) {
      sprintf(tsd_errmsg, "iio_buffer_start returned 0");
      printf("FATAL: iio_buffer_start returned 0");
      e=HDL_ERR_FAIL;
      break;
    }
    // p = iio_buffer_end(adc_buf);
    // printf(" size %zd\n", p - adc_buf_p);


    
    
    left_sz = sz;
    while(left_sz>0) {
      sz = write(fd, adc_buf_p, left_sz);
      if (sz<=0) return err_fail("write failed");
      sz_wr += sz;
      if (sz == left_sz) break;
      printf("tried to write %zd but wrote %zd\n", left_sz, sz);
      left_sz -= sz;
      adc_buf_p = (void *)((char *)adc_buf_p + sz);
    }
  }
  close(fd);


  qregs_print_hdr_det_status();
  
  
  qregs_search_en(0);
  qregs_txrx(0);

  
  printf("saved %zd bytes\n", sz_wr);



  
  time_t t;
  struct tm *timeinfo;
  time(&t);
  timeinfo=localtime(&t);
  char lt[64], *p;
  strcpy(lt,asctime(timeinfo));
  for(p=lt; *p;++p)
    if (*p <' ') {*p=0; break;};

  FILE *fp;


  fp = fopen("out/r.txt","w");
  //  fprintf(fp,"sfp_attn_dB = %d;\n",   sfp_attn_dB);
  //    fprintf(fp,"tst_sync = %d;\n",     tst_sync);
  fprintf(fp,"err = %d;\n", e);
  fprintf(fp,"msg_fname = '%s';\n",   tsd_st.name);
  fprintf(fp,"localtime = '%s';\n",  lt);
  fprintf(fp,"host = '%s';\n",      tsd_st.hostname);
  fprintf(fp,"tst_sync = %d;\n",    1); // DELETE THIS!
  fprintf(fp,"asamp_Hz = %lg;\n",   st.asamp_Hz);
  fprintf(fp,"use_lfsr = %d;\n",     st.use_lfsr);
  fprintf(fp,"lfsr_rst_st = '%x';\n", st.lfsr_rst_st);
  fprintf(fp,"meas_noise = %d;\n",   0); // meas_noise);
  fprintf(fp,"cdm_en = %d;\n",      tsd_st.mode=='c');
  fprintf(fp,"noise_dith = %d;\n",  0); //  noise_dith);
  fprintf(fp,"tx_always = %d;\n",   st.tx_always);
  fprintf(fp,"is_alice = %d;\n",    !st.is_bob);
  fprintf(fp,"alice_txing = %d;\n", tsd_st.mode=='q');
  //  if (is_alice) 
  //    fprintf(fp,"rx_same_hdrs = 1;\n");
  //  else
  if (!st.is_bob)
    fprintf(fp,"rx_same_hdrs = 1;\n");
  else
    fprintf(fp,"rx_same_hdrs = %d;\n", st.tx_same_hdrs);
  fprintf(fp,"alice_syncing = %d;\n", tsd_st.mode=='s');
  fprintf(fp,"search = %d;\n",       0); // search
  fprintf(fp,"osamp = %d;\n",        st.osamp);
  fprintf(fp,"cipher_w = %d;\n",     st.cipher_w);
  fprintf(fp,"cipher_en = %d;\n",     st.cipher_en);
  fprintf(fp,"decipher_en = %d;\n",     st.decipher_en);
  fprintf(fp,"cipher_symlen_asamps = %d;\n", st.cipher_symlen_asamps);
  
  fprintf(fp,"tx_pilot_pm_en = %d;\n",  st.tx_pilot_pm_en);
  fprintf(fp,"frame_qty = %d;\n",       st.frame_qty);
  fprintf(fp,"frame_pd_asamps = %d;\n", st.frame_pd_asamps);
  fprintf(fp,"init_pwr_thresh = %d;\n", st.init_pwr_thresh);
  fprintf(fp,"hdr_pwr_thresh = %d;\n",  st.hdr_pwr_thresh);
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
  //  fprintf(fp,"data_len_samps = %d;\n", iio->cap_len_asamps);
  fprintf(fp,"data_in_other_file = 2;\n");
  fprintf(fp,"num_itr = %d;\n", 1);
  fprintf(fp,"time = %d;\n", (int)time(0));
  fprintf(fp,"itr_times = [0];\n");
  //  if (num_itr) {
  //    for(i=0;i<num_itr;++i)
  //      fprintf(fp," %d", *(times_s+i)-*(times_s));
  //  }
  //  fprintf(fp, "];\n");
  fclose(fp);

  printf("iio_cap() wrote out/r.txt and out/p.raw\n");

  return e;

}  

    




#if 0
void *iio_thread_func(void *arg) {
  int done=0;
  //  lcl_iio_t *p=&st.lcl_iio;
#if IIO_THREAD_DBG
  dbg("iio thread running");
#endif
  while (!done) {
    pthread_mutex_lock(&tsd_st.lock);
#if IIO_THREAD_DBG
    dbg("iio thread waiting");
#endif    
    pthread_cond_wait(&tsd_st.cond, &tsd_st.lock);
#if IIO_THREAD_DBG
    dbg("iio thread out of wait");
#endif    
    switch(tsd_st.thread_cmd) {
      case IIO_THREAD_CMD_EXIT:
	done=1;
        break;
      case IIO_THREAD_CMD_CAP:
	//iio_cap();
	break;
    }
    pthread_mutex_unlock(&tsd_st.lock);
  }
#if IIO_THREAD_DBG
  dbg("iio thread ending");
#endif  
  return NULL;
}

#endif

int lcl_iio_chan_en(struct iio_channel *ch, char *name) {
  iio_channel_enable(ch);
  if (!iio_channel_is_enabled(ch)) {
    static char tmp[256];
    sprintf(tmp, "%s not enabled", name);
    return err_fail(tmp);
  }
}



// A limitation of libiio.
// or at least, this is the limit to which we want to
// try to push libiio.
#define MAX_DAC_TXBUF_LEN_ASAMPS (2<<5)
#define MAX_ADC_TXBUF_LEN_ASAMPS (2<<18)





int lcl_iio_create_dac_bufs(lcl_iio_t *iio, int sz_bytes) {
  size_t sz, dac_buf_sz;

  if (iio->dac_buf) return 0;
  
  printf("iio create dac bufs\n");
  if (iio->tx_buf_sz_bytes)
    return err_fail("dac bufs already created");
  // prompt("will create dac buf");
  sz = iio_device_get_sample_size(iio->dac);
  // libiio sample size is 2 * number of enabled channels.
  dac_buf_sz = sz_bytes / sz;
  // note: oddly, this create buffer seems to cause the dac to output
  //       a ghz sin wave for about 450us.
  iio->dac_buf = iio_device_create_buffer(iio->dac, dac_buf_sz, false);
  if (!iio->dac_buf) {
    sprintf(tsd_errmsg, "cant create dac bufer  nsamp %zu", dac_buf_sz);
    return err_fail(tsd_errmsg);
  }
  iio->tx_buf_sz_bytes = sz_bytes;
}


// local IIO accesses
int tsd_lcl_iio_open(lcl_iio_t *p) {
  
  ssize_t sz, buf_sz;
  void *rval;
  int e;

  if (p->open)
    return err_fail("BUG: iio already open");


  //  pthread_mutex_init(&tsd_st.lock, NULL);
  //  tsd_st.thread_cmd = 0;
  //  pthread_cond_init(&tsd_st.cond, NULL);

  
  //  e = pthread_create(&tsd_st.thread, NULL, &iio_thread_func, NULL);
  //  if (e != 0)
  //    return err_fail("cant create IIO thread");
  
  
  p->ctx = iio_create_local_context();
  if (!p->ctx)
    return err_fail("cant get iio context");
  printf("DBG: made local iio context\n");
  e = iio_context_set_timeout(p->ctx, 3000); // in ms
  if (e)
    return err_fail("cant set iio timeout");
    
  p->dac = iio_context_find_device(p->ctx, "axi-ad9152-hpc");
  if (!p->dac)
    return err_fail("cant find dac");
  p->adc = iio_context_find_device(p->ctx, "axi-ad9680-hpc");
  if (!p->adc)
    return err_fail("cant find adc");

  p->dac_ch0 = iio_device_find_channel(p->dac, "voltage0", true); // by name or id
  if (!p->dac_ch0)
    return err_fail("dac lacks ch 0");
  p->dac_ch1 = iio_device_find_channel(p->dac, "voltage1", true); // by name or id
  if (!p->dac_ch1)
    return err_fail("dac lacks ch l");
  e=lcl_iio_chan_en(p->dac_ch0, "dac ch0");
  if (e) return e;
  e=lcl_iio_chan_en(p->dac_ch1, "dac ch1");
  if (e) return e;

  p->adc_ch0 = iio_device_get_channel(p->adc, 0);
  if (!p->adc_ch0)  return err_fail("adc lacks ch 0");
  p->adc_ch1 = iio_device_get_channel(p->adc, 1);
  if (!p->adc_ch1)  return err_fail("adc lacks ch 1");
  e=lcl_iio_chan_en(p->adc_ch0, "adc ch0");
  if (e) return e;
  e=lcl_iio_chan_en(p->adc_ch1, "adc ch1");
  if (e) return e;
  

  // sample size is 2 * number of enabled channels.
  sz = iio_device_get_sample_size(p->dac);
  // printf("dac samp size %zd\n", sz);
  //  prompt("will create dac buf");


  // WIERD:
  // At power up, dac will be emitting a sin wave.
  // creating a dac bufer stops it.
  // But then every subsequent create buffer seems to cause the dac to output
  // that sin wave for about 450us!
  // So, create it now and keep it around.  Later, use iio_buffer_push_partial.
  lcl_iio_create_dac_bufs(p, 2048);

  //  e=iio_buffer_set_blocking_mode(p->dac_buf, true); // default is blocking.
  //  if (e) return err_and_msg(CMD_ERR_FAIL, "cant set blocking");
  printf("lcl iio open\n");
  p->open=1;
  return 0;  
}




void lcl_iio_close(lcl_iio_t *p) {
  int e;
  void *rval;

  
  if (p->open) {
    // Tell the iio thread to exit
    pthread_mutex_lock(&tsd_st.lock);
    tsd_st.thread_cmd = IIO_THREAD_CMD_EXIT;
    pthread_cond_signal(&tsd_st.cond);
    pthread_mutex_unlock(&tsd_st.lock);

    e = pthread_join(tsd_st.thread, &rval);
    if (e)
      printf("ERR: pthread join failed\n");
    
    iio_context_destroy(p->ctx);
    printf("DBG: closed iio context\n");

    pthread_mutex_destroy(&tsd_st.lock);
    pthread_cond_destroy(&tsd_st.cond);

  }
}



int cmd_fwver(int arg) {
  printf("fwver %d\n", qregs_fwver);
  sprintf(cmdrsp, "%d", qregs_fwver);
  return 0;
}


// This will be deleted
int cmd_hdr_len(int arg) {
  int len;
  if (parse_int(&len)) return CMD_ERR_SYNTAX;
  printf("hdr_len %d (bits)\n", len);
  qregs_set_hdr_len_bits(len);
  //  if (st.hdr_len_bits != len)
  //    printf("WARN: hdr len actually %d\n", st.hdr_len_bits);
  sprintf(cmdrsp, "%d", st.hdr_len_bits);
  return 0;
}


int tsd_parse_key_int(char *key, int *val) {
  if (parse_search(key)) {
    sprintf(tsd_errmsg, "missing keyword %s", key);
    printf(tsd_errmsg);
    return CMD_ERR_SYNTAX;
  }
  // printf("parsing: %s\n", parse_get_ptr());
  if (parse_int(val)) {
    sprintf(tsd_errmsg, "missing int after %s", key);
    printf(tsd_errmsg);
    return CMD_ERR_SYNTAX;
  }
  return 0;
}

int tsd_parse_key_dbl(char *key, double *val) {
  if (parse_search(key)) {
    sprintf(tsd_errmsg, "missing keyword %s", key);
    return CMD_ERR_SYNTAX;
  }
  // printf("parsing: %s\n", parse_get_ptr());
  if (parse_double(val)) {
    sprintf(tsd_errmsg, "missing double after %s", key);
    return CMD_ERR_SYNTAX;
  }
  return 0;
}

int tsd_parse_key_str(char *key, char *val, int len_bytes) {
  if (parse_search(key)) {
    sprintf(tsd_errmsg, "missing keyword %s", key);
    return CMD_ERR_SYNTAX;
  }
  // printf("parsing: %s\n", parse_get_ptr());
  parse_token(val, len_bytes);
  return 0;
}




int cmd_qsdc_protocol(int arg) {
}

int cmd_setup(int arg) {
  int en, is_alice, sync;
  qregs_sync_status_t sstat;
  printf("setup\n");
  DO(tsd_parse_key_int("is_alice=", &is_alice));
  DO(tsd_parse_key_int("sync=", &sync));
  if (is_alice) {
    qregs_get_sync_status(&sstat);
    if (!sstat.locked)
      return err_fail("synchronizer unlocked");
  }
  return 0;
}

int cmd_frame_pd(int arg) {
  int pd;
  if (!parse_int(&pd)) {
    printf("frame_pd %d\n", pd);
    qregs_set_frame_pd_asamps(pd);
  }
  sprintf(rbuf, "0 %d", st.frame_pd_asamps);
  return 0;
}

int cmd_osamp(int arg) {
// default is 4  
  int os;
  if (parse_int(&os)) return CMD_ERR_NO_INT;
  printf("osamp %d\n", os);
  qregs_set_osamp(os);
  sprintf(rbuf, "%d", st.osamp);
  return 0;
}


int cmd_frame_qty(int arg) {
  int qty;
  if (parse_int(&qty)) return CMD_ERR_NO_INT;
  printf("frame_qty %d\n", qty);
  qregs_set_frame_qty(qty);
  sprintf(rbuf, "0 %d", st.frame_qty);
  return 0;
}

int cmd_tx(int arg) {
  int en;
  if (parse_int(&en)) return CMD_ERR_NO_INT;
  en = !!en;
  printf("tx %d\n", en);
  qregs_txrx(en);
  sprintf(rbuf, "0 %d", en);  
  return 0;
}


int cmd_iioopen(int arg) {
  int err;
  printf("iioopen\n");
  if (!tsd_st.iio.open) {
    err = tsd_lcl_iio_open(&tsd_st.iio);
    if (err) return err;
  }
  sprintf(rbuf, "0");
  return 0;
}

// TODO: delete this
int cmd_txrx(int arg) {
  int en;
  ssize_t sz, adc_buf_sz;
  lcl_iio_t *p = &tsd_st.iio;
  int frame_qty_req, buf_len_asamps, num_bufs, frames_per_buf, frame_qty;
  int max_frames_per_buf;
  

  if (parse_int(&frame_qty_req)) return CMD_ERR_NO_INT;
  printf("txrx %d\n", frame_qty_req);

  max_frames_per_buf = (int)floor(MAX_ADC_TXBUF_LEN_ASAMPS
				  /st.frame_pd_asamps);
  num_bufs = ceil((double)frame_qty_req / max_frames_per_buf);
  printf("dbg: so num_bufs %d\n", num_bufs);
  frames_per_buf = ceil((double)frame_qty_req / num_bufs);
  frame_qty = num_bufs * frames_per_buf;
  if (frame_qty != frame_qty_req)
    printf("dbg: actually frame qty %d\n", frame_qty);

  // this may be wrong
  p->rx_buf_sz_bytes = frames_per_buf * st.frame_pd_asamps*4;

  // for now i always do this:
  if (1) // tst_sync)
      p->rx_buf_sz_bytes *= 2;
  printf("DBG: buf_sz_bytes %d\n", p->rx_buf_sz_bytes);




  qregs_set_frame_qty(frame_qty);
  

  // If I set txrx and search after creating the buffer this does not work.
  // met_init is never set.
  // TODO: why is that the case?
  qregs_search_en(1);
  qregs_txrx(1);
  
  
  /* When we "create" the adc buffer, libiio will typically allocate
     four buffers and initiate a chained DMA xfer into them.
     This causes the dma_xfer_req to be asserted in the HDL.
   */
  sz = iio_device_get_sample_size(p->adc);  // sz=4;
  adc_buf_sz = sz * buf_len_asamps;
  p->adc_buf = iio_device_create_buffer(p->adc, buf_len_asamps, false);
  if (!p->adc_buf)
    return err_fail("cant make adc buffer");
  printf("DBG: made adc buf size %zd samps\n", (ssize_t)buf_len_asamps);
  

  p->rx_num_bufs     = num_bufs;
  p->rx_buf_sz_bytes = adc_buf_sz;
  //  p->cap_len_asamps  = cap_len_asamps;  
  

  // Now HDL operates on its own.  After DMA rx buffer is ready,
  // DAC generates its first frame and simultaneously the HDL
  // begins to save ADC samples.  The ADC fifo will fill on its own.
  
  pthread_mutex_lock(&tsd_st.lock);
  tsd_st.thread_cmd = IIO_THREAD_CMD_CAP;
  pthread_cond_signal(&tsd_st.cond);
  pthread_mutex_unlock(&tsd_st.lock);

  
  sprintf(rbuf, "0");
  return 0;
}


int cmd_tx_pilot_pm_en(int arg) {
  int en;
  if (parse_int(&en)) return CMD_ERR_NO_INT;
  printf("pilot_pm_en %d\n", en);
  qregs_set_tx_pilot_pm_en(en);
  sprintf(rbuf, "0 %d", st.tx_pilot_pm_en);
  return 0;
}

int cmd_tx_always(int arg) {
  int en;
  if (parse_int(&en)) return CMD_ERR_NO_INT;
  printf("tx_always %d\n", en);
  qregs_set_tx_always(en);
  sprintf(rbuf, "0 %d", st.tx_always);
  return 0;
}

int cmd_shutdown(int arg) {
  printf("REBOOT!\n");
  sync();
  setuid(0);
  //  reboot(LINUX_REBOOT_CMD_POWER_OFF);
  return 0;
}
int cmd_q(int arg) {
  sprintf(rbuf, "0");
  return CMD_ERR_QUIT;
}

int cmd_bob_sync(int arg) {
  qregs_txrx(arg);
  printf("bob sync\n");
  sprintf(rbuf, "0");
  return 0;
}

int cmd_qna_timo(int arg) {
  int ms;
  if (parse_int(&ms)) return CMD_ERR_NO_INT;
  if (!qna_connected)
    return err_fail("qna not connected");
  qregs_ser_set_timo_ms(ms);
  sprintf(rbuf, "0 %d", ms);
  return 0;
}








int cmd_qna(int arg) {
  char *p;
  int e;
  if (parse_next() == ' ') parse_char();
  p = parse_get_ptr();
  
  // e = qna_usb_do_cmd(p, qnarsp, 1024);
  e = qregs_ser_do_cmd(p, qnarsp, 1024,1);
  
  // printf("DBG: e %d  qrsp %s\n", e, qnarsp);
  if (e) return e;
  else   sprintf(rbuf, "0 %s", qnarsp);  
  return e;
}


//hdl_cdm_cfg_t cdm_cfg;
//hdl_loop_cfg_t loop_cfg;
//hdl_qsdc_cfg_t qsdc_cfg;
//hdl_noise_cfg_t noise_cfg;

int tsd_lcl_cdm_cfg(hdl_cdm_cfg_t *cfg_p, ssize_t *rx_buf_sz_bytes) {

  qregs_txrx(0);  // clean up just in case
  qregs_set_cdm_cfg(cfg_p, rx_buf_sz_bytes);
  return 0;
}

int tsd_lcl_cdm_go(void) {
  qregs_set_tx_same_hdrs(0);
  qregs_set_tx_pilot_pm_en(0);
  qregs_search_en(0);
  h_w_fld(H_DAC_CTL_CIPHER_EN, 0);
  h_w_fld(H_ADC_ACTL_DECIPHER_EN,0);
  h_w_fld(H_DAC_HDR_SECOND_IM_IS_PROBE,1);

  qregs_set_save_after_init(0);
  qregs_set_save_after_pwr(0);
  qregs_set_save_after_hdr(0);
  qregs_set_alice_txing(0);
  printf(" using tx_go condition %c\n", st.tx_go_condition);


  if (cdm_cfg.is_passive) {
    // set switches in QNA2,1
  }else {
    printf("    txrx 1\n");
    qregs_txrx(1);
  }
}

int tsd_lcl_cdm_stop(void) {
  qregs_txrx(0);
  h_w_fld(H_DAC_HDR_SECOND_IM_IS_PROBE,0);
}

int tsd_lcl_loop_cfg(hdl_loop_cfg_t *cfg)
{
  return 0;
}

int tsd_lcl_loop_go(void)
{
  return 0;
}

int tsd_lcl_loop_stop(int delay)
{
  return 0;
}

#define DAC_N (4095*2)


static ssize_t read_file_into_buf(char *fname, void *buf, ssize_t buf_sz) {
  int i, fd = open(fname,O_RDWR);
  short int v;
  ssize_t rd_sz;
  if (fd<0) {
    snprintf(tsd_errmsg, 512, "read_file_into_buf() cant open file %s", fname);
    printf("%s\n",tsd_errmsg);
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


int tsd_lcl_prepare_im_premphasis(int en)
{
  // loads dac samples into tx buffer in HDL to be driven
  // out to intensity modulator (IM) to counteract AC coupling
  // distortion and produce a clean pulse like:  ___-___
  
  char *pilot_preemph_fname_p;
  short int mem[DAC_N];
  size_t data_sz_samps, mem_sz, sz;
  qregs_pilot_cfg_t pilot_cfg;
  int e,i;
  lcl_iio_t *iio=&tsd_st.iio;
  
  if (!en) {
    pilot_cfg.im_from_mem = 0;
    qregs_cfg_pilot(&pilot_cfg, 0);
    return 0;
  }

  e = ini_get_string(vars_cfg_all, "qsdc_im_preemph_fname", &pilot_preemph_fname_p);
  if (e) return err_fail("missing qsdc_im_preemph_fname in cfg/ini_all.txt");
  mem_sz=read_file_into_buf(pilot_preemph_fname_p, mem, sizeof(mem));
  sz = st.frame_pd_asamps * 2;
  if (mem_sz > sz) {
    printf("WARN: file sz %zd is too long. truncating to %zd\n", mem_sz, sz);
    mem_sz = sz;
  }
      
  void *p;
  p = iio_buffer_start(iio->dac_buf);
  if (!p) return err_fail("no tx iio buffers to write into");
  memcpy(p, mem, mem_sz);
  // sz = iio_channel_write(dac_ch0, dac_buf, mem, mem_sz);
  // returned 256=DAC_N*2, makes sense
  printf("  filled dac_buf sz %zd bytes\n", mem_sz);
  
  qregs_zero_mem_raddr();

  sz = iio_device_get_sample_size(iio->dac);
  // printf("  dac samp sz %zd\n", sz);
  data_sz_samps = (int)(mem_sz/sz);
  if (data_sz_samps*sz != mem_sz)
    return err_bug("preemphasis file has a bad length");

  sz = iio_buffer_push_partial(iio->dac_buf, data_sz_samps); // supposed to ret num bytes
  if (sz<0) {
    sprintf(tsd_errmsg,"libiio push_partial returned errcode %d while writing preemphasis", sz);
    return HDL_ERR_FAIL;
  }
  printf("  pushed %zd bytes\n", sz);

  // Tell HDL how many words were written.  Actually, it already knows.
  // I just did this as a means of verification.
  i = mem_sz/8-2;
  h_w_fld(H_DAC_DMA_MEM_RADDR_LIM_MIN1, i);
  // qregs_dbg_get_info(&j);
  // printf("  DBG: set raddr lim %zd (dbg %d)\n", i, j);


  pilot_cfg.im_from_mem = 1;
  qregs_cfg_pilot(&pilot_cfg, 0);
  return 0;
}





// WONT USE THIS
#if 0
int cmd_sendfile(int arg) {
  // This was only made to speed up testing.
  // it runs as alice and sends a file to Bob.
  char data_fname[64];
  short int mem[DAC_N];
  lcl_iio_t *iio=&tsd_st.iio;
  size_t mem_sz;
  int e, tx_frame_qty;
  void *p;
    
  parse_token(data_fname, 64);
  printf("sending %s\n", data_fname);
  
  if (!iio->open) {
    e=tsd_lcl_iio_open(iio);
    if (e) return e;
  }
  
  p = iio_buffer_start(iio->dac_buf);
  if (!p) return err_fail("no tx iio buffer avail");
  
  mem_sz = read_file_into_buf(data_fname, p, iio->tx_buf_sz_bytes);
  printf("  filled dac_buf sz %zd bytes\n", mem_sz);
    
  int data_len_syms = (int)mem_sz * 8 / (st.qsdc_data_cfg.is_qpsk?2:1) *
    st.qsdc_data_cfg.bit_dur_syms;
  printf("total data len %d symbols", data_len_syms);
  int data_len_frames = (int)ceil((double)data_len_syms
				  * st.qsdc_data_cfg.symbol_len_asamps
				  / st.qsdc_data_cfg.data_len_asamps);
  printf("   =  %d frames\n", data_len_frames);


  qregs_zero_mem_raddr();

  sz = iio_device_get_sample_size(iio->dac);
  data_sz_samps = (int)(mem_sz/sz);
  tx_sz = iio_buffer_push_partial(iio->dac_buf, data_sz_samps);
  // iio_buffer_push_partial is supposed to ret num bytes
  printf("  pushed %zd bytes\n", tx_sz);

  // This is something that needs to change to do streaming
  i = mem_sz/8-2;
  h_w_fld(H_DAC_DMA_MEM_RADDR_LIM_MIN1, i);
  qregs_dbg_get_info(&j);
  printf("  DBG: set raddr lim %zd (dbg %d)\n", i, j);

  sprintf(rbuf, "0 frames=%s", data_len_frames);    
  return 0;  
}
#endif




int tsd_lcl_qsdc_cfg_rx(hdl_qsdc_cfg_t *cfg)
{
  // tells remote to recieve a file or chunk of data
  // This was only made to speed up testing.
  // Alice sends this command to Bob
  short int mem[DAC_N];
  lcl_iio_t *iio=&tsd_st.iio;
  size_t mem_sz;
  int i, e, tx_frame_qty, rx_frame_qty;
  void *p;

  printf("tsd_lcl_qsdc_cfg_rx()\n");
  
  qregs_halfduplex_is_bob(!cfg->is_alice);
  qregs_set_frame_pd_asamps(600);
  qregs_set_init();

  if (cfg->is_alice) {
    qregs_pilot_cfg_t pilot_cfg;
    pilot_cfg.im_from_mem = 0;
    qregs_cfg_pilot(&pilot_cfg, 0);
  }
  
  //  tsd_parse_key_int("frame_qty=", &rx_frame_qty);

  printf("  rxing %s\n", cfg->msg_name);
  strcpy(tsd_st.name, cfg->msg_name);
  
  if (!iio->open) {
    e=tsd_lcl_iio_open(iio);
    if (e) return e;
  }
  
  // printf("bit dur syms %d\n", st.qsdc_data_cfg.bit_dur_syms); // 10
  printf("  data len %zd bytes\n", cfg->bytes);
  // double-check what alice said
  int data_len_syms = (int)cfg->bytes * 8 / (st.qsdc_data_cfg.is_qpsk?2:1) *
    st.qsdc_data_cfg.bit_dur_syms;
  printf("  total data len %d symbols", data_len_syms);
  int data_len_frames = (int)ceil((double)data_len_syms
                                    * st.qsdc_data_cfg.symbol_len_asamps
                                    / st.qsdc_data_cfg.data_len_asamps);
  printf("   =  %d frames\n", data_len_frames);


  i = round(cfg->est_round_trip_s * st.asamp_Hz);
  printf("  est round trip = %d asamps", i);
  i = ceil((double)i/st.frame_pd_asamps); // est round asamps
  printf(" = %d frames\n", i);
  tsd_st.round_trip_frames=i;  
  
  //  if (data_len_frames != rx_frame_qty) {
  //    sprintf(tsd_errmsg,"frame qty data len mismatch");
  //    return CMD_ERR_FAIL;
  //  }

  tx_frame_qty = data_len_frames;
  rx_frame_qty = tsd_st.round_trip_frames + tx_frame_qty;

  
  // round up num frames to save, to fit into rx dma (adc) bufs.
  {
    int frames_per_iiobuf;
    int max_frames_per_buf = (int)floor(IIO_ADC_MAX_ASAMPS/st.frame_pd_asamps);
    printf("  max frames per buf %d", max_frames_per_buf);
    iio->rx_num_bufs = ceil((double)(rx_frame_qty) / max_frames_per_buf);
    printf("  so num_bufs %d per itr\n", iio->rx_num_bufs);
    frames_per_iiobuf = ceil((double)(rx_frame_qty) / iio->rx_num_bufs);
    rx_frame_qty = iio->rx_num_bufs * frames_per_iiobuf;
    if (rx_frame_qty != tx_frame_qty)
      printf("  actually SAVING %d frames per itr\n", rx_frame_qty);
    iio->rx_buf_sz_bytes = frames_per_iiobuf * st.frame_pd_asamps*4;
    printf("  rxbuf_len_bytes %zd\n", iio->rx_buf_sz_bytes);

    


  }

  

  qregs_set_meas_noise(0);
  qregs_set_use_lfsr(1);
  qregs_set_tx_always(0);
  qregs_set_save_after_init(0);
  qregs_set_save_after_pwr(0);
  qregs_set_save_after_hdr(0);

  if (cfg->is_alice) {
    qregs_sync_status_t sstat;
    qregs_get_sync_status(&sstat);
    if (!sstat.locked)
      printf("synchronizer unlocked\n");
  }

  
  qregs_search_en(0); // recover from prior crash if we need to.
  qregs_txrx_new(0,0);
  qregs_set_tx_pilot_pm_en(1);
  qregs_set_alice_syncing(0);
  qregs_set_alice_txing(0); // changes further down...

  qregs_clr_corr_status(); // for dbg
  qregs_clr_adc_status();
  qregs_clr_tx_status();


  qregs_set_tx_same_hdrs(1);

  if (!cfg->is_alice) {
    e=qregs_set_sync_ref('t');
    if (e) return cmd_qerr("cant set sync ref");
    qregs_qsdc_track_pilots(0);
    //    qregs_qsdc_track_pilots(1);  // not ready to do this yet.
    qregs_set_tx_go_condition('r'); // r=tx when rxbuf rdy
  } else {
    e=qregs_set_sync_ref('r');
    qregs_set_tx_go_condition('h'); // h=tx when see hdr

  }
  // if alice, should be set to rxclk

  qregs_set_tx_go_condition('r'); // r=tx when rxbuf rdy
  printf(" using tx_go condition %c\n", st.tx_go_condition);



  qregs_set_frame_qty(tx_frame_qty);

  if (st.frame_qty !=tx_frame_qty) { // a bug?
    sprintf(tsd_errmsg,"actually tx frame qty %d not %d\n",
	   st.frame_qty, tx_frame_qty);
    return CMD_ERR_FAIL;
  }


  // DONE: make cipher prime NOT be dependent on cipher_en
  // going low.  Then we can just keep it high all the time.
  qregs_qsdc_other_cfg_t oth={0};
  lookup_int("cipher_en",   &oth.cipher_en);
  lookup_int("decipher_en", &oth.decipher_en);
  printf("cipher_en %d  \t decipher_en %d\n",
	   oth.cipher_en, oth.decipher_en);

  oth.stream = 0; // for now
  oth.m=2;
  oth.dbg_cipher_same=0;
  oth.cipher_symlen_asamps = st.osamp;
  //  qsdc_cfg.is_bob = !is_alice;
  qregs_qsdc_other_cfg(&oth);

  // do I need this to reset IM preemph?
  qregs_zero_mem_raddr();
  
  printf("new thing, bob resync for syncref t\n");
  qregs_sync_resync();

  sprintf(rbuf, "");
  return 0;  
}



int tsd_lcl_qsdc_cfg(hdl_qsdc_cfg_t *cfg)
{
  if (cfg->do_tx)
    return CMD_ERR_FAIL;
  return tsd_lcl_qsdc_cfg_rx(cfg);
}



int tsd_lcl_qsdc_go(void)
{
  lcl_iio_t *iio=&tsd_st.iio;
  int e;
  // On alice, currently this must be done after pushing the dma data,
  // because it primes qsdc.
  // again, HDL needs to change so we don't have to do this.
  if (st.is_bob) {
    e= tsd_iio_create_rxbuf(iio);
    if (e) return tsd_err(HDL_ERR_FAIL, "iio cant create rxbuf");
  }
  printf("tx 1  rx 1\n");
  qregs_txrx_new(1,1);
  return 0;
}


int tsd_lcl_qsdc_stop(void)
{
  int k, e=0, e1;
  if (!st.is_bob) {

    
    for(k=0;k<100*30;++k) {
      if (h_r_fld(H_DAC_STATUS_QSDC_DATA_DONE)) break;
      usleep(10);
    }
    if (k>=100*30) {
      e=HDL_ERR_TIMO;
    }
  }
  qregs_txrx_new(0,0);
  qregs_set_alice_txing(0);
  tsd_iio_destroy_rxbuf(&tsd_st.iio);
  
  // TODO: do I need this:???
  // lcl_iio_destroy_dac_bufs(&tsd_st.iio);
  
  return e;
}




int tsd_lcl_noise_cfg(hdl_noise_cfg_t *cfg)
{
  return 0;
}

int tsd_lcl_noise_go(void)
{
  return 0;
}

int tsd_lcl_noise_stop(void)
{
  return 0;
}

int cmd_cdm_cfg(int arg) {
  ssize_t rx_buf_sz_bytes;
  printf("\nCMD: cdm cfg\n");
  DO(tsd_parse_key_int("is_passive=", &cdm_cfg.is_passive));
  DO(tsd_parse_key_int("is_wdm=",     &cdm_cfg.is_wdm));
  

  DO(tsd_parse_key_int("sym_len=",   &cdm_cfg.sym_len_asamps));
  DO(tsd_parse_key_int("probe_len=", &cdm_cfg.probe_len_asamps));
  DO(tsd_parse_key_int("frame_pd=",  &cdm_cfg.frame_pd_asamps));  
  DO(tsd_parse_key_int("num_iter=",  &cdm_cfg.num_iter));
  tsd_lcl_cdm_cfg(&cdm_cfg, &rx_buf_sz_bytes);
  
  sprintf(rbuf, "0 is_passive=%d is_wdn=%d sym_len=%d probe_len=%d frame_pd=%d num_iter=%d  rx_bytes=%zd", cdm_cfg.is_passive,  cdm_cfg.is_wdm,
	  cdm_cfg.sym_len_asamps,  cdm_cfg.probe_len_asamps,
	  cdm_cfg.frame_pd_asamps, cdm_cfg.num_iter,
	  rx_buf_sz_bytes);
  return 0;
}

int cmd_cdm_go(int arg) {
  printf("\nCMD: cdm go\n");
  tsd_lcl_cdm_go();
  tsd_st.mode='c';
  return 0;
}

int cmd_cdm_stop(int arg) {
  printf("\nCDM: cdm stop\n");
  
}

int cmd_loop_cfg(int arg)
{
  printf("\nCMD: loop cfg\n");
  tsd_lcl_loop_cfg(&loop_cfg);
  return 0;
}

int cmd_loop_go(int arg)
{
  printf("\nCMD: loop go\n");
  tsd_lcl_loop_go();
  return 0;
}

int cmd_loop_stop(int arg)
{
  printf("\nCMD: loop stop\n");
  int delay; // delay in number of samples
  DO(tsd_parse_key_int("delay=", &delay));
  tsd_lcl_loop_stop(delay);
  return 0;
}

int cmd_qsdc_cfg(int arg)
{
  // configuress ZCU to either tx or rx

  // params:
  //   is_alice  int
  //   do_rx     int
  //   bytes     int
  //   rnd_trip  double seconds
  int i, e;
  hdl_qsdc_cfg_t cfg;
  printf("\nCMD: qsdc cfg\n");
  DO(tsd_parse_key_int("is_alice=", &cfg.is_alice));
  DO(tsd_parse_key_int("bytes=", &i)); // could be cell len
  cfg.bytes=i;
  DO(tsd_parse_key_int("do_tx=", &cfg.do_tx));
  //  DO(tsd_parse_key_int("frame_qty=", &rx_frame_qty));
  // TODO: FIX!!  parsing DID NOT WORK
  //  DO(tsd_parse_key_dbl("rnd_trip=", &cfg.est_round_trip_s));
  cfg.est_round_trip_s = 64.0e-6;
  
  DO(tsd_parse_key_str("name=", cfg.msg_name, 64));
  e = tsd_lcl_qsdc_cfg(&cfg);
  printf("cfg over, e=%d\n", e);
  sprintf(rbuf, "%d", e);
  tsd_st.mode='q';
  return e;
}


int cmd_qsdc_go(int arg)
{
  int e;
  lcl_iio_t *iio=&tsd_st.iio;  
  printf("\nCMD: qsdc go\n");
  if (tsd_st.mode!='q') return CMD_ERR_FAIL;
    

  e=tsd_lcl_qsdc_go();
  if (e) return cmd_err_fail(tsd_get_last_errmsg());
  
  if (tsd_st.data_handler_fn) {

    e=tsd_iio_forward(iio);
    if (e) {
      printf("fwd failed\n");
      return cmd_err_fail(tsd_get_last_errmsg());
    }
    
  }else {
    e=tsd_iio_cap(iio);
    if (e) return cmd_err_fail(tsd_get_last_errmsg());
  }
  sprintf(rbuf, "%d", e);
  return 0;
}


int cmd_qsdc_stop(int arg)
{
  int e;
  printf("\nCMD: qsdc stop\n");
  if (tsd_st.mode!='q') cmd_err_fail("illegal in mode");
  
  e=tsd_lcl_qsdc_stop();
  if (e) return cmd_err_fail(tsd_get_last_errmsg());

  
  tsd_st.mode=' ';
  sprintf(rbuf, "%d", e);
  return 0;
}



int cmd_noise_cfg(int arg)
{
  printf("\nCMD: noise cfg\n");
  tsd_lcl_noise_cfg(&noise_cfg);
  tsd_st.mode='n';
  return 0;
}

int cmd_noise_go(int arg)
{
  printf("\nCMD: noise go\n");
  tsd_lcl_noise_go();
  return 0;
}

int cmd_noise_stop(int arg)
{
  printf("\nCMD: noise stop\n");
  tsd_lcl_noise_stop();
  return 0;
}

cmd_info_t cdm_cmds_info[]={
  {"cfg",    cmd_cdm_cfg,   0, 0},
  {"go",     cmd_cdm_go,      0, 0},
  {"stop",   cmd_cdm_stop,    0, 0},
  {0}};

cmd_info_t loop_cmds_info[]={
  {"cfg",    cmd_loop_cfg,   0, 0},
  {"go",     cmd_loop_go,      0, 0},
  {"stop",   cmd_loop_stop,    0, 0},
  {0}};

cmd_info_t qsdc_cmds_info[]={
  {"cfg",    cmd_qsdc_cfg,   0, 0},
  {"go",     cmd_qsdc_go,      0, 0},
  {"stop",   cmd_qsdc_stop,    0, 0},
  {0}};

cmd_info_t qsdc_noise_info[]={
  {"cfg",    cmd_noise_cfg,   0, 0},
  {"go",     cmd_noise_go,      0, 0},
  {"stop",   cmd_noise_stop,    0, 0},
  {0}};

cmd_info_t cmds_info[]={
  {"bob_sync",  cmd_bob_sync,  0, 0},
  {"iioopen",   cmd_iioopen,   0, 0},
  //  {"qna",       cmd_qna,       0, 0},
  //  {"qna_timo",  cmd_qna_timo,  0, 0},
  {"cdm",       cmd_subcmd,  cdm_cmds_info, 0},
  {"loop",	cmd_subcmd, loop_cmds_info, 0 },
  {"tx",        cmd_tx,        0, 0},
  {"txrx",      cmd_txrx,      0, 0},
  {"tx_always", cmd_tx_always, 0, 0},
  {"pilot_pm_en",  cmd_tx_pilot_pm_en,    0, 0},
  {"qsdc",	cmd_subcmd,  qsdc_cmds_info, 0},
  {"frame_pd",  cmd_frame_pd,  0, 0},
  {"frame_qty", cmd_frame_qty, 0, 0},
  {"fwver",     cmd_fwver,     0, 0},
  {"osamp",     cmd_osamp,     0, 0},
  {"hdr_len",   cmd_hdr_len,   0, 0},
  {"q",         cmd_q,         0, 0},
  {"shutdown",  cmd_shutdown,  0, 0},
  {0}};

void handle(int soc) {
// This is the HDL server  
  char cmd_str[512];
  int l, i, n, done=0, e;
  // printf("\nopened\n");
  while(!done) {
    tsd_errmsg[0]=0;
    l = tsd_rd_pkt(soc, cmd_str, 255);
    if (!l) {
      printf("WARN: abnormal close (client did not issue q)\n");
      break;
    }
    // printf("rxed %d bytes\n", l);
    cmd_str[l]=0;
    //  printf("rx: %s\n", tok);
    e = cmd_exec(cmd_str, cmds_info);
    if (e && (e!=CMD_ERR_QUIT))
      sprintf(rbuf, "%d %s", e, tsd_errmsg);
    
    printf("RESPONDING WITH: %s\n", rbuf);
    
    e = tsd_wr_str(soc, rbuf);
    if (e==CMD_ERR_QUIT) break;
    if (e) {
      printf("ERR: %s\n", tsd_errmsg);
      //      printf("WARN: tried to write rsp:\n");
      //      u_print_all(rbuf);
      //      printf("  which is %d bytes, but actually wrote %d\n",
      //	     strlen(rbuf), l);
    }
    if (e)
      printf("WARN: cmd '%s' returning err %d\n", cmd_str, e);
  }
  printf("client disconnected\n");
  // how to I wait for socket to finish?
  sleep(1);
}


void show_ipaddr(void) {
  int e, fam;
  struct ifaddrs *ifa, *p;
  char host[NI_MAXHOST];
  e = getifaddrs(&ifa);
  if (e) printf("getifaddrs failed");
  p=ifa;
  while (p) {
    // ignore lo
    if (strcmp(p->ifa_name,"lo")&&(p->ifa_addr)) {
      fam = p->ifa_addr->sa_family;
      if (fam == AF_INET) {
	struct sockaddr_in *pa = (struct sockaddr_in *)p->ifa_addr;
	printf("%s  %s\n", p->ifa_name, inet_ntoa(pa->sin_addr));
      }
    }
    p=p->ifa_next;
  }
  freeifaddrs(ifa);
}

/*
int err_qna(char *s, int e) {
  // this is for qna_usb
  // printf("ERR: %s\n", s);
  sprintf(errmsg, s);
  return e;
}
*/




int tsd_serve(void) {
  int l_soc, c_soc, e, i;
  struct sockaddr_in srvr_addr;

  printf("qregd\n");
  printf("the demon that accesses quanet_regs\n");

  
  l_soc = socket(AF_INET, SOCK_STREAM, 0);
  if (l_soc<0) return err_fail("cant make socket to listen on ");

  e = setsockopt(l_soc, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
  if (e<0) return err_fail("cant set sockopt");
  
  // e = qna_usb_connect("/dev/ttyUSB1", &err_qna);
  //  e = qregs_ser_qna_connect(rbuf, 1024);
  //  if (e)
  //    printf("*\n* ERR: %s\n*\n", errmsg);
  //  else qna_connected=1;
  
  memset((void *)&srvr_addr, 0, sizeof(srvr_addr));
  srvr_addr.sin_family = AF_INET;
  srvr_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  srvr_addr.sin_port = htons(QREGD_PORT);
  e =bind(l_soc, (struct sockaddr*)&srvr_addr, sizeof(srvr_addr));
  if (e<0) return err_fail("bind fialed");

  e = listen(l_soc, 2);
  if (e<0) return err_fail("listen fialed");
  
  printf("HDL server listening on ");
  show_ipaddr();  
  printf("   port %d\n", QREGD_PORT);
  
  
  while (1) {
    c_soc = accept(l_soc, (struct sockaddr *)NULL, 0);
    if (c_soc<0) return err_fail("cant accept");
    printf("  client accepted\n");
    handle(c_soc);
  }

  
  return 0;
}




