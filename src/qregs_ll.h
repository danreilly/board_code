// low-level declarations
// included only by code that qregs.c calls

#ifndef _QREGS_LL_H_
#define _QREGS_LL_H_

#include "qregs.h"

extern char *qregs_errs[];
extern char qregs_errmsg[];

// lower-lvl code can report errors by returning one of these functions
int qregs_err(int e, char *msg);
int qregs_err_fail(char *msg);
int qregs_err_buf(char *msg);


// for parsing response strings
int qregs_findkey(char *buf, char *key, char *val, int val_size);
int qregs_findkey_int(char *buf, char *key, int *val);
int qregs_findkey_dbl(char *buf, char *key, double *val);


// serial links within QNIC
#define QREGS_SER_SEL_QNA2 (0)
#define QREGS_SER_SEL_RP   (1)
#define QREGS_SER_SEL_QNA1 (2)
int qregs_ser_sel(int sel);
extern char *qregs_ser_names[];

void qregs_ser_flush(void);
void qregs_ser_set_params(int *baud_Hz, int parity, int en_xonxoff);
int  qregs_ser_tx(char c);    // ret 0=success, 1=timo.
int  qregs_ser_rx(char *c_p); // ret 0=success, 1=timo
void qregs_ser_set_timo_ms(int timo_ms);
int  qregs_ser_do_cmd(char *cmd, char *rsp, int rsp_len, int skip_echo);
void qregs_ser_set_params(int *baud_Hz, int parity, int en_xonxoff);
int  qregs_ser_qna_connect(char *irsp, int irsp_len);

extern qregs_st_t st;

void qregs_dbg_print_regs(void);
void qregs_dbg_get_info(int *info);
void qregs_dbg_print_tx_status(void);
void qregs_set_dbg_clk_sel(int sel);

void qregs_clr_adc_status(void);
void qregs_clr_tx_status(void);
void qregs_clr_corr_status(void);
  
void qregs_print_adc_status(void); // for dbg
void qregs_print_hdr_det_status(void);


int qregs_rp_shutdown(void);
int qregs_rp_reboot(void);
int qregs_rp_info(char *str, int len_byts);

#endif
