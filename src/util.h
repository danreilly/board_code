#ifndef _UTIL_H_
#define _UTIL_H_

void u_print_all(char *str);
int u_max(int a, int b);
int u_min(int a, int b);
void u_pause(char *prompt);

int u_ask_yn(char *prompt, int dflt);
// dflt: 0=no,1=yes,-1=no default
// returns: 0=no, 1=yes

int u_ask_dbl(char *prompt, double dflt);
// returns: a double

#endif
