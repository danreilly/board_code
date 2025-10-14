#include "util.h"
#include <stdio.h>


void u_pause(char *prompt) {
  char buf[256];
  if (prompt && prompt[0])
    printf("%s > ", prompt);
  else
    printf("hit enter > ");
  scanf("%[^\n]", buf);
  getchar();
}

void u_print_all(char *str) {
  unsigned char c;
  while(c=*str++) {
    if ((c>=' ')&&(c<127)) printf("%c", c);
    else printf("<%d>", c);
  }
}


int u_max(int a, int b) {
  return a>b?a:b;
}

int u_min(int a, int b) {
  return a<b?a:b;
}


int u_ask_yn(char *prompt, int dflt) {
  char c;
  char buf[32];
  int n, v=-1;
  while (v<0) {
    if (dflt>=0)
      printf("%s (y/n) ? [%c] > ", prompt, dflt?'y':'n');
    else
      printf("%s (y/n) ? > ", prompt);

    n=scanf("%[^\n]", buf);
    getchar(); // get cr
    if (n==1)
      n=sscanf(buf, "%c", &c);
    if (n==0) v=dflt;
    else if ((c=='Y')||(c=='y')||(c=='1')) v=1;
    else if ((c=='N')||(c=='n')||(c=='0')) v=0;
  }
  return v;
}

int u_ask_dbl(char *prompt, double dflt) {
  double d,v=dflt;
  char buf[32];
  int n;
  printf("%s (y/n) ? [%lg] > ", prompt, dflt);

  n=scanf("%[^\n]", buf);
  getchar(); // get cr
  if (n==1) {
    n=sscanf(buf, "%lg", &d);
    if (n==1) v=d;
  }
  return v;
}


