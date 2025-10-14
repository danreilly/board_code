#ifndef _TSD_SERVE_
#define _TSD_SERVE_

// for error codes
#include "qregc.h"
#include "corr.h" 
#include <iio.h>


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


// callback to handle data received over QSDC:
typedef int tsd_data_handler_fn_t(int16_t *samples, size_t nsamps);
// should return: 0=succes, or one of HDL_ERR_*

typedef struct tsd_state_st {
  pthread_t thread;
  pthread_mutex_t lock;
  pthread_cond_t cond;
  int thread_cmd;

  lcl_iio_t iio;
  int round_trip_frames; // set by cmd_qsdc_cfg()
  char mode;
  char name[64];
  char hostname[64];

  int  forwarding;

  tsd_data_handler_fn_t *data_handler_fn;
  
} tsd_state_t;


// TODO: should not be exported
extern tsd_state_t tsd_st;

void tsd_set_data_handler(tsd_data_handler_fn_t *fn);


int tsd_cli_do_cmd(char *cmd, char *rsp, int rsp_len);

int tsd_parse_kval(char *key, int *val);
int tsd_parse_key_int(char *key, int *val);
int tsd_parse_key_dbl(char *key, double *val);


int tsd_serve(void);
//int tsd_connect(char *hostname, tsd_err_fn_t *err_fn);

int  lcl_iio_open(lcl_iio_t *p);
int  lcl_iio_chan_en(struct iio_channel *ch, char *name);
int lcl_iio_create_dac_bufs(lcl_iio_t *p, int sz_bytes);
void lcl_iio_close(lcl_iio_t *p);

int tsd_lcl_cdm_cfg(hdl_cdm_cfg_t *cdm_cfg, ssize_t *rx_buf_sz_bytes);
int tsd_lcl_cdm_go(void);
int tsd_lcl_cdm_stop(void);
int tsd_lcl_prepare_im_premphasis(int en);


typedef struct tsd_setup_params_st {
  int is_alice;
  char mode;
  int alice_syncing;
  int alice_txing;
  int frame_qty_to_tx;
  int opt_save;  
  int search;
  int cipher_en, decipher_en;  
  int opt_corr;  // if correlating in C
} tsd_setup_params_t;

int tsd_remote_setup(tsd_setup_params_t *params);
//int tsd_first_action(tsd_setup_params_t *params, lcl_iio_t *iio);
//int tsd_second_action(void);
int tsd_init(void);
int tsd_done(void);

int  tsd_iio_create_rxbuf(lcl_iio_t *iio);
void tsd_iio_destroy_rxbuf(lcl_iio_t *iio);
int  tsd_iio_read(lcl_iio_t *iio);
int  tsd_lcl_iio_open(lcl_iio_t *p);



int cmd_err(int code, char *msg);
int cmd_err_fail(char *msg);
int tsd_err_fail(char *msg);



int wr_str(int soc, char *str);

int tsd_rd_pkt(int soc, char *buf, int buf_sz);
int tsd_wr_pkt(int soc, char *buf, int pkt_sz);
int tsd_wr_str(int soc, char *str);

int tsd_lcl_qsdc_cfg(hdl_qsdc_cfg_t *cfg);
int tsd_lcl_qsdc_go(void);
int tsd_lcl_qsdc_stop(void);

#endif
