// This does not test QNICLL or QREGD.
// rather, it tests libiio locally.


#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include <errno.h>
#include <iio.h>
#include "cmd.h"
#include "parse.h"
#include "qregs.h"
#include "qregs_ll.h"

#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <ifaddrs.h>


// maybe should not call rp directly
#include "rp.h"

#include <poll.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "corr.h"
#include <time.h>
#include <ctype.h>
#include "ini.h"
#include "h_vhdl_extract.h"
#include "qna.h"
#include "i2c.h"
#include "h.h"
#include "util.h"
#include <sys/reboot.h>


cmd_info_t cmds_info[];

#define QREGC_LINKED (0)

#if QREGC_LINKED
#include "qregc.h"

// qregc level code calls this
static int remote_err_handler(char *str, int err) {
  printf("REMOTE ERR: %s\n", str);
  //  printf("BUG: st.errmsg_ll %s\n", st.errmsg_ll);
  return err;
}

#endif

char errmsg[256];
void err(char *str) {
  printf("ERR: %s\n", str);
  if (errno) {
    printf("  errno %d\n", errno);
    perror("");
  }
  exit(1);
}

int cmd_err_syntax(char *msg) {
  printf("ERR: %s\n", msg);
  return CMD_ERR_SYNTAX;
}
int cmd_err_fail(char *msg) {
  printf("ERR: %s\n", msg);
  return CMD_ERR_FAIL;
}
int qerr(char *msg) {
  printf("ERR: %s\n", qregs_last_errmsg());
  printf("     %s\n", msg);
  return CMD_ERR_FAIL;
}




ini_val_t *tvars;
int do_shutdown=0;

/*
int cmd_cal(int arg) {
  int en;
  if (parse_int(&en)) return CMD_ERR_NO_INT;
  qregs_calibrate_bpsk(en);
  printf("calbpsk %d\n", en);
  return 0;
}
*/





int cmd_set(int arg) {
  qregs_print_settings();
  return 0;
}

int cmd_pwr(int arg) {
  int e;
  qregs_frame_pwrs_t pwrs;
  char c=parse_nonspace();
  if (c==0) {
    e= qregs_measure_frame_pwrs(&pwrs);
    if (e) {printf("err: no rsp from RP?\n");  return CMD_ERR_FAIL;}
    printf("  dark      %.3f V\n",  pwrs.dark_pwr_V);
    printf("  body      %.3f V\n",  pwrs.body_pwr_V);
    printf("  hdr/body  %5.1f dB\n", pwrs.ext_rat_dB);
    printf("  body/mean %5.1f dB\n", pwrs.body_rat_dB);
  }else {
    while(1) {
      e= qregs_measure_frame_pwrs(&pwrs);
      if (e) {printf("err: no rsp from RP?\n");  break;}
      printf("  ext  %4.1f dB    b/m %4.1f dB   body %7.3f V\n",
	     pwrs.ext_rat_dB, pwrs.body_rat_dB, pwrs.body_pwr_V);
      usleep(1000*500);
    }
  }
  return 0;
}

int cmd_dbg_pminv(int arg) {
  int i;
  if (parse_int(&i)) return CMD_ERR_SYNTAX;
  h_w_fld(H_DAC_CTL_ALICE_TXING, 0);
  h_w_fld(H_DAC_ALICE_PM_INVERT, !!i);
  return 0;
}

int cmd_dbg_clksel(int arg) {
  int i;
  if (parse_int(&i)) return CMD_ERR_SYNTAX;
  qregs_set_dbg_clk_sel(i);
  return 0;
}

int cmd_dbg_ser(int arg) {
  int i;
  //  printf("dbg ser\n");
  qregs_print_dbg_ser();
  //  if (parse_int(&i)) return CMD_ERR_SYNTAX;
  //  qregs_ser_sel(i);
  //  qregs_ser_tx('A');
  return 0;
}

int cmd_dbg_pwr(int arg) {
  int pwr_avg, pwr_max, pwr_cnt, n;
  if (parse_int(&n)) n=-1;
  printf("pwr_avg pwr_max pwr_cnt\n");
  while(n) {
    qregs_get_avgpwr(&pwr_avg, &pwr_max, &pwr_cnt);
    printf("%6d  %6d  %6d", pwr_avg, pwr_max, pwr_cnt);

    if(pwr_cnt)
      printf("  %6.1lf", (double)pwr_avg*1000/pwr_cnt);
    if (n>0) --n;
    if (!n) break;
    printf("\n");
    usleep(1000*200);
  }
  return 0;
}

int cmd_sweep(int arg) {
  int th, th_sav, mx=256, step=10;
  int pwr_avg, pwr_max, pwr_cnt, z=0;
  th_sav = st.hdr_pwr_thresh;
  printf("pwr_th pwr_cnt\n");
  parse_int(&mx);
  
  if (parse_int(&step))
    step = mx/24;
  
  for(th=0;th<mx; th+=step) {
    qregs_set_hdr_det_thresh(th, st.hdr_corr_thresh);
    usleep(210);
    qregs_get_avgpwr(&pwr_avg, &pwr_max, &pwr_cnt);
    printf("%d %d\n", th, pwr_cnt);
  }
  qregs_set_hdr_det_thresh(th_sav, st.hdr_corr_thresh);
  return 0;
}

int cmd_always(int arg) {
  int en;
  if (parse_int(&en))
    return CMD_ERR_NO_INT;
  qregs_set_tx_always(en);
  printf("%d\n", st.tx_always);
  return 0;
}

int cmd_twopi(int arg) {
  int en;
  if (parse_int(&en))
    return CMD_ERR_NO_INT;
  qregs_set_tx_hdr_twopi(en);
  printf("%d\n", st.tx_hdr_twopi);
  return 0;
}

int cmd_ver(int arg) {
  int en;
  printf("version %d\n", st.ver_info.quanet_dac_fwver);
  printf("txfifo addr_w %d\n", st.ver_info.tx_mem_addr_w);
  printf("       size %d bytes\n", (1<<st.ver_info.tx_mem_addr_w)*2);
  return 0;
}


int cmd_circ(int arg) {
  int en;
  if (parse_int(&en))
    return CMD_ERR_NO_INT;
  qregs_set_tx_mem_circ(en);
  qregs_set_memtx_to_pm(en);
  printf("%d\n", en);
  return 0;
}



