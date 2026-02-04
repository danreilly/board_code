
// Client-side code for qrp service running on Red Pitaya.

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "qregs_ll.h"
#include "util.h"
#include "rp.h"
#include "parse.h"



#define CMD_LEN 1024
static char rp_cmd[CMD_LEN];
static char rp_rsp[CMD_LEN];
char rp_errmsg[CMD_LEN];


int rp_dbg=0;
int rp_connected=0;

int rp_do_cmd(char *cmd) {
  int e;
  char *p;
  qregs_ser_flush();
  qregs_ser_sel(QREGS_SER_SEL_RP);
  if (rp_dbg) {
    printf("RP tx:");
    u_print_all(cmd);
    printf("\n");
  }
  e=qregs_ser_do_cmd(cmd, rp_rsp, CMD_LEN, 0);
  if (rp_dbg) {
    printf("RP rx:");
    u_print_all(rp_rsp);
    printf("\n");
  }
  p=strstr(rp_rsp,"ERR:");
  if (p) {
    strncpy(rp_errmsg, p, CMD_LEN-1);
    rp_errmsg[CMD_LEN-1]=0;
    p=strstr(rp_errmsg,"\n");
    if (p) *p=0;
    printf("RP ERR: %s\n", rp_errmsg);
    return qregs_err_fail(rp_errmsg);
  }
  return e;
}

int rp_connect() {
  int e;
  e = rp_do_cmd("i\r");
  if (e) return qregs_err_fail("could not connect to RP");
  rp_connected=1;
  printf("NOTE: connected to %s\n", rp_rsp);
  return 0;
}

int rp_cfg_frames(int frame_pd_asamps, int pilot_dur_asamps) {
  int e;
  if (!rp_connected) {
    if ((e=rp_connect())) return e;
  }
  sprintf(rp_cmd,"cfg %d %d\r", frame_pd_asamps, pilot_dur_asamps);
  e = rp_do_cmd(rp_cmd);
  if (e) return e;
  return 0;
}

int rp_shutdown(void) {
  int e;
  if (!rp_connected) {
    if ((e=rp_connect())) return e;
  }
  e = rp_do_cmd("shutdown\r");
  return e;
}

int rp_info(char *str, int strlen) {
  int e;
  if (!rp_connected) {
    if ((e=rp_connect())) return e;
  }
  e = rp_do_cmd("info\r");
  str[0]=0;
  if (!e) {
    strncpy(str, rp_rsp, strlen);
    str[strlen-1]=0;
  }
  return e;
}

int rp_measdark(void) {
  int e;
  if (!rp_connected) {
    if ((e=rp_connect())) return e;
  }
  e = rp_do_cmd("measdark\r");
  printf("%s\n", rp_rsp);
  return e;
}

int rp_reboot(void) {
  int e;
  if (!rp_connected) {
    if ((e=rp_connect())) return e;
  }
  e = rp_do_cmd("reboot\r");
  return e;
}

int rp_get_status(rp_status_t *status) {
  int e, e1;
  double dark, hdr, body, mean;
  if (!rp_connected) {
    if ((e=rp_connect())) return e;
  }
  e = rp_do_cmd("stat\r");
  if (e) return e;
  e1 = qregs_findkey_dbl(rp_rsp, "dark", &dark);
  if (e1) e=e1;
  e1 = qregs_findkey_dbl(rp_rsp, "hdr", &hdr);
  if (e1) e=e1;
  e1 = qregs_findkey_dbl(rp_rsp, "body", &body);
  if (e1) e=e1;
  e1 = qregs_findkey_dbl(rp_rsp, "mean", &mean);
  if (e1) e=e1;
  if (e) return e;
  //  status->pwr_dBm = (double)i/100;
  //  printf("DBG: dark %.6f  hdr %.6f  body %.6f  mean %.6f\n",
  //	 dark, hdr, body, mean);
  status->pilot_pwr_V = hdr;
  status->mean_pwr_V = mean;
  status->body_pwr_V = body;
  status->dark_pwr_V = dark;
  
  status->ext_rat_dB  = (body < dark) ? 1000 :
    10*log10((hdr-dark)/(body-dark));
  
  status->body_rat_dB = (mean < dark) ? 1000 :
    10*log10((body-dark)/(mean-dark));

  //  strncpy(srsp,  qna_rsp, srsp_len-1);
  //  srsp[srsp_len-1]=0;
  return e;
  
}


