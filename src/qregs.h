#ifndef _QREGS_H_
#define _QREGS_H_

#include "hdl.h"


// error codes returned by qregs_ functions:
#define QREGS_ERR_FAIL  1
#define QREGS_ERR_TIMO  2
#define QREGS_ERR_BUG   3
#define QREGS_ERR_PARAM 4
#define QREGS_MAX_ERR   4

char *qregs_last_errmsg(void);
void qregs_print_last_err(void);




// indexed by error code
extern char *qregs_errs[];
#define QREGS_ERRMSG_LEN (1024)


// numbers of things
#define QREGS_NUM_OPSW (3)
#define QREGS_NUM_VOA (5)

typedef struct version_info_struct {
  int quanet_dac_fwver;
  int quanet_adc_fwver;
  int cipher_w;
  int tx_mem_addr_w;
} qregs_version_info_t;


int qregs_get_hdr_detection_stats(int *hdr_cnt, int *mag_tot);


// IM bias
#define QREGS_IM_BIAS_RANGE_V (10.8)


// parameters to correct imperfection of optical hybrid
// and difference in detector efficiencies and amplifier gains.
// HDL computes:
//
// [ I_out]   [ m11 m12 ]   [ I + i_off ]
// [ Q_out] = [ m21 m22 ] * [ Q + q_off ]
typedef struct iq_rebalance_struct {
  int i_off, q_off;
  double m11, m21, m12, m22;
} qregs_rebalance_params_t;


// CDM(reflection tomography) configuration
typedef struct qregs_cdm_cfg_struct {
  int sym_len_asamps;
  int frame_pd_asamps;
  int probe_len_asamps;
  int num_iter;
  
  int probe_qty_to_tx;
  int num_passes;
} qregs_cdm_cfg_t;

// QSDC pilot configuration
typedef struct qregs_pilot_cfg_struct {
  int im_from_mem;
  int im_simple_pilot_dac; // sign extended
  int im_simple_body_dac;  // sign extended
} qregs_pilot_cfg_t;

// describes how Alice inserts data
typedef struct qsdc_data_cfg_st {
  int is_qpsk;           // data modulation. 0=bpsk, 1=qpsk
  int pos_asamps;        // start of Alice's data relative to start of frame
  int symbol_len_asamps; // typically 1 or 4. Maybe 8.
  int data_len_asamps;   // portion of each frame that carries Alice's data.
  int bit_dur_syms;      // duration of one bit in units of symbols
} qregs_qsdc_data_cfg_t;

typedef struct qregs_qsdc_other_cfg_st {
  int stream;
  int cipher_en;   // usually 1
  int decipher_en; // usually 1
  int cipher_symlen_asamps;
  int m;
  int dbg_cipher_same;
} qregs_qsdc_other_cfg_t;
// symlen_asamps: symbol length in units of asamps.
//                typically the same as osamp, but could be different
//                for certain experiments.
// m: m = cipher modulation.  m-psk.
//        2=bpsk, 4=qpsk, etc.
void qregs_qsdc_other_cfg(qregs_qsdc_other_cfg_t *other_cfg);



// communication links with QNA and RedPitaya boards
typedef struct ser_state_struct {
  int sel;
  int timo_ms; // -1 means forever
  int baud_Hz;
  int parity;
  int xon_xoff_en;
  char term;
} qregs_ser_state_t;


typedef struct qregs_lo_settings_st {
  int    en;      // 1=enabled
  double pwr_dBm; // setpoint power
  double wl_nm;   // wavelength
  char   mode;    // 'w'=whisper, 'd'=dither
  int    gas_fdbk_en;
  int    gas_goal_offset_MHz; // offset from gasline edge midpoint
} qregs_lo_settings_t;





// Notation:
//   an "asamp" is the duration of one ADC or DAC sample,
//       typically 1/1.23GHz or 1/1GHz.
//
//   a "cyc" is one cycle in the main HDL datapath,
//       and is 4*asamp.