int cmd_rst(int arg) {
  qregs_txrx(0);
  qregs_set_tx_always(0);
  qregs_search_en(0);
  qregs_set_tx_mem_circ(0);
  qregs_set_memtx_to_pm(0);
  qregs_set_tx_pilot_pm_en(1);
  h_w_fld(H_DAC_CTL_ALICE_SYNCING, 0);
  h_w_fld(H_DAC_HDR_SECOND_IM_IS_PROBE, 0);  
  printf("rst\n");
  return 0;
}


int cmd_shutdown(int arg) {
  if (!u_ask_yn("really shutdown",-1)) return 0;
  if (qregs_rp_shutdown()) {
    printf("ERR: cant shut down Red Pitaya");
    return CMD_ERR_FAIL;
  }
  do_shutdown=1;
  return 0;
}
int cmd_dbg_rpinfo(int arg) {
  char str[512];
  if (qregs_rp_info(str, 512)) {
    qregs_print_last_err();
    printf("ERR: cant get info from Red Pitaya");
    return CMD_ERR_FAIL;
  }else
    printf("%s",str);
  return 0;
}

int cmd_cal_norebal(int arg) {
  qregs_rebalance_params_t rebal={0};
  rebal.m11=1;
  rebal.m22=1;
  qregs_set_iq_rebalance(&rebal);
  printf("IQ imbalance correction turned off\n");
  return 0;
}

int cmd_cal_measdark(int arg) {
  if (rp_measdark())
    return CMD_ERR_FAIL;
  return 0;
}

int cmd_dbg_rpreboot(int arg) {
  if (!u_ask_yn("really reboot Red Pitaya",-1)) return 0;
  if (rp_reboot()) {
    printf("ERR: reboot Red Pitaya");
    return CMD_ERR_FAIL;
  }
  return 0;
}




int cmd_stat(int arg) {
  printf("\nstatus\n");
    //  qregs_print_adc_status();
  //  printf("  search %d\n", qregs_r_fld(H_ADC_ACTL_SEARCH));
  //  printf("  calbpsk %d\n", qregs_r_fld(H_DAC_CTL_BPSK_CALIBRATE));
  //  printf("\n");
  qregs_print_hdr_det_status();
  return 0;
}


int cmd_proto(int arg) {
  double d;
  int i, j,k;
  
  d = ini_ask_num(tvars, "osamp (1,2,4)", "osamp", 4);
  qregs_set_osamp(d);
  // printf("osamps %d\n", st.osamp);


  d = ini_ask_num(tvars, "frame_pd (us)", "frame_pd_us", 1);
  i = qregs_dur_us2samps(d);
  // printf("adc samp freq %lg Hz\n", st.asamp_Hz);
  qregs_set_frame_pd_asamps(i);
  printf("frame_pd_asamps %d = %.3f us\n", st.frame_pd_asamps,
	   qregs_dur_samps2us(st.frame_pd_asamps));

    //  if (qregs_done()) err("qregs_done fail");  
    //  return 0;

  
    //  qregs_set_sync_dly_asamps(-346);

  i=32;  
  i = ini_ask_num(tvars,"length of pilot (bits)", "hdr_len_bits", i);
  qregs_set_hdr_len_bits(i);
  printf("pilot_len_bits %d = %.2f ns\n", st.hdr_len_bits,
	   qregs_dur_samps2us(st.hdr_len_bits*st.osamp)*1000);
  printf("body_len_asamps %d\n", st.body_len_asamps);


  qregs_qsdc_other_cfg_t oth ={0};
  oth.cipher_en   = ini_ask_yn(tvars,"cipher_en", "cipher_en", 0);
  oth.decipher_en = ini_ask_yn(tvars,"decipher_en", "decipher_en", 0);
  oth.m = ini_ask_num(tvars, "cipher_m (for m-psk)", "cipher_m", 2);
  oth.stream = 0;
  oth.dbg_cipher_same=0;
  qregs_qsdc_other_cfg(&oth);
  //  qregs_set_cipher_en(i, st.osamp, k, j);

  
  
  if (st.osamp!=st.cipher_symlen_asamps)
    printf("  actually symlen = %d\n", st.cipher_symlen_asamps);
  if (j!=st.cipher_m)
    printf("  actually m = %d\n", st.cipher_m);

  // added this frame size stuff into a common init file
  //  read by u.c.  add an interactive cfg thing to u.c

  double gap_ns;
  qregs_qsdc_data_cfg_t data_cfg;
  printf("QSDC stuff:\n"); 
  data_cfg.is_qpsk = ini_ask_yn(tvars, "is the data qpsk","body_is_qpsk", 0);

  data_cfg.symbol_len_asamps = ini_ask_num(tvars, "data symbol len (asamps)",
					   "symbol_len_asamps", 8);

  gap_ns = ini_ask_num(tvars, "gap after pilot (ns)", "post_hdr_gap_ns", 100);
  i = round(gap_ns * 1.0e-9 * st.asamp_Hz) + st.hdr_len_asamps;
  i = round(i/4)*4;
  j = i-st.hdr_len_asamps;
  printf("rounded to %d asamps = %.1f ns\n", j, j/st.asamp_Hz*1.0e9);
  data_cfg.pos_asamps   = i;

  gap_ns = ini_ask_num(tvars, "gap at end (ns)", "post_body_gap_ns", 100);
  i= round(gap_ns * 1e-9 * st.asamp_Hz);
  i = (st.frame_pd_asamps - i - data_cfg.pos_asamps);
  i = floor(i/data_cfg.symbol_len_asamps)*data_cfg.symbol_len_asamps;
  data_cfg.data_len_asamps = i; // per frame


  data_cfg.bit_dur_syms = ini_ask_num(tvars,"  duration of one bit (symbols)",
				       "qsdc_bit_dur_sym", 100);

  qregs_set_qsdc_data_cfg(&data_cfg);

  i = st.qsdc_data_cfg.data_len_asamps;
  printf("  data len %d asamps = %.1f ns per frame\n", i, i/st.asamp_Hz*1.0e9);
  printf("  bit duration ACTUALLY %d symbols\n", st.qsdc_data_cfg.bit_dur_syms);

  printf("  bits per frame %.3lf\n", (double)i/st.qsdc_data_cfg.bit_dur_syms/
	 st.qsdc_data_cfg.symbol_len_asamps);

    
  
}