int rp_meas_pwr_hist(int ch, rp_hist_t *rval) {
  int e, e1;
  double dark, hdr, body, mean;
  char cmd[64];
  if (!rp_connected) {
    if ((e=rp_connect())) return e;
  }
  sprintf(cmd,"hist %d\r", ch);
  e = rp_do_cmd(cmd);
  if (e) return e;
  e1 = qregs_findkey_dbl(rp_rsp, "dark", &dark);
  if (e1) e=e1;
  e1 = qregs_findkey_dbl(rp_rsp, "hdr", &hdr);
  if (e1) e=e1;
  e1 = qregs_findkey_dbl(rp_rsp, "body", &body);
  if (e1) e=e1;
  e1 = qregs_findkey_dbl(rp_rsp, "mean", &mean);
  if (e1) e=e1;
  if (e) return e;

  char tmp[1024];
  e1 = qregs_findkey(rp_rsp, "hist", tmp, 1024);
  if (e1) e=e1;
  else {
    int num_bins, i;
    printf("DBG GH: %s\n", tmp);
    parse_set_line(tmp);
    parse_int(&num_bins);
    num_bins=u_min(num_bins,RP_MAX_BINS);
    for(i=0;i<num_bins;++i) {
      parse_double(&rval->bins[i]);
      printf("DBG GH:    %lg\n", rval->bins[i]);
    }
  }
  if (e) return e;
  
  //  status->pwr_dBm = (double)i/100;
  //  printf("DBG: dark %.6f  hdr %.6f  body %.6f  mean %.6f\n",
  //	 dark, hdr, body, mean);
  rval->pilot_pwr_V = hdr;
  rval->mean_pwr_V  = mean;
  rval->body_pwr_V  = body;
  rval->dark_pwr_V  = dark;
  
  rval->ext_rat_dB  = (body < dark) ? 1000 :
    10*log10((hdr-dark)/(body-dark));
  
  rval->body_rat_dB = (mean < dark) ? 1000 :
    10*log10((body-dark)/(mean-dark));

  //  strncpy(srsp,  qna_rsp, srsp_len-1);
  //  srsp[srsp_len-1]=0;
  return e;
}


int rp_set_bias(int ch, double bias_V) {
  // bias: zero based
  int e, e1, n;
  char cmd[64];
  double d;
  if (!rp_connected) {
    if ((e=rp_connect())) return e;
  }
  sprintf(cmd, "bias %d %.6f\r", ch+1, bias_V);
  e = rp_do_cmd(cmd);
  //  e1 = qregs_findkey_dbl(rp_rsp, "bias", &d);
  // maybe this:
  n=sscanf(rp_rsp, "%lg", &d);
  printf("DBG:RP_NEW: n=%d, %g\n", n, d);
  if (e1) e=e1;
  return e;
}


int rp_get_settings(rp_set_t *set) {
  int e, e1;
  char cmd[64];
  char tmp[1024];
  if (!rp_connected) {
    if ((e=rp_connect())) return e;
  }

  sprintf(cmd, "set\r");
  e = rp_do_cmd(cmd);

  e1 = qregs_findkey(rp_rsp, "bias", tmp, 1024);
  if (e1) e=e1;
  else {
    int num_bins, i;
    parse_set_line(tmp);
    for(i=0;i<2;++i)
      parse_double(&set->bias_V[i]);
  }
  e1 = qregs_findkey(rp_rsp, "fdbk_en", tmp, 1024);
  if (e1) e=e1;
  else {
    int num_bins, i;
    parse_set_line(tmp);
    for(i=0;i<2;++i)
      parse_int(&set->fdbk_en[i]);
  }
  return e;
}


