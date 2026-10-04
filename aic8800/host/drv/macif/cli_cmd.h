#ifndef _CLI_CMD_H_
#define _CLI_CMD_H_

#include "rwnx_platform.h"
#include "reg_access.h"
//#include "hal_desc.h"
#include "rwnx_main.h"
#include "rwnx_msg_tx.h"
//#include "log.h"
#include "rwnx_defs.h"
#include "lmac_msg.h"

int handle_private_cmd(struct rwnx_hw *rwnx_hw, char *command);
unsigned int command_strtoul(const char *cp, char **endp, unsigned int base);

int aic_cli_run_cmd(char *CmdBuffer);
bool aic_cli_cmd_init(struct rwnx_hw *rwnx_hw);
bool aic_cli_cmd_deinit(struct rwnx_hw *rwnx_hw);

#endif