int cmd_thresh(int arg) {
  int init_th, pwr_th, corr_th, e;
  if (parse_int(&pwr_th)) {
    pwr_th = ini_ask_num(tvars, "  power threshold for probe/pilot detection",
		  "hdr_pwr_thresh", 100);
    corr_th = ini_ask_num(tvars, "  correlation threshold for probe/pilot detection",
		  "hdr_corr_thresh", 40);
    qregs_set_hdr_det_thresh(pwr_th, corr_th);


    init_th = ini_ask_num(tvars, "  initial pwr req (for debug. enter 0 if unknown)",
		  "init_pwr_thresh", 0);
    qregs_dbg_set_init_pwr(init_th);
  }else {
    
    if (parse_int(&corr_th)) return CMD_ERR_FAIL;
    qregs_set_hdr_det_thresh(pwr_th, corr_th);

    if (!parse_int(&init_th)) {
      qregs_dbg_set_init_pwr(init_th);      
    }
  }
  printf("\nthreshs:  pwr %d  corr %d   (init %d  for dbg)\n",
	 st.hdr_pwr_thresh, st.hdr_corr_thresh, st.init_pwr_thresh);
  return 0;
}



int cmd_rx(int arg) {
  char c;
  while(1) {
    if (qregs_ser_rx(&c)){
      printf("timo\n");
      break;
    }
    printf("%c", c);
    if (c=='>') break;
  }
  printf("\n");
  return 0;
}

int cmd_dly_round(int arg) {
  int dly;
  if (parse_int(&dly)) {
    dly = ini_ask_num(tvars, "round_trip_dly (asamps)",
		      "round_trip_dly", st.round_trip_asamps);
  }
  qregs_set_round_trip_asamps(dly);
  printf("round_trip_asamps %d\n", st.round_trip_asamps);
  return 0;
}

int cmd_dly_rxsc(int arg) {
  int dly;
  if (parse_int(&dly))
    return CMD_ERR_NO_INT;
  qregs_set_rx_subcyc_dly_asamps(dly);
  printf("rx_subcyc_dly %d (asamps)\n", st.rx_subcyc_dly_asamps);
  return 0;
}
 
int cmd_tx(int arg) {
  char c;
  parse_char();
  qregs_ser_sel(QREGS_SER_SEL_RP);
  while((c=parse_char()))
    if (qregs_ser_tx(c))
      printf("ERR: tx would block\n");
  qregs_ser_tx('\r');
  printf("now rx\n");
  if (qregs_ser_tx(13))
      printf("ERR: tx would block\n");
  cmd_rx(0);
}

int cmd_rp(int arg) {
  char c;
  parse_char();
  qregs_ser_flush();
  qregs_ser_sel(QREGS_SER_SEL_RP);
  while((c=parse_char()))
    if (qregs_ser_tx(c))
      printf("ERR: tx would block\n");
  if (qregs_ser_tx('\n'))
    printf("ERR: tx would block\n");
  printf("now rx:\n");
  cmd_rx(0);
  return 0;
}


char *ini_fname;
void get_ini_int(ini_val_t *ivars, char *varname, int *d) {
  int e;
  errno=0;
  e = ini_get_int(ivars, varname, d);
  sprintf(errmsg,"no variable named %s in file %s", varname, ini_fname);
  if (e) err(errmsg);
}
void get_ini_double(ini_val_t *ivars, char *varname, double *d) {
  int e;
  errno=0;
  e = ini_get_double(ivars, varname, d);
  sprintf(errmsg,"no variable named %s of double in file %s", varname, ini_fname);
  if (e) err(errmsg);
}

int cmd_sfp_status(int arg) {
  int e, lol;
  qregs_sfp_gth_status();
  e=i2c_get_si5328_lol(&lol);
  printf("si5328_lol x%x\n", lol);
  return 0;
}




int sfp_init(char *fname, int verbose) {
  int e, i, lol;
  e = i2c_program(fname, verbose);
  if (verbose)
    printf("wrote %s\n", fname);
  for(i=1;i<30;++i) {
    usleep(500000);
    e=i2c_get_si5328_lol(&lol);
    if (verbose)
      printf("  si5328_lol x%x\n", lol);
    if (e) return e;
    if (!lol) break;
  }
  usleep(100000);  
  qregs_sfp_gth_rst();
  if (verbose)
    cmd_sfp_status(0);
  return e;
}


