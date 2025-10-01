
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "qna.h"
#include "qna_usb.h"
#include "qregs.h"
#include "qregs.h"
#include "qregs_ll.h"
#include "util.h"

qna_st_t qna_st={0};

#define CMD_LEN 1024
static char qna_cmd[CMD_LEN];
static char qna_rsp[CMD_LEN];
char qna_errmsg[CMD_LEN];

int qna_dbg=0;


int qna_err(int e, char *msg) {
  // convenince function  
  strcpy(qna_errmsg, msg);
  return e;
}


int qna_do_cmd(int ser_sel) {
// ser_sel: one of QREGS_SER_SEL*
  int e;
  char *p;
  // printf("is usb(%d) %d\n", ser_sel, st.qna_is_usb[0l]);
  if ((ser_sel==QREGS_SER_SEL_QNA1) && st.qna_is_usb[0]) {
    e=qna_usb_do_cmd(0, qna_cmd, qna_rsp, CMD_LEN);
  }else if ((ser_sel==QREGS_SER_SEL_QNA2) && st.qna_is_usb[1]) {
    e=qna_usb_do_cmd(1, qna_cmd, qna_rsp, CMD_LEN);
  }else {
    qregs_ser_flush();
    qregs_ser_sel(ser_sel);
    if (qna_dbg) {
      printf("QNA tx:");
      u_print_all(qna_cmd);
      printf("\n");
    }
    e=qregs_ser_do_cmd(qna_cmd, qna_rsp, CMD_LEN, 1);
    if (qna_dbg) {
      printf("QNA rx:");
      u_print_all(qna_rsp);
      printf("\n");
    }
    p=strstr(qna_rsp,"ERR:");
    if (p) {
      strncpy(qna_errmsg, p, CMD_LEN-1);
      qna_errmsg[CMD_LEN-1]=0;
      p=strstr(qna_errmsg,"\n");
      if (p) *p=0;
      printf("QNA ERR: %s\n", qna_errmsg);
      return qregs_err_fail(qna_errmsg);
    }
  }
  return e;
}

int qna_do_cmd_get_num(int ser_sel, double *d) {
  int e;
  e = qna_do_cmd(ser_sel);
  if (e) return
    qregs_err(QREGS_ERR_TIMO, "qna timeout");
  e = sscanf(qna_rsp,"%lg", d);
  if (e!=1) return
    qregs_err(QREGS_ERR_FAIL, "qna did not provide numeric respose");
  return 0;
}

int qna_do_cmd_get_int(int ser_sel, int *d) {
  int e;
  e = qna_do_cmd(ser_sel);
  if (e) return e; //QREGS_ERR_TIMO;
  e = sscanf(qna_rsp,"%d", d);
  if (e!=1) return
    qregs_err(QREGS_ERR_FAIL, "qna did not provide integer respose");
  return 0;
}


int qna_set_timo_ms(int timo_ms) {
  qregs_ser_set_timo_ms(timo_ms);
}




int qna_get_lo_status(qregs_lo_status_t *status) {
  int e, e1, i=0, j;
  strcpy(qna_cmd, "stat\r");
  e = qna_do_cmd(QREGS_SER_SEL_QNA1);
  printf("qna rsp: %s\n", qna_rsp);
  
  if (e) return e;
  e1 = qregs_findkey_int(qna_rsp, "gas_lock", &status->gas_lock);
  if (e1) e=e1;
  e1 = qregs_findkey_int(qna_rsp, "gas_lock_dur_s", &status->gas_lock_dur_s);
  if (e1) e=e1;
  e1 = qregs_findkey_int(qna_rsp, "gas_mse_MHz2", &j);
  if (e1) e=e1;
  status->gas_err_rms_MHz = sqrt((double)j);

  strcpy(qna_cmd, "cfg it stat\r");
  e = qna_do_cmd(QREGS_SER_SEL_QNA1);
  if (e) return e;
  e1 = qregs_findkey_int(qna_rsp, "init_err", &status->init_err);
  if (e1) e=e1;
  e1 = qregs_findkey_int(qna_rsp, "meas_pwr_dBmx100", &i);
  if (e1) e=e1;
  status->pwr_dBm = (double)i/100;
  //  strncpy(srsp,  qna_rsp, srsp_len-1);
  //  srsp[srsp_len-1]=0;
  return e;
}


  


