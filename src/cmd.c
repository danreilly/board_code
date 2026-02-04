#include "cmd.h"
#include "parse.h"
#include "string.h"
#include <stdio.h>



//char inbyte(void);
#define inbyte getchar

#define CMD_MAXPATH 64
char cmd_path[CMD_MAXPATH];


void cmd_print_err(char *s) {
  printf("ERR: %s\n", s);
}
void cmd_print_errcode(int err) {
  switch(err) {
    case 0:  break;
    case CMD_ERR_MT_LINE   : break;
    case CMD_ERR_AMBIGUOUS : cmd_print_err("ambiguous"); break;
    case CMD_ERR_SYNTAX    : cmd_print_err("syntax");  break;
    case CMD_ERR_BAD_CMD   : cmd_print_err("bad cmd"); break;
    case CMD_ERR_NO_INT    : cmd_print_err("no int");  break;
    case CMD_ERR_BAD_VAL   : cmd_print_err("bad val"); break;
    case CMD_ERR_ABORTED   : cmd_print_err("aborted"); break;
    case CMD_ERR_TIMO      : cmd_print_err("timeout"); break;
    case CMD_ERR_BUG       : cmd_print_err("BUG"); break;
    case CMD_ERR_FAIL:
    default:            cmd_print_err("failed"); break;
  }
}

#define DESCCOL 20
int cmd_help(cmd_info_t *ci_p) {
  int i, j;
  char *name;
  for(i=0; ci_p[i].fn; ++i);
  name=ci_p[i].name;
  for(i=0; ci_p[i].fn; ++i) {
    if (ci_p[i].desc != CMD_ALIAS) {
      printf("  ");
      if (name) {
	printf(name);
	j=strlen(name);
	printf(" ");
      } else j=0;
      printf(ci_p[i].name);
      j+=1+strlen(ci_p[i].name);
      if (ci_p[i].usage) {
	if (ci_p[i].arg==CMD_PERDET) {
	  printf(" {detid}"); j+=8;
	}
	printf(" ");
	printf(ci_p[i].usage);
	j+=1+strlen(ci_p[i].usage);
      }
      if (ci_p[i].desc) {
        for(; j<DESCCOL; ++j)
	  printf(" ");
	printf(ci_p[i].desc);
      }
      printf("\n");

      if ((i%20)==19) {
	printf("(hit any key)");
        inbyte();
	printf("\r\x1b[K\x00"); //  cr, vt100_erase_to_eol
      }
    }
  }
  return 0;
}


int cmd_do_token(char *token, cmd_info_t *ci_p) {
  int i, j=0, l, cnt;
  const cmd_info_t *p;
  if(!token[0]) return CMD_ERR_SYNTAX;
  l = strlen(token); cnt=0;
  if ((l==1)&&(token[0]=='h'))
    return cmd_help(ci_p);
  for(i=0; ci_p[i].fn; ++i) {
    if (!strncmp(ci_p[i].name, token, l)) {
      j=i;
      if (ci_p[i].name[l]) ++cnt;
      else {cnt=1; break;} // exact match
    }
  }
  if (cnt==1) {
    p=ci_p[j].arg;
    if (p==CMD_PERDET) {
      if (parse_int(&i)) return CMD_ERR_NO_INT;
      if ((i<0) || (i>3)) return CMD_ERR_BAD_VAL;
    }
    return (*ci_p[j].fn)((int)p);
  }
  else if (cnt==0) return CMD_ERR_BAD_CMD;
  else             return CMD_ERR_AMBIGUOUS;
}

char cmd_token[512];

int cmd_subcmd(int arg) {
  cmd_info_t *ci_p = (cmd_info_t *)arg;
  if (cmd_path[0])  strcat(cmd_path, " ");
  strcat(cmd_path, ci_p[0].name);
  return cmd_do_token(parse_token(cmd_token, 512), ci_p);
}

int cmd_exec(char *line, cmd_info_t *ci_p) {
// parses and executes a null-terminated string
// returns CMD_ERR_*
  char *t;
  int err;
  parse_set_line(line);
  t=parse_token(cmd_token, 512);
  if(!t[0]) return CMD_ERR_MT_LINE;
  cmd_path[0]=0;
  err = cmd_do_token(t, ci_p);
  /*
  switch(err) {
    case 0             : printf("\n"); break;
    case CMD_ERR_MT_LINE   : break;
    case CMD_ERR_AMBIGUOUS : cmd_print_err("ambiguous"); break;
    case CMD_ERR_SYNTAX    : cmd_print_err("syntax");  break;
    case CMD_ERR_BAD_CMD   : cmd_print_err("bad cmd"); break;
    case CMD_ERR_NO_INT    : cmd_print_err("no int");  break;
    case CMD_ERR_BAD_VAL   : cmd_print_err("bad val"); break;
    case CMD_ERR_ABORTED   : cmd_print_err("aborted"); break;
    case CMD_ERR_TIMO      : cmd_print_err("timeout"); break;
    case CMD_ERR_FAIL:
    default:            cmd_print_err("failed"); break;
  }
  */
  return err;
}