int cmd_init(int arg) {
  int e;
  double d;
  char *str_p, fname[64];	
  int i;
  ini_val_t *ivars;

  strcpy(fname, "cfg/ini_all.txt");
  ini_fname=fname;
  e = ini_read(fname, &ivars);
  if (e) {
    printf("err %d\n", e);
    printf("%s\n", ini_err_msg());
    return CMD_ERR_FAIL;
  }

  // I think second zhl amp is inverting
  h_w_fld(H_DAC_ALICE_PM_INVERT, 0);

  get_ini_int(ivars, "ser_baud_Hz", &i);
  st.ser_state.baud_Hz       = i;
  get_ini_int(ivars, "ser_parity",  &i);
  st.ser_state.parity      = i;
  get_ini_int(ivars, "ser_xon_xoff_en", &i);
  st.ser_state.xon_xoff_en = i;
  qregs_ser_set_params(&st.ser_state.baud_Hz,
                       st.ser_state.parity ,
                       st.ser_state.xon_xoff_en);

  get_ini_int(ivars,"lfsr_rst_st", &i);
  qregs_set_lfsr_rst_st(i);
  get_ini_int(ivars,"use_lfsr", &i);
  qregs_set_use_lfsr(i);
  
  e=ini_get_string(ivars,"sfp_init", &str_p);
  if (e || !*str_p) {
    printf("WARN: no sfp_init string in file %s", fname);
    printf("      not initialzing si5328 or the SFP\n");
  }else {
    e=sfp_init(str_p, 0);
    if (e) err("sfp init failed");
    printf("initialized SFP reference from %s\n", str_p);
  }
  
  // describe QSDC frames
  get_ini_int(ivars,"osamp", &i);
  qregs_set_osamp(i);  
  get_ini_int(ivars,"frame_pd_asamps", &i);
  qregs_set_frame_pd_asamps(i);
  if (i!=st.frame_pd_asamps)
    printf("WARN: frame pd actually %d asamps not %d\n", st.frame_pd_asamps, i);
  get_ini_int(ivars,"hdr_len_bits", &i);
  qregs_set_hdr_len_bits(i);

  // Bob's IM modulations
  qregs_pilot_cfg_t pilot_cfg;
  get_ini_int(ivars,"qsdc_im_simple_pilot_dac", &pilot_cfg.im_simple_pilot_dac);
  get_ini_int(ivars,"qsdc_im_simple_body_dac", &pilot_cfg.im_simple_body_dac);
  get_ini_int(ivars,"qsdc_im_from_mem", &pilot_cfg.im_from_mem);
  qregs_cfg_pilot(&pilot_cfg,0);

  
  // describe how Alice inserts data
  qregs_qsdc_data_cfg_t data_cfg;
  get_ini_int(ivars,"qsdc_symbol_len_asamps", &data_cfg.symbol_len_asamps);
  get_ini_int(ivars,"qsdc_data_is_qpsk", &data_cfg.is_qpsk);
  get_ini_int(ivars,"qsdc_data_len_asamps", &data_cfg.data_len_asamps);
  get_ini_int(ivars,"qsdc_data_pos_asamps", &data_cfg.pos_asamps);
  get_ini_int(ivars,"qsdc_data_bit_dur_syms", &data_cfg.bit_dur_syms);
  qregs_set_qsdc_data_cfg(&data_cfg);

  

  e = ini_get_string(ivars,"sync_ref", &str_p);
  if (e) err("no sync_ref");
  printf("DBG: set sync ref %s\n", str_p); 
  e=qregs_set_sync_ref(str_p[0]);
  if (e) err("cant set sync ref");
  
  ini_free(ivars);
  printf("initialized qregs from %s\n", fname);
  
  
  strcpy(fname,"cfg/ini_");
  gethostname(fname+strlen(fname), sizeof(fname)-strlen(fname));
  fname[63]=0;
  strcat(fname, ".txt");
  
  e =  ini_read(fname, &ivars);
  if (e) {
    printf("err %d\n", e);
    printf("%s\n", ini_err_msg());
    return CMD_ERR_FAIL;
  }

  //  e = ini_get_string(ivars, "hostname", &str_p);
  //  if (strcmp(hostname, str_p)) {
  //    printf("hostname %s does not match file (%s)\n", hostname, str_p);
  //    return CMD_ERR_FAIL;
  //  }
  



  qregs_rebalance_params_t rebal={0};
  e=ini_get_string(ivars,"rebal_fname", &str_p);
  if (e || !*str_p) {
    printf("WARN: no rebal_fname variable in file %s\n", fname);
    printf("      there will be no effective IQ rebalancing\n");
    rebal.m11=1;
    rebal.m22=1;
  }else {
    ini_val_t *rbvars;
    double i_fact, q_fact, ang;
    e =  ini_read(str_p, &rbvars);
    if (e) {
      printf("err %d\n", e);
      printf("%s\n", ini_err_msg());
      return CMD_ERR_FAIL;
    }
    get_ini_int(rbvars, "i_off", &rebal.i_off);
    get_ini_int(rbvars,"q_off", &rebal.q_off);
    get_ini_double(rbvars,"i_fact", &i_fact);
    get_ini_double(rbvars,"q_fact", &q_fact);
    get_ini_double(rbvars,"ang_deg", &ang);
    ang = ang*M_PI/180;
    rebal.m11 = rebal.m22 = cos(ang);
    rebal.m21 = rebal.m12 = sin(ang);
    rebal.m11 *= i_fact;
    rebal.m12 *= i_fact;
    rebal.m21 *= -q_fact;
    rebal.m22 *= q_fact;
    if (e) err("sfp init failed");
      ini_free(rbvars);
    printf("using %s\n", str_p);
  }
  qregs_set_iq_rebalance(&rebal);

  e = ini_get_int(ivars,"pm_delay_cycs", &i);
  if (!e) qregs_set_pm_dly_cycs(i);

  ini_free(ivars);


  
  // For convenience, restore prior round trip dly setting
  e = ini_get_int(tvars,"round_trip_dly", &i);
  if (!e) qregs_set_round_trip_asamps(i);
  // For convenience, restore prior thresholds
  {  int init_th=0, pwr_th=0, corr_th=0, e;
     ini_get_int(tvars, "hdr_pwr_thresh", &pwr_th);
     ini_get_int(tvars, "hdr_corr_thresh", &corr_th);
     qregs_set_hdr_det_thresh(pwr_th, corr_th);
     e=ini_get_int(tvars, "init_pwr_thresh", &init_th);
     if (!e) qregs_dbg_set_init_pwr(init_th);
  }



  printf("initialized qregs from %s\n", fname);
  return 0;
}

void chan_en(struct iio_channel *ch) {
  iio_channel_enable(ch);
  if (!iio_channel_is_enabled(ch))
    err("chan not enabled");
}

int cmd_dbg_regs(int arg) {
  qregs_dbg_print_regs();
  return 0;
}

int cmd_dbg_info(int arg) {
  int i;

  //  printf("set txgocond\n");
  //  qregs_set_tx_go_condition('i'); // r=tx when rxbuf rdy
  //  h_w_fld(H_ADC_DBG_HOLD, 1);

  return 0;
}


