// client side routines
#include "hdl.h"
#include <stdio.h>
#include "tsd.h"
#include "string.h"
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

#include <unistd.h>
#include <linux/reboot.h>
#include <pthread.h>

#define REQ_SZ (1024)
char req[REQ_SZ];
char rsp[REQ_SZ];


// The last error message, in case Caller cares
char hdl_errmsg[REQ_SZ];

int cli_soc=0;



#define DO(CALL) { \
  int e; \
  e = CALL; \
  if (e) return e; \
  }


#define BUG(msg) { \
    sprintf(hdl_errmsg, "BUG: %s", msg); \
    printf("BUG: %s\n", msg); \
    return HDL_ERR_BUG; }


int hdl_connect(char *hostname) {
// err_fn: function to use to report errors.
  int e, l;
  char rx[16];
  struct sockaddr_in srvr_addr;

  //  tsd_err_fn = err_fn;
  cli_soc = socket(AF_INET, SOCK_STREAM, 0);
  if (cli_soc<0) BUG("cant make socket to connect on ");
  
  memset((void *)&srvr_addr, 0, sizeof(srvr_addr));

  srvr_addr.sin_family = AF_INET;
  srvr_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  srvr_addr.sin_port = htons(5000);

  if (!isdigit(hostname[0])) {
    struct hostent *hent;
    struct in_addr **al;
    hent = gethostbyname(hostname);
    if (!hent) {
      printf("ERR: cant lookup %s\n", hostname);
      herror("failed");
    }
    al = (struct in_addr **)hent->h_addr_list;
    printf("is %s\n", inet_ntoa(*al[0]));
  }
  
  e=inet_pton(AF_INET, hostname, &srvr_addr.sin_addr);
  //  e=inet_pton(AF_INET, "10.0.0.5", &srvr_addr.sin_addr);
  if (e<0) BUG("cant convert ip addr");

  e = connect(cli_soc, (struct sockaddr *)&srvr_addr, sizeof(srvr_addr));
  if (e<0) {
    sprintf(hdl_errmsg, "could not connect to host %s", hostname);
    BUG(hdl_errmsg);
  }
  socklen_t sz = sizeof(int);
  l=1;
  e= setsockopt(cli_soc, IPPROTO_TCP, TCP_NODELAY, (void *)&l, sz);
  //  e= setsockopt(cli_soc, IPPROTO_TCP, O_NDELAY, (void *)&l, sz);
  if (e) {
    sprintf(hdl_errmsg, "cant set TCP_NODELAY");
    BUG(hdl_errmsg);
  }

  
  //  e= getsockopt(soc, IPPROTO_TCP, TCP_NODELAY, &l, &sz);
  //  printf("get e %d  l %d\n", e, l);
  //       int setsockopt(int sockfd, int level, int optname,
  //                      const void *optval, socklen_t optlen);
  

  // Capablities are determined by the firmware version,
  // and the demon might not be running on the latest and greatest,

  
  return 0;
}



int hdl_rd(int soc, char *rxbuf, int sz_bytes) {
  int n, e, i, l = tsd_rd_pkt(soc, rxbuf, sz_bytes-1);
  rxbuf[l]=0;
  // printf("rx: ");  util_print_all(rxbuf); printf("\n");
  n = sscanf(rxbuf, "%d", &e);
  if (n!=1) BUG("no errcode rsp");
  if (e) {
    u_print_all(rxbuf);
    BUG(rxbuf);
  }
}


int hdl_do_cmd(char *cmd, char *rsp, int rsp_len) {
  char buf[2048], *p;
  static char rxbuf[1024];
  int e;
  //  printf("buf %s\n", buf);
  DO(tsd_wr_str(cli_soc, cmd));
  e = hdl_rd(cli_soc, rxbuf, 1024);
  p=strstr(rxbuf, " ");
  p = p?(p+1):rxbuf; // ptr to string after first space
  if (e) {
    strcpy(hdl_errmsg, p);
    return HDL_ERR_FAIL;
  }
  strncpy(rsp, p, strlen(p));
  return 0;
}



int hdl_setup(tsd_setup_params_t *params) {
  int e;
  snprintf(req, REQ_SZ, "setup is_alice=%d mode=%d sync=%d, qsdctx=%d",
	  params->is_alice,
	  params->mode,
	  params->alice_syncing,
	  params->alice_txing,
	  params->alice_syncing);
  e=hdl_do_cmd(req, rsp, REQ_SZ);

  // TODO parse rsp
  return e;
}

int hdl_cdm_cfg(hdl_cdm_cfg_t *cfg) {
  return 0;
}

#if 0
int hdl_cdm_cfg(hdl_cdm_cfg_t *cfg) {
  int e;
  hdl_cdm_cfg_t sav = *cfg;
  snprintf(req, REQ_SZ, "cdm cfg is_passive=%d is_wdm=%d sym_len=%d frame_pd=%d probe_len=%d num_iter=%d",
	   cfg->is_passive, cfg->is_wdm, cfg->sym_len_asamps,
	   cfg->frame_pd_asamps, cfg->probe_len_asamps,
	   cfg->num_iter);
  e=hdl_do_cmd(req, rsp, REQ_SZ);

  tsd_parse_key_int("is_passive=", &cfg->is_passive);
  tsd_parse_kval("is_wdm=",     &cfg->is_wdm);

  tsd_parse_kval("sym_len=",   &cfg->sym_len_asamps);
  tsd_parse_kval("probe_len=", &cfg->probe_len_asamps);
  tsd_parse_kval("frame_pd=",  &cfg->frame_pd_asamps);  
  tsd_parse_kval("num_iter=",  &cfg->num_iter);
  if (!e)
    if (memcmp((void *)cfg, (void *)&sav, sizeof(cfg)))
      e = HDL_ERR_PARAM_CHANGE;
  return e;
}
#endif

int hdl_cdm_go(void) {
  int e;
  snprintf(req, REQ_SZ, "cdm go");
  e=hdl_do_cmd(req, rsp, REQ_SZ);
  return e;
}

int hdl_cdm_stop(void) {
  int e;
  snprintf(req, REQ_SZ, "cdm stop");
  e=hdl_do_cmd(req, rsp, REQ_SZ);
  return e;
}

int hdl_disconnect(void) {
  int e;
  snprintf(req, REQ_SZ, "q");
  e=hdl_do_cmd(req, rsp, REQ_SZ);
  return e;
}