typedef struct qregs_struct {
  qregs_version_info_t ver_info;

  int qna_is_usb[2];
  
  int use_lfsr;
  int lfsr_rst_st;
  int cipher_en;   // 1=txed frame bodies are ciphered
  int decipher_en; // 1=rxed frame bodies are deciphered
  int cipher_m;  // cipher modulation m-psk. 2=bpsk,4=qpsk,etc.
  int cipher_symlen_asamps;  // cipher symbol length in asamps
  int cipher_w;  // HDL detail

  int phase_est_en; // HDL does phase est and derotation

  int tx_always;
  int tx_indefinite;
  int tx_mem_circ;
  int tx_same_hdrs; // used with use_lfsr.  All hdrs will be same.
  int tx_same_cipher;
  int tx_hdr_twopi;

  int alice_syncing; // set by Alice when she syncs to bob
  int alice_txing;   // set by Alice when she txes qsdc
  int tx_pilot_pm_en; // 0 disables phase modulation of pilot
  int frame_pd_asamps; // in units of 1.23GHz ADC samples
  int osamp; // oversampling rate in units of asamps. currently 1 2 or 4
  int hdr_len_bits; // hdr = probe = pilot
  int hdr_len_asamps;
  int body_len_asamps; // frame = hdr + body.  FOR BOB.

  int init_pwr_thresh; // for dbg
  int hdr_pwr_thresh; // used for hdr detection
  int hdr_corr_thresh; // used for hdr detection
  int hdr_im_dly_cycs;
  int pm_dly_cycs;

  char sync_ref; // r=rxclk, p=power, or h=headers
  int sync_dly_asamps; // header sync to DAC ouput delay
  int rx2tx_dly_asamps; // for alice, rx to tx dly
  
  int frame_qty;
  int meas_noise_en;
  double asamp_Hz; // ADC sample rate. typically 1.233GHz.
  
  // int *m;
  int *memmaps[2];
  int *m2;


  int is_bob;
  char tx_go_condition; // 'p'=power,'h'=header','r'=ready,'i'=immediate


  int setflags;

  int uio_fd;

  qregs_ser_state_t ser_state;
  
  qregs_rebalance_params_t rebal;

  qregs_pilot_cfg_t     pilot_cfg;
  qregs_qsdc_data_cfg_t qsdc_data_cfg;
  int qsdc_track_pilots;

  hdl_cdm_cfg_t cdm_cfg;
  int cdm_num_passes;
  int cdm_probe_qty_to_tx;
  
  
  int round_trip_asamps;
  int rx_subcyc_dly_asamps;

  double voa_attn_dB[QREGS_NUM_VOA];
  int opsw_cross[QREGS_NUM_OPSW];

  double xph_deg; // extra phase.  applied after hdr derot
} qregs_st_t;

extern qregs_st_t st;
extern int qregs_fwver;


void qregs_set_init(void);
int qregs_dur_us2samps(double us);
double qregs_dur_samps2us(int s);
void qregs_set_rx_subcyc_dly_asamps(int dly);

int  qregs_init(char *tty0, char *tty1);
// set to 0 or "" if not using ttyUSB
int  qregs_done();

void qregs_set_meas_noise(int en);

void qregs_set_cdm_en(int en);


void qregs_set_lfsr_rst_st(int lfsr_rst_st);
// desc: Sets the inital (reset) state of the lfsr used to
// generate headers. Used to be hardcoded to x50f.

void qregs_set_use_lfsr(int use_lfsr);
// en: 1 = code generates a header from an LFSR, sends to DAC
//     0 = code reads memory and sends that to DAC.
//     

//void qregs_hdr_preemph_en(int en);


void qregs_set_sync_dly_asamps(int sync_dly_asamps);
//   sync_dly_asamps: sync delay in units of 1.23GHz ADC samples.
//                    may be positive or negative.

void qregs_set_rx2tx_dly_asamps(int dly_asamps);
// used for alice.  should be fixed based on HW

void qregs_sync_resync(void);


void qregs_set_iq_rebalance(qregs_rebalance_params_t *params);
//   NOTE: params are changed to what can actually be implemented

void qregs_dbg_set_init_pwr(int init_pwr);
void qregs_set_hdr_det_thresh(int hdr_pwr, int hdr_corr);
void qregs_set_hdr_im_dly_cycs(int hdr_im_dly_cycs);
void qregs_set_pm_dly_cycs(int pm_dly_cycs);
  


void qregs_set_tx_always(int en);
void qregs_set_tx_indefinite(int en);
void qregs_set_tx_pilot_pm_en(int en);
// 1=enable (default), 0=disable pilot phase modulation

void qregs_set_tx_hdr_twopi(int en);

void qregs_set_tx_mem_circ(int en);
// Only has an effect when not using lfsr and txing from mem.
// en: 0 = mem read once, then it stops
//     1 = mem is re-read circularly.

void qregs_set_tx_same_hdrs(int same);
void qregs_set_tx_same_cipher(int same);
// search en is being phased out
void qregs_search_and_txrx(int search_en, int en);
void qregs_txrx_new(int tx_en, int rx_en);

void qregs_halfduplex_is_bob(int en);

// if we dont save after init or pwr, what do we save after?
void qregs_set_save_after_init(int en);
void qregs_set_save_after_pwr(int en);
void qregs_set_save_after_hdr(int en);

int  qregs_set_qsdc_data_cfg(qregs_qsdc_data_cfg_t *data_cfg);

void qregs_set_cdm_cfg(hdl_cdm_cfg_t *cdm_cfg, size_t *rx_buf_sz_bytes);
// sets rx_buf_sz_bytes according to requested params in cdm_cfg.

void qregs_set_alice_txing(int en);
void qregs_set_alice_syncing(int en);
  

void qregs_set_osamp(int osamp);
// osamp: 1,2 or 4


void qregs_set_cdm_frame_pd_asamps(int frame_pd_asamps);
int qregs_set_frame_pd_asamps(int frame_pd_asamps);
// inputs:
//       frame_pd_asamps: requested frame period in units of ADC/DAC samples.
// call this BEFORE qregs_set_hdr_len_bits