int cmd_pm_sin(int arg) {
  int n, k, i, j, mx=0;
  int npds;
  size_t mem_sz, sz, dac_buf_sz, tx_sz;
  double th, pd_ns;
  struct iio_context *ctx;
  struct iio_device *dac;
  struct iio_channel *dac_ch0, *dac_ch1;    
  struct iio_buffer *dac_buf;
  short int *mem;  
    
  if (parse_double(&pd_ns)) return CMD_ERR_NO_INT;
  
  ctx = iio_create_local_context();
  if (!ctx)
    err("cannot get context");
  dac = iio_context_find_device(ctx, "axi-ad9152-hpc");
  if (!dac)
    err("cannot find dac");
  // If we dont enable the channels, they wont work.
  // even if channels were enabled by a prior session.
  dac_ch0 = iio_device_find_channel(dac, "voltage0", true); // by name or id
  if (!dac_ch0)
    err("dac lacks channel");
  dac_ch1 = iio_device_find_channel(dac, "voltage1", true); // by name or id
  if (!dac_ch1)
    err("dac lacks channel");
  chan_en(dac_ch0);
  chan_en(dac_ch1);

  /*
  n=iio_channel_get_attrs_count(dac_ch0);
  printf("there are %d attributes\n", n);
  for(i=0;i<n;++i) {
    printf("--  %d %s\n", i, iio_channel_get_attr(dac_ch0, i));
  }
  */


  // I suspect DMA MUST be multiple of 16 bytes = 8 samps.
  npds=4;
  
  n = (int)round(pd_ns*npds*1.0e-9*st.asamp_Hz/8)*8;
  n = u_max(8, n);
  printf("  %d pds is %d asamps = %.2f ns\n", npds, n, n/st.asamp_Hz*1e9);
  printf("  one pd is actualy %.2f ns = %.2f MHz\n", n/st.asamp_Hz*1e9/npds,
	  st.asamp_Hz*npds/1e6/n);

  if (n<=0) return CMD_ERR_FAIL;

  // for twopi use (1<<15)-1
  // for pi use (1<<14)-1
  mem_sz = n*sizeof(short int);
  mem = (short int *)malloc(mem_sz);
  if (!mem) err("out of memory");
  for(k=0;k<n;++k) {
    th = k*npds*2*M_PI/n;
    i = round(sin(th)*((1<<14) - 1));
    mem[k  ]=(short int)i;
    if (i>mx) mx=i;
    printf("  %d %g %d\n", k, th, (int)mem[k]);
  }
  printf("max %d = x%04x\n", mx, mx);
  
  sz = iio_device_get_sample_size(dac);
  // sample size is 2 * number of enabled channels.
  printf("iio dac samp size %zd bytes\n", sz);
  if (sz<=0) return CMD_ERR_FAIL;


  // oddly, this create buffer seems to cause the dac to output
  // a GHz sin wave for about 450us.
  dac_buf_sz = mem_sz / sz;
  dac_buf = iio_device_create_buffer(dac, dac_buf_sz, false);
  if (!dac_buf) {
    sprintf(errmsg, "cant create dac bufer  nsamp %zd", dac_buf_sz);
    err(errmsg);
  }

  void *p;
  // calls convert_inverse and copies data into buffer
  p = iio_buffer_start(dac_buf);
  if (!p) err("no buffer yet");
  memcpy(p, mem, mem_sz);
  // sz = iio_channel_write(dac_ch0, dac_buf, mem, mem_sz);
  // returned 256=DAC_N*2, makes sense
  printf("  filled dac_buf with %zd bytes\n", mem_sz);

  tx_sz = iio_buffer_push(dac_buf);
  printf("  pushed %zd bytes\n", tx_sz);
  i=mem_sz/8-2;
  h_w_fld(H_DAC_DMA_MEM_RADDR_LIM_MIN1, i);
  // This problem solved
  qregs_dbg_get_info(&j);
  if (i!=j) {
    printf("DBG: raddr lim %d = x%x but dbg %d= x%x\n", i, i,j,j);
  }

  
  //  qregs_halfduplex_is_bob(0);

  // Should not have to set go cond.
  //  qregs_set_tx_go_condition('i');
  
  u_pause("");

  //  qregs_dbg_print_tx_status(); // well behaved

  
  //  qregs_set_use_lfsr(0);
  qregs_set_alice_txing(0);
  // h_w_fld(H_DAC_CTL_ALICE_SYNCING, 0); // for now halts IM hdr
  qregs_set_tx_mem_circ(1);
  qregs_set_memtx_to_pm(1);
  qregs_set_tx_always(0);
  qregs_txrx(0);

  u_pause("");
  //  iio_context_destroy(ctx);    

  printf("DBG: run tst dont use im preemph, search for probe using 0 thresh\n");
  
  return 0;  
}

int cmd_phest(int arg) {
  int i;
  double d;
  if (parse_int(&i)) return CMD_ERR_NO_INT;
  if (parse_double(&d))
    d = st.xph_deg;
  qregs_set_phase_est_en(i, d);
  printf("%d %.3f\n", st.phase_est_en, st.xph_deg);
  return 0;
}

int cmd_pm_dly(int arg) {
  int dly;
  if (parse_int(&dly)) return CMD_ERR_NO_INT;
  qregs_set_pm_dly_cycs(dly);
  printf("%d\n", st.pm_dly_cycs);
  return 0;
}


int cmd_im_bias(int arg) {
  int ch, e;
  double bias_V;
  if (parse_int(&ch)) {
    rp_set_t set;
    e = rp_get_settings(&set);
    if (e) qerr("rp_get_settings failed");
    printf("bias %.6f %.6f (V)\n", set.bias_V[0], set.bias_V[1]);
    return 0;
  }
  if (parse_double(&bias_V)) return CMD_ERR_NO_INT;
  e = rp_set_bias(ch, bias_V);
  if (e) qerr("rp_set_bias failed");
  return 0;
}



int cmd_im_bsweep(int arg) {
  // usage: bweep <ch> where 1=after im1, 2=after im2
  int i, ch, e;
  double b_V, step;
  rp_hist_t hist;
  if (parse_int(&ch)) return CMD_ERR_NO_INT;
  step=QREGS_IM_BIAS_RANGE_V/16;
  printf("optical power as a function of bias");
  printf("bias_V  pilot_V  body_V  mean_V\n");
  for(i=0;i<16;++i) {
    b_V = step*i;
    e = rp_set_bias(ch, b_V);
    if (e) {err("rp_set_bias failed\n"); break;}
    printf("%7.6f", b_V);
    e=rp_meas_pwr_hist(ch, &hist);
    if (e) {err("rp_meas_pwr_hist failed\n"); return CMD_ERR_FAIL;}
    printf("  %7.3f %7.3f  %7ef", hist.pilot_pwr_V, hist.body_pwr_V, hist.mean_pwr_V);
  }
  printf("\n");
  return 0;
}

int cmd_im_hist(int arg) {
  int ch, i, e;
  rp_hist_t hist;
  if (parse_int(&ch)) return CMD_ERR_NO_INT;
  char c=parse_nonspace();
  while(1) {
    e= rp_meas_pwr_hist(ch, &hist);
    if (e) {printf("err: no rsp from RP?\n");  return CMD_ERR_FAIL;}
    printf("  ext  %4.1f dB    b/m %4.1f dB   body %7.3f V\n",
	   hist.ext_rat_dB, hist.body_rat_dB, hist.body_pwr_V);
    if (!c) break;
    for(i=0;i<64;++i)
      printf(" %7.6lf",hist.bins[i]);
    usleep(1000*1000);
  }
}

int cmd_im_dly(int arg) {
  int dly;
  if (parse_int(&dly)) return CMD_ERR_NO_INT;
  qregs_set_hdr_im_dly_cycs(dly);
  printf("%d\n", st.hdr_im_dly_cycs);
  return 0;
}

#if QREGC_LINKED
int cmd_r(int arg) {
  int en;
  printf("remote tx always");
  if (parse_int(&en)) return CMD_ERR_NO_INT;  
  qregc_connect("10.0.0.5", &remote_err_handler);
  qregc_set_tx_always(&en);
  qregc_disconnect();
  printf("%d\n", en);
  return 0;
}
#endif



