#ifndef _CLI_H_
#define _CLI_H_

#define CLI_SCRATCH_BUF_LEN       512

char pcCliScratchBuffer[ CLI_SCRATCH_BUF_LEN ];

void Task_CLI( void * pvParameters );
#endif