int qna_get_qna_settings(qregs_lo_settings_t *set) {
  int e, e1=0, i=0;
  char tmp[64];

  // box1
  strcpy(qna_cmd, "set\r");
  e = qna_do_cmd(QREGS_SER_SEL_QNA1);
  // printf("\nqna1: set rsp: ");  u_print_all(qna_rsp);  printf("\n");
  if (e) e1=e;
  else {
    e = qregs_findkey_int(qna_rsp, "gas goal", &set->gas_goal_offset_MHz);
    if (e) e1=e;
    e = qregs_findkey_int(qna_rsp, "gas en", &set->gas_fdbk_en);
    if (e) e1=e;
    e = qregs_findkey_dbl(qna_rsp, "voa 1",
			  &st.voa_attn_dB[QREGS_VOA_QUANT_TX]);
    if (e) e1=e;
    e = qregs_findkey_dbl(qna_rsp, "voa 2",
			  &st.voa_attn_dB[QREGS_VOA_HYB_RX]);
    if (e) e1=e;

    e = qregs_findkey_int(qna_rsp, "opsw 1",
			  &st.opsw_cross[QREGS_OPSW_L]);
    if (e) e1=e;
    e = qregs_findkey_int(qna_rsp, "opsw 2",
			  &st.opsw_cross[QREGS_OPSW_RX2]);
    if (e) e1=e;
    e = qregs_findkey_int(qna_rsp, "opsw 3",
			  &st.opsw_cross[QREGS_OPSW_RX1]);
    if (e) e1=e;
    
  }
  
  strcpy(qna_cmd, "cfg it set\r");
  e = qna_do_cmd(QREGS_SER_SEL_QNA1);
  if (e) e1=e;
  else {
    e = qregs_findkey_int(qna_rsp, "en", &set->en);
    if (e) e1=e;
    e = qregs_findkey(qna_rsp, "mode", tmp, 64);
    if (e) e1=e;
    else set->mode=tmp[0];
  // printf("tmp: "); u_print_all(tmp);
    e = qregs_findkey_int(qna_rsp, "pwr", &i);
    if (e) e1=e;
    else set->pwr_dBm = (double)i/100;
    e = qregs_findkey_dbl(qna_rsp, "wl_nm", &set->wl_nm);
    if (e) e1=e;
  }

  // box2
  strcpy(qna_cmd, "set\r");
  e = qna_do_cmd(QREGS_SER_SEL_QNA2);
  if (e) e1=e;
  else {
    e = qregs_findkey_dbl(qna_rsp, "voa 1",
			  &st.voa_attn_dB[QREGS_VOA_DATA_TX]);
    if (e) e1=e;
    e = qregs_findkey_dbl(qna_rsp, "voa 2",
			  &st.voa_attn_dB[QREGS_VOA_DATA_RX]);
    if (e) e1=e;
    e = qregs_findkey_dbl(qna_rsp, "voa 3",
			  &st.voa_attn_dB[QREGS_VOA_QUANT_RX]);
    if (e) e1=e;
    
  }
  return e1;
}



int qna_set_lo_offset_MHz(int offset_MHz) {
// If attempt to set offset out of range, returns err
  int e;
  qna_set_timo_ms(1000);
  sprintf(qna_cmd, "gas goal %d\r", offset_MHz);
  e = qna_do_cmd(QREGS_SER_SEL_QNA1);
  return e;
}