int help(int arg) {
  cmd_help(cmds_info);
}

char qna_rsp[1024];

int cmd_lo_en(int arg) {
  int e, en;
  if (parse_int(&en)) return CMD_ERR_SYNTAX;
  e = qregs_set_lo_en(en);
  if (e) err("timeout from QNA");
  // this is sort of a lie
  printf("%d\n", en);
  return 0;
}
int cmd_lo_offset(int arg) {
  int off, e;
  if (parse_int(&off)) return CMD_ERR_SYNTAX;
  e = qregs_set_lo_offset_MHz(off);
  if (e) return e;
  printf("%d\n", off);
  return 0;
}
int cmd_lo_mode(int arg) {
  int e;
  char c;
  if (!(c=parse_nonspace())) return CMD_ERR_SYNTAX;
  e = qregs_set_lo_mode(c);
  if (e) return e;
  printf("%c\n", c);
  return 0;
}

int cmd_lo_fdbken(int arg) {
  int e, en;
  if (parse_int(&en)) return CMD_ERR_SYNTAX;
  e = qregs_set_lo_fdbk_en(en);
  if (e) return e;
  printf("%c\n", en);
  return 0;
}

int cmd_lo_pwr(int arg) {
  int e;
  double dBm;
  if (parse_double(&dBm)) return CMD_ERR_SYNTAX;
  e = qregs_set_lo_pwr_dBm(&dBm);
  if (e) err("timeout from QNA");
  printf("%g\n", dBm);
  return 0;
}

int cmd_lo_stat(int arg) {
  int e;
  qregs_lo_status_t s;
  e = qna_get_lo_status(&s);
  if (e) {
    qregs_print_last_err();
    return CMD_ERR_FAIL;
  }
  printf("  pwr             %.2f dBm\n", s.pwr_dBm);
  printf("  init_err        %d\n", s.init_err);
  printf("  gas_lock        %d\n", s.gas_lock);
  printf("  gas_lock_dur    %d s\n", s.gas_lock_dur_s);
  printf("  gas_err_rms     %.1f MHz\n", s.gas_err_rms_MHz);
  return 0;
}

int cmd_sfp_rst(int arg) {
  qregs_sfp_gth_rst();
  printf("1\n");
}


int cmd_sfp_init(int arg) {
  int e;
  char *ini_fname = "cfg/ini_all.txt";
  char *str_p;
  ini_val_t *ivars;  
  e = ini_read(ini_fname, &ivars);
  if (e) {
    printf("err %d\n", e);
    printf("%s\n", ini_err_msg());
    return CMD_ERR_FAIL;
  }
  e=ini_get_string(ivars,"sfp_init", &str_p);
  if (e) err("no sfp_init in ini_all.txt");
  printf("reading %s\n", str_p);
  e=sfp_init(str_p, 1);
  ini_free(ivars);
  return e;
}


int cmd_sync_stat(int arg) {
  printf("sync dly %d asamps\n", st.sync_dly_asamps);
  qregs_print_sync_status();
  return 0;
}
int cmd_sync_resync(int arg) {
  qregs_sync_resync();
  printf("resynced\n\n");
  qregs_print_sync_status();  
  return 0;
}

int cmd_sync_dbg(int arg) {
  int hdr_cnt, mag_tot, i;
  int e;
  printf("hdr cnt and mag tot at current sync dly\n");
  for(i=1;i<16;++i) {
    e=qregs_get_hdr_detection_stats(&hdr_cnt, &mag_tot);  
    if (e) return qerr("qregs_get_hdr_detection_stats failed");
    printf(" %6d %6d\n", hdr_cnt, mag_tot);
  }
  return 0;
}

int cmd_sync_tsweep(int arg) {
  int max, th, th_sav, i, pilot_cnt, mag_tot, e;
  if (parse_int(&max)) {
    max = ini_ask_num(tvars, "max thresh to sweep down from",
		      "tsweep_max", 100);
  }
  th_sav = st.hdr_corr_thresh;
  printf("th  pilot_cnt mag_tot\n");
  for (i=0; i<16;++i) {
    th =(16-i)*max/16;
    qregs_set_hdr_det_thresh(st.hdr_pwr_thresh, th);
    e=qregs_get_hdr_detection_stats(&pilot_cnt, &mag_tot);
    if (e) qerr("qregs_get_hdr_detection_stats failed");
    printf("%6d %6d %6d\n", th, pilot_cnt, mag_tot);
  }
  qregs_set_hdr_det_thresh(st.hdr_pwr_thresh, th_sav);
  printf("corr_thresh restored back to %d\n", st.hdr_corr_thresh);
  return 0;
}

int cmd_sync_sweep(int arg) {
  int i, hdr_cnt, mag_tot, best, best_i, avg, e, z=0;
  for (i=0; i<st.frame_pd_asamps; i+=4) {
    //    for (i=0; i<8; i+=4) {
    qregs_set_sync_dly_asamps(i);
    e=qregs_get_hdr_detection_stats(&hdr_cnt, &mag_tot);
    if (e) return qerr("BUG: qregs_get_hdr_detection_stats failed");
    if (!hdr_cnt)
      avg=0;
    else
      avg=mag_tot/hdr_cnt;
    
    if ((i==0)||(avg > best)) {
      best_i = i;
      best   = avg;
    }
    if (!hdr_cnt || !mag_tot) {
      if (!z)
	printf("   ...\n");
      z=1;
    }else {
      printf("%3d  %5d/%5d = %d\n", i, mag_tot, hdr_cnt, avg);
      z=0;
    }
  }
  printf("setting: sync_dly_asamps %d\n", best_i);
  qregs_set_sync_dly_asamps(best_i);
  return 0;
}

int cmd_sync_dly(int arg) {
  int dly;
  if (parse_int(&dly)) return CMD_ERR_NO_INT;
  qregs_set_sync_dly_asamps(dly);
  printf("%d\n", st.sync_dly_asamps);
  return 0;
}
int cmd_dly_rx2tx(int arg) {
  int dly;
  if (parse_int(&dly)) {
    dly = ini_ask_num(tvars, "rx to tx (asamps)",
		      "rx2tx_dly_asamps", st.rx2tx_dly_asamps);

  }
  qregs_set_rx2tx_dly_asamps(dly);
  printf("%d\n", st.rx2tx_dly_asamps);
  return 0;
}


