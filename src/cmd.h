// cmd.h
// Dan Reilly

#ifndef _CMD_H_
#define _CMD_H_


#define CMD_DESC(X) X

typedef int cmd_t(int arg);

typedef struct cmd_info_st {
  char  *name; // name of command. or super-command if last in list
  cmd_t *fn;   // pointer to function that performs cmd.  0 marks end of list
  const struct cmd_info_st *arg;
  CMD_DESC(char  *desc;) // 0 if no description.  CMD_ALIAS if same desc as prev.
  char  *usage;
} cmd_info_t;


#define CMD_PERDET ((cmd_info_t *)(2))
/* parse detid and pass to fcn */


#define CMD_ALIAS  CMD_DESC(((char *)1))

#define CMD_ERR_MT_LINE 1
#define CMD_ERR_SYNTAX  2
#define CMD_ERR_NO_INT  3
#define CMD_ERR_BAD_VAL 4
#define CMD_ERR_BAD_CMD 5
#define CMD_ERR_AMBIGUOUS 6
#define CMD_ERR_ABORTED   7
#define CMD_ERR_TIMO      8
#define CMD_ERR_FAIL      9
#define CMD_ERR_QUIT      10
#define CMD_ERR_BUG       11

struct cmd_info_st;


int cmd_help(cmd_info_t *ci_p);

void cmd_print_errcode(int err);

int cmd_exec(char *line, cmd_info_t *ci_p);
// parses and executes a null-terminated string
// returns ERR_*

int cmd_subcmd(int arg);
// Use this as the fn field in a cmd_info_t structure
// to make hierarchical commands

void cmd_print_err(char *s);


#endif
