#ifndef _RP_H_
#define _RP_H_

#include "qregs_ll.h"

typedef struct rp_st {
  int foo;
} rp_st_t;
extern rp_st_t rp_st;

// DEPRECATED
typedef struct rp_status_st {
  double pilot_pwr_V;
  double mean_pwr_V;
  double body_pwr_V;
  double dark_pwr_V;
  double ext_rat_dB;
  double body_rat_dB;
} rp_status_t;

#define RP_MAX_BINS (64)
typedef struct rp_hist_st {
  double pilot_pwr_V;
  double mean_pwr_V;
  double body_pwr_V;
  double dark_pwr_V;
  double ext_rat_dB;
  double body_rat_dB;
  double bins[RP_MAX_BINS];
} rp_hist_t;
int rp_meas_pwr_hist(int ch, rp_hist_t *rval);

typedef struct rp_set_st {
  double bias_V[2];
  int fdbk_en[2];
} rp_set_t;
int rp_get_settings(rp_set_t *set);


// These return 0 on success,
// or typically QREGS_ERR_FAIL on error

int rp_get_status(rp_status_t *stat);
int rp_cfg_frames(int frame_pd_asamps, int pilot_dur_asamps);
int rp_shutdown(void);
int rp_reboot(void);
int rp_measdark(void);
int rp_info(char *str, int strlen);
int rp_set_bias(int ch, double bias_V);


#endif