int cmd_sync_add(int arg) {
  int add, i;
  if (parse_int(&add)) return CMD_ERR_NO_INT;
  i=st.sync_dly_asamps;
  qregs_set_sync_dly_asamps(i+add);
  printf("was %d now %d\n", i, st.sync_dly_asamps);
}

int cmd_sync_ref(int arg) {
  int e;
  char c=parse_nonspace();
  e= qregs_set_sync_ref(c);
  if (e) qregs_print_last_err();
  return e;
}

int cmd_qsdc_cfg(int arg) {
  double gap_ns;
  qregs_qsdc_data_cfg_t data_cfg;
  int i, e;
  ini_val_t *tvars;
  e =  ini_read("tvars.txt", &tvars);
  if (e)
    printf("ini err %d\n",e);
  
  data_cfg.is_qpsk      = ini_ask_yn(tvars, "is the data qpsk","body_is_qpsk", 0);

  data_cfg.symbol_len_asamps = ini_ask_num(tvars,"data symbol len (asamps)",
                                           "symbol_len_asamps", 8);

  gap_ns = ini_ask_num(tvars,"gap after header (ns)", "post_hdr_gap_ns", 100);
  i= round(gap_ns * 1e-9 * st.asamp_Hz / st.osamp) * st.osamp;
  i = ((int)i/4)*4;
  printf("rounded to %d asamps = %.1f ns\n", i, i/st.asamp_Hz*1.0e9);
  data_cfg.pos_asamps   = st.hdr_len_asamps + i;

  gap_ns = ini_ask_num(tvars,"gap at end (ns)", "post_body_gap_ns", 10);
  i= round(gap_ns * 1e-9 * st.asamp_Hz / st.osamp) * st.osamp;
  i = ((int)i/4)*4;
  i = (st.frame_pd_asamps - i - data_cfg.pos_asamps);
  data_cfg.data_len_asamps = i;
  printf("per-frame data len %d asamps = %.1f ns\n", i, i/st.asamp_Hz*1.0e9);


  i = ini_ask_num(tvars,"duration of one data bit (in symbols)", "bit_dur_symbols", 10);
  data_cfg.bit_dur_syms = i;
  

  qregs_set_qsdc_data_cfg(&data_cfg);
  
  ini_write("tvars.txt", tvars);
  ini_free(tvars);  
  return 0;
}

int cmd_opsw(int arg) {
  int idx, i,cross, e;
  if (parse_int(&idx))   { // base one
    for(i=0;i<QREGS_NUM_OPSW;++i)
      printf(" %s=%d", qregs_opsw_names[i], st.opsw_cross[i]);
    printf("\n");
    return 0;
  }
  if (parse_int(&cross)) return CMD_ERR_SYNTAX;
  --idx;
  e = qregs_set_opsw(idx, &cross);
  if (e) qerr("failed to set opsw");
else  printf("%d (for %s)\n", cross, qregs_opsw_names[idx]);
  return 0;
}


int cmd_voa(int arg) {
  int i, voa_i=-1, e;
  double attn_dBm;
  parse_space();
  if (isalpha(parse_next())) {
    char voa_name[16];
    parse_token(voa_name, 16);
    for (i=0;i<QREGS_NUM_VOA;++i) {
      if (!strcmp(voa_name, qregs_voa_names[i])) {
	voa_i=i; break;
      }
    }
    if (voa_i<0) return CMD_ERR_SYNTAX;
  }else if (parse_int(&voa_i))  { // base one
    for(i=0;i<QREGS_NUM_VOA;++i)
      printf("  %s %.2f", qregs_voa_names[i], st.voa_attn_dB[i]);
    printf("\n");
    return 0;
  }

  if (parse_double(&attn_dBm))
    attn_dBm = u_ask_dbl("attenuation (dB)", st.voa_attn_dB[voa_i]);
  e = qregs_set_voa_attn_dB(voa_i, &attn_dBm);
  if (e) qerr("failed to set voa");
  printf("voa %s %.2f\n", qregs_voa_names[voa_i],
	 st.voa_attn_dB[voa_i]);
  return 0;
}



int cmd_dbg_search(int arg) {
  printf("dbg search for hdr\n");
  qregs_print_adc_status();
  printf("\n");
  qregs_search_en(1);
  while(1) {
    qregs_print_hdr_det_status();
    usleep(1000*1000);
  }
  return 0;
}


int cmd_lo_set(int arg) {
  int e;
  qregs_lo_settings_t s;
  e = qna_get_qna1_settings(&s);
  if (e) qregs_print_last_err();
  else {
    printf("  en %d\n", s.en);
    printf("  pwr_dBm %.2f\n", s.pwr_dBm);
    printf("  wl_nm %.3f\n", s.wl_nm);
    printf("  mode %c\n", s.mode);
    printf("  gas_fdbk_en     %d\n", s.gas_fdbk_en);
    printf("  gas_goal_offset %d MHz\n", s.gas_goal_offset_MHz);
  }
  return 0;
}

int cmd_lo_wl(int arg) {
  int e;
  double wl_nm;
  if (parse_double(&wl_nm)) return CMD_ERR_SYNTAX;  
  e = qna_set_lo_wl_nm(&wl_nm);
  if (e) err("timeout from QNA");
  printf("%.3f\n", wl_nm);
  return 0;
}

cmd_info_t im_cmds_info[]={
  {"bsweep",  cmd_im_bsweep,  0, "sweep bias meas pwr",    "<ch>"},
  {"hist",    cmd_im_hist,    0, "meas hist of frame pwr", "<ch>"},
  {"bias",    cmd_im_bias,    0, "set IM bias",            "<ch> <V>"},
  {0}};


cmd_info_t dbg_cmds_info[]={
  {"pwr",    cmd_dbg_pwr,  0, 0}, 
  {"clksel", cmd_dbg_clksel, 0, 0}, 
  {"pminv",  cmd_dbg_pminv,  0, 0}, 
  {"rpreboot",  cmd_dbg_rpreboot,  0, 0}, 
  {"rpinfo",   cmd_dbg_rpinfo,  0, 0}, 
  {"search", cmd_dbg_search, 0, 0}, 
  {"info",   cmd_dbg_info,  0, 0},  
  {"regs",   cmd_dbg_regs, 0, 0},
  {"ser",    cmd_dbg_ser, 0, 0},
  {"rp",     cmd_rp,      0, "dbg RedPitaya cmds"},
  {0}};

