



#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include "qna_usb.h"
#include <unistd.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <sys/time.h>
//#include "qnicll.h"
#include "util.h"

#define IBUF_LEN (2048)
char ibuf[IBUF_LEN];

int qna_fd[2]={0};
qna_set_err_fn *my_set_errmsg_fn;
int qna_timo_s = 1;

char umsg[IBUF_LEN];
#define RBUG(MSG) return (*my_set_errmsg_fn)(MSG, QNA_ERR_BUG);

#define DO(CALL) {int e=CALL; if (e) return e;}


int max(int a, int b) {
  if (a>b) return a;
  return b;
}

static int set_attrib(int fd, int speed) {

    struct termios tty;

    if (tcgetattr(fd, &tty) < 0)
      RBUG("tcgetattr");

    cfsetospeed(&tty, (speed_t)speed);
    cfsetispeed(&tty, (speed_t)speed);

    tty.c_cflag |= (CLOCAL | CREAD);    /* ignore modem controls */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;         /* 8-bit characters */
    tty.c_cflag &= ~PARENB;     /* no parity bit */
    tty.c_cflag &= ~CSTOPB;     /* only need 1 stop bit */
    tty.c_cflag &= ~CRTSCTS;    /* no hardware flowcontrol */


    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN | ECHOE | ECHOK | ECHONL | ECHOCTL | ICANON);
    //        tty.c_lflag |= ICANON;
    tty.c_oflag &= ~OPOST;

    /* fetch bytes as they become available */
    tty.c_cc[VEOL] = '>'; // end of line char
    tty.c_cc[VMIN]  =  1;
    tty.c_cc[VTIME] = 10; // in deciseconds, so this is 1 second.

    if (tcsetattr(fd, TCSANOW, &tty) != 0)
      RBUG("Error from tcsetattr");
    return 0;
}

int dbg=0;

int qna_wr(int idx, char *str) {
  size_t l;
  ssize_t n;
  l=strlen(str);
  if (dbg) {
    printf("qna_usb w: ");
    // u_print_all(str);
    printf("\n");
  }
  n = write(qna_fd[idx], str, l);
  if (n<0) RBUG("write failed");
  if (n!=l) {
    sprintf(umsg, "did not write %zd chars", l);
    RBUG(umsg);
  }
  if (tcdrain(qna_fd[idx])) RBUG("drain failed");
  return 0;  
}

static int qna_rd(int idx, char *ibuf, int ibuf_len, int timo_ms) {
  ssize_t sz;
  int i;
  char *p;
  struct timeval tvs, tve;
  if (dbg) {
    printf("qna_usb r: ");
  }
  gettimeofday(&tvs, 0);
  
  p=ibuf;
  for(i=0; i<ibuf_len-1; ) {
    sz = read(qna_fd[idx], p, 1);
    
    if (sz<0) {
      if (errno==EAGAIN)
	usleep(1000);
      else {
        if (dbg)
    	printf("rd err %zd\n", sz);
        RBUG("read from qna usb failed");
      }
    }
    if (sz==1) {
      ++i;
      if (*p++=='>') break;
    }
    gettimeofday(&tve, 0);
    if ((int)(tve.tv_sec-tvs.tv_sec) > qna_timo_s) break;
  }
  *p=0;

  if (dbg) {  
    u_print_all(ibuf);
    printf("\n");
  }
  /*  
  sz = read(fd, ibuf, IBUF_LEN-1);
  if (sz<0) RBUG("read from qna usb failed");
  ibuf[(int)sz]=0;
  if (dbg) {
    util_print_all(ibuf);
    printf("\n");
  }
  */
  return 0;
}

int findkey(char *buf, char *key, char *val, int *val_len) {
  char *p, *ep, *m, *buf_e;
  size_t llen, klen, vlen;
  buf_e = buf + strlen(buf);
  *val = 0;
  p = buf;  
  while(p < buf_e) {
    ep = strstr(p, "\n");
    if (ep) llen = ep-p;
    else    llen=strlen(p);
    // printf("llen %d\n", llen);
    m = strstr(p, key);
    if (m && (m < p + llen)) {
      // printf("found %s\n", m);
      klen = strlen(key);
      vlen = max(llen - (m-p) - klen, *val_len-1);
      strncpy(val, m + klen, vlen);
      *(val+vlen)=0;
      *val_len = vlen;
      return 0;
    }
    p = p + llen+1;
  }
  return -1;
}

int qna_isconnected(int idx) {
  return (qna_fd[idx]>0);
}

int qna_usb_connect(int idx, char *devname,  qna_set_err_fn *set_errmsg_fn) {
  char irsp[IBUF_LEN];
  int il;
  int fd;
  char key[16];
  my_set_errmsg_fn = set_errmsg_fn;
  qna_fd[idx]=open(devname, O_RDWR | O_NOCTTY | O_SYNC | O_NONBLOCK );
  if (qna_fd[idx]<0) {
    sprintf(umsg, "cant open usb device %s", devname);
    RBUG(umsg);
  }
  // printf("* opened %s\n", devname);
  if (set_attrib(qna_fd[idx], B115200))
    RBUG("cant set serial usb port attributes");

  DO(qna_wr(idx, "\r"));
  DO(qna_rd(idx, ibuf, IBUF_LEN, 1));
  
  DO(qna_wr(idx, "i\r"));
  DO(qna_rd(idx, ibuf, IBUF_LEN, 1));
  // u_print_all(ibuf);
  il = IBUF_LEN;
  sprintf(key, "QNA%d", idx+1);
  if (findkey(ibuf, key, irsp, &il))
    RBUG("did not iden QNA");
  
  //   printf("got rsp QNA %s\n", irsp);
  return 0;
}


void qna_set_timo_s(int s) {
  qna_timo_s = s;
}

int qna_usb_do_cmd(int idx, char *cmd, char *rsp, int rsp_len) {
  ssize_t n;
  int p_len, ll;
  ssize_t cl, rl;
  char *p;
  
  DO(tcflush(qna_fd[idx], TCIOFLUSH));
  DO(qna_wr(idx, cmd));
  //  DO(qna_wr(idx, "\r")); cmd must have \r
  // printf("%s\n"	 , cmd);
  DO(qna_rd(idx, ibuf, 1024, qna_timo_s));
  
  p = strstr(ibuf, "\n");
  if (p) {
    strncpy(rsp, p+1, rsp_len-1);
    rsp[rsp_len-1]=0;
  }
  if (0) { // old method
  
    cl=strlen(cmd);
    if (strlen(ibuf)+1>cl) {
      rl = strlen(ibuf + cl);
    strncpy(rsp, ibuf+cl+1, rsp_len-1);
    rsp[rl]=0;

    }
  }
  /*  
  p     = rsp;
  p_len = rsp_len-1;
  while(1) {
    n = read(fd, p, 1023);
    if (n<0) RBUG("read error");
    p[n]=0;
    if (strstr(p,">")) return 0;
    p     += n;
    p_len -= n;
  }
  */
  return 0;
}

int qna_usb_disconnect() {
  int i;
  for(i=0;i<2;++i) {
    if (qna_fd[i]>0)
      close(qna_fd[i]);
    qna_fd[i] = 0;
  }
  return 0;
}