// TODO: change to qregs_set_pilot/probe_len_asamps
void qregs_set_hdr_len_bits(int hdr_len_bits);
// call this after qregs_set_frame_pd_asamps


int qregs_cfg_pilot(qregs_pilot_cfg_t *cfg, int autocalc_body_im);


//void qregs_set_im_hdr_dac(int hdr_dac);
  // sets DAC values used during hdr and body for intensity modulator (IM)  
  // in: hdr_dac - value in dac units

void qregs_set_frame_qty(int frame_qty);

void qregs_get_iq_pwr(int *avg, int *max);


// This SFP stuff will be different when Corundum is in place
void qregs_sfp_gth_rst(void);
void qregs_sfp_gth_status(void); // for dbg

void qregs_set_round_trip_asamps(int dly);


void qregs_search_en(int en);
// en: 1=starts a search for hdr

int qregs_set_sync_ref(char s);
// s: 'r'=sync using SFP rxclk
//    'p'=sync using optical power-above-thresh events
//    'h'=sync to arrival of headers

void qregs_alice_sync_en(int en);



// This gets settings of HDL registers.
void qregs_get_hdl_settings(void);
// This gets settings of Aux boards qna1 and qna2,
// and consist of voa settings, optical switches, polarization, laser
int qregs_get_qna_settings(qregs_lo_settings_t *set);



void qregs_print_settings(void);

typedef struct qregs_sync_status_struct {
  int locked;
  int lor; // loss of reference
  int errsum;
  int qty;
  double mean_ref_err_asamps;
} qregs_sync_status_t;

void qregs_get_sync_status(qregs_sync_status_t *s);
void qregs_print_sync_status(void);

void qregs_txrx(int en);


int qregs_set_tx_go_condition(char cond);
// cond:
  //    'h'=start after a header is received (corr above thresh)   
  //    'p'=start after a rise in optical power (pwr above thresh)   
  //    'r'=start after DMA rx buffer is available
  //    'i'=start immediately (when code issues txrx en)

void qregs_zero_mem_raddr(void);

  
// void qregs_get_adc_samp(short int *s_p);
void qregs_get_avgpwr(int *avg, int *mx, int *cnt);

void qregs_dbg_new_go(int en);

// pertaining to laser used as the local oscillator (LO):
int qregs_set_lo_mode(char m);
int qregs_set_lo_en(int en);
int qregs_set_lo_pwr_dBm(double *dBm);
int qregs_set_lo_wl_nm(double *wl_nm);
int qregs_set_lo_offset_MHz(int offset_MHz);
int qregs_set_lo_fdbk_en(int en);
  
typedef struct qregs_lo_status_st {
  int    init_err; // should be zero
  int    gas_lock; 
  int    gas_lock_dur_s;
  double gas_err_rms_MHz; 
  double pwr_dBm;  // laser measures its own output power
} qregs_lo_status_t;
int qregs_get_lo_status(qregs_lo_status_t *status);




// same as rp_hist_t.
// TODO: merge two
typedef struct qregs_pwr_hist_st {
  double pilot_pwr_V;
  double mean_pwr_V;
  double body_pwr_V;
  double dark_pwr_V;
  double ext_rat_dB;
  double body_rat_dB;
  double bins[64];
} qregs_pwr_hist_t;


// Below DEPRECATED, since qregs_pwr_hist_st has more info
typedef struct qregs_frame_pwrs_st {
  double dark_pwr_V;
  double body_pwr_V;
  double ext_rat_dB;
  double body_rat_dB;
} qregs_frame_pwrs_t;
int qregs_measure_frame_pwrs(qregs_frame_pwrs_t *pwr);

void qregs_dbg_print_regs(void);
void qregs_set_phase_est_en(int en, double offset_deg);
// calibration functions

//void qregs_calibrate_bpsk(int en);
void qregs_qsdc_track_pilots(int en);
void qregs_set_memtx_to_pm(int en);



// in box1:
#define QREGS_VOA_QUANT_TX (0)
#define QREGS_VOA_HYB_RX   (1)
// in box2:
#define QREGS_VOA_DATA_TX  (2)
#define QREGS_VOA_DATA_RX  (3)
#define QREGS_VOA_QUANT_RX (4)

extern char *qregs_voa_name[];


int qregs_set_voa_attn_dB(int voa_i, double *attn_dB);
// voa_i: one of QRGES_VOA_*


// L is for the loopback switch.  Normally "cross" but Alice
// sets it to "pass" duing qsdc.
#define QREGS_OPSW_L   (0)
// RX1 is the first recieve switch.
#define QREGS_OPSW_RX2 (1)
#define QREGS_OPSW_RX1 (2)

extern char *qregs_opsw_name[];

int qregs_set_opsw(int sw_i, int *cross);
// sw_i: one of: QREGS_OPSW_L

#endif