int qna_set_lo_mode(char mode) {
// mode: 'd'=dither 'w'=whisper  
  int e;
  qna_set_timo_ms(60000);
  sprintf(qna_cmd, "cfg it mode %c\r", mode);
  e = qna_do_cmd(QREGS_SER_SEL_QNA1);
  qna_set_timo_ms(1000);
  return e;
}


int qna_set_lo_fdbk_en(int en) {
// mode: 'd'=dither 'w'=whisper  
  int e;
  sprintf(qna_cmd, "gas en %d\r", !!en);
  e = qna_do_cmd(QREGS_SER_SEL_QNA1);
  return e;
}


int qna_set_lo_wl_nm(double *wl_nm) {
  int e;
  qna_set_timo_ms(60000);
  e = qna_set_lo_mode('d');
  if (e) return qregs_err(e,"set to dither mode failed");
  // after this, we try to go on after an error
  sprintf(qna_cmd, "wavelen %.3f\r", *wl_nm);
  e = qna_do_cmd_get_num(0, wl_nm);
  if (e) qregs_err(e,"set wavelength failed");  
  e= qna_set_lo_mode('w');
  if (e) qregs_err(e,"set to whisper mode failed");
  qna_set_timo_ms(1000);
  return e;
}

int qna_set_lo_en(int en) {
  int e;
  qna_set_timo_ms(60000);  
  sprintf(qna_cmd, "cfg it en %d\r", en);
  e = qna_do_cmd(QREGS_SER_SEL_QNA1);
  qna_set_timo_ms(1000);
  return e;
}

int qna_set_lo_pwr_dBm(double *dBm) {
  int e, i;
  qna_set_timo_ms(60000);
  sprintf(qna_cmd, "cfg it pwr %d\r", (int)round(*dBm*100));
  e = qna_do_cmd_get_int(0, &i);
  if (e) return e;
  qna_st.pwr_dBm = (double)i/100;
  *dBm = qna_st.pwr_dBm;
  qna_set_timo_ms(500);
  return 0;  
}

int qna_voa_idx_to_board(int v_i, int *brd_i, int *bv_i) {
  if ((v_i<0) || (v_i>=QREGS_NUM_VOA))
    return qregs_err(QREGS_ERR_PARAM,"bad voa idx");
  if (v_i<3) {
    *brd_i=QREGS_SER_SEL_QNA1;
    *bv_i=v_i;
  }else {
    *brd_i=QREGS_SER_SEL_QNA2;
    *bv_i=v_i-3;
  }
  return 0;
}


int qna_set_voa_attn_dB(int voa_i, double *dBm) {
  int e, brd_i, bvoa_i;
  e=qna_voa_idx_to_board(voa_i, &brd_i, &bvoa_i); // validates voa_i
  if (e) return e;
  // voa indexes are base 1  
  sprintf(qna_cmd, "voa %d %.2lf\r", bvoa_i+1, *dBm);
  e = qna_do_cmd_get_num(brd_i, dBm);
  if (e) return e;
  st.voa_attn_dB[voa_i]=*dBm;
  return 0;
}

int qna_opsw_idx_to_board(int sw_i, int *ser_sel, int *bsw_i) {
  if ((sw_i<0) || (sw_i>=QREGS_NUM_OPSW))
    return qregs_err(QREGS_ERR_PARAM, "bad opsw idx");
  *ser_sel=QREGS_SER_SEL_QNA1;
  *bsw_i=sw_i;
  return 0;
}

int qna_set_opsw(int sw_i, int *cross) {
  int e, brd_i, bsw_i, rval;
  e=qna_opsw_idx_to_board(sw_i, &brd_i, &bsw_i); // validates v_i
  if (e) return e;
  // printf("brd %d bsw %d\n", brd_i, bsw_i);
  // qna indexes are base 1
  sprintf(qna_cmd, "opsw %d %d\r", bsw_i+1, *cross);
  printf("DBGOP: %s\n", qna_cmd);
  e = qna_do_cmd_get_int(brd_i, &rval);
  if (e) return e;
  st.opsw_cross[sw_i]=rval;
  return 0;
}