cmd_info_t qsdc_cmds_info[]={
  {"cfg",    cmd_qsdc_cfg,  0, 0}, 
  {0}};



cmd_info_t sfp_cmds_info[]={
  {"init",   cmd_sfp_init,   0, "init & rst si5328 pll"},  
  {"rst",    cmd_sfp_rst,    0, "reset GTH"},  
  {"status", cmd_sfp_status, 0, "view status"},
  {0}};


cmd_info_t lo_cmds_info[]={
  {"en",     cmd_lo_en,     0, "enable LO laser", "0|1"},
  {"fdbken", cmd_lo_fdbken, 0, "enable gas feedback", "0|1"},
  {"mode",   cmd_lo_mode,   0, "set laser mode", "w|d"},
  {"offset", cmd_lo_offset, 0, "set LO offset frequency", "<MHz>"},
  {"pwr",    cmd_lo_pwr,    0, "set laser pwr", "dBm"},  
  {"set",    cmd_lo_set,    0, "view settings", 0},  
  {"stat",   cmd_lo_stat,   0, "view status", 0},  
  {"wl",     cmd_lo_wl,     0, "set wavelength", "nm"},  
  {0}};  
  
cmd_info_t cal_cmds_info[]={
  {"measdark",  cmd_cal_measdark,   0, "set dark lvl on dets", ""},
  {"norebal",   cmd_cal_norebal,    0, "turn off HDL IQ rebalance", ""},
  {0}};

cmd_info_t dly_cmds_info[]={
  {"rx2tx",   cmd_dly_rx2tx,   0, "set alice's rx2tx dly", "<asamps>"},
  {"rxsc",    cmd_dly_rxsc,    0, "set rx sub-cycle dly (asamps)", "0..3"},
  {"round",   cmd_dly_round,   0, "set bob's tx2rx dly", "[<asamps>]"},
  {0}};

cmd_info_t sync_cmds_info[]={
  {"ref",  cmd_sync_ref,   0, "h=hdr,p=pwr,r=rxclk", "h|r|p"},
  {"sweep", cmd_sync_sweep, 0, "sweep sync2rx for alice", 0},
  {"tsweep", cmd_sync_tsweep, 0, "sweep hdr thresh", 0},
  {"dbg",  cmd_sync_dbg, 0, "dbg sync stat",0},
  {"stat", cmd_sync_stat,  0, "view sync status", 0},
  {"add",  cmd_sync_add,   0, "add to sync dly", "<asamps>"},
  {"dly",  cmd_sync_dly,   0, "set sync dly",  "<asamps>"},
  {"resync", cmd_sync_resync, 0, "resync", 0},
  {0}};  

cmd_info_t cmds_info[]={
  //  {"calbpsk", cmd_cal,       0, 0},
  {"always",  cmd_always,   0, "0|1"},
  {"cal",     cmd_subcmd, cal_cmds_info, 0, 0},
  {"circ",    cmd_circ,   0,   "0|1"},
  {"dbg",     cmd_subcmd, dbg_cmds_info, 0, 0},
  {"lo",      cmd_subcmd, lo_cmds_info, 0, 0},
  {"dly",     cmd_subcmd, dly_cmds_info, 0, 0},
  {"help",    help,       0, 0},
  {"pm_dly",  cmd_pm_dly, 0, 0},
  {"phest",   cmd_phest,  0, "set auto phase est0", "0|1 <deg>"}, 
  {"im_dly",  cmd_im_dly, 0, 0}, 
  {"pm_sin",  cmd_pm_sin, 0, 0}, 
  {"init",    cmd_init,   0, "initializes HDL"}, 
  {"sfp",     cmd_subcmd, sfp_cmds_info, 0, 0}, 
  {"tx",      cmd_tx,     0, 0},
  {"rx",      cmd_rx,   0, 0},

  {"opsw",    cmd_opsw,   0, 0},
  {"pwr",     cmd_pwr,     0, "querry RedPitaya for power"},
  {"proto",   cmd_proto,   0, "asks for protocol params", 0},
  {"qsdc",    cmd_subcmd, qsdc_cmds_info, 0, 0},
  {"sweep",   cmd_sweep, 0, 0},
  {"sync",    cmd_subcmd, sync_cmds_info, 0, 0},
  {"rst",     cmd_rst,   0, 0},
#if QREGC_LINKED  
  {"r",       cmd_r,     0, 0},
#endif  
  {"set",      cmd_set,  0, 0},
  {"stat",     cmd_stat,   0, 0},
  {"shutdown", cmd_shutdown,   0, 0},
  {"thresh",   cmd_thresh, 0, "set detection thersholds", "[<pwr> <corr> [<ini>]]"}, 
  {"twopi",   cmd_twopi,   0, 0},
  {"voa",     cmd_voa,   0, "[<voaidn> <dB>]"},
  {"ver",     cmd_ver,   0, 0}, 
  {0}};



int main(int argc, char *argv[]) {
  int i, j, e;
  char c, ll[256]={0};
  
  e =  ini_read("tvars.txt", &tvars);
  if (e)
    err("cant read tvars.txt");


  ini_val_t *vars_cfg_all;
  e =  ini_read("cfg/ini_all.txt", &vars_cfg_all);
  if (e)
    printf("can read ini_all.txt err %d\n",e);
  char *tty0_p, *tty1_p;
  e = ini_get_string(vars_cfg_all,"tty0", &tty0_p);
  if (e) tty0_p=0;
  e = ini_get_string(vars_cfg_all,"tty1", &tty1_p);
  if (e) tty1_p=0;
  // printf("tty0 %s\n", tty0_p);
  if (qregs_init(tty0_p, tty1_p)) err("qregs fail");
  ini_free(vars_cfg_all);

  
  for(i=1;i<argc;++i) {
    if (i>1) strcat(ll, " ");
    strcat(ll, argv[i]);
  }
  // printf("exec: %s\n", ll);
  e = cmd_exec(ll, cmds_info);
  if (e && (e!=CMD_ERR_QUIT))
    cmd_print_errcode(e);

  e = ini_save(tvars);
  if (e)
    printf("ERR: cannot save tvars.txt\n%s\n", ini_err_msg());

  if (qregs_done()) err("qregs_done fail");

  if (do_shutdown) {
    if (u_ask_yn("really really shutdown",-1)) {
    printf("shutting down in 1 second...\n");
    usleep(1000000);
    sync();
    setuid(0);
    reboot(RB_POWER_OFF);
    }
  }
  return 0;
}

