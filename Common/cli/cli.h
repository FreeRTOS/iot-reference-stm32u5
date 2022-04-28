#ifndef _CLI_H_
#define _CLI_H_

#define CLI_OUTPUT_SCRATCH_BUF_LEN    512
#define CLI_INPUT_LINE_LEN_MAX        128
#define CLI_PROMPT_STR                "> "
#define CLI_OUTPUT_EOL                "\r\n"

#define CLI_UART_BAUD_RATE            ( 115200 )

#define CLI_PROMPT_LEN                ( 2 )
#define CLI_OUTPUT_EOL_LEN            ( 2 )

/*
 * 115200 bits      1 byte             1 second
 * ------------ X --------------- X  -------- = 11.52 bytes / ms
 *    second       8 + 1 + 1 bits     1000 ms
 *
 * Up to ~115 bytes per 10ms
 */

/* 8 bits per frame + 1 start + 1 stop bit */
#define CLI_UART_BITS_PER_FRAME       ( 8 + 1 + 1 )

#define CLI_UART_FRAMES_PER_SEC       ( CLI_UART_BAUD_RATE / CLI_UART_BITS_PER_FRAME )

#define CLI_UART_RX_HW_TIMEOUT_MS     10

/* 115.2 bytes per 10 ms */
#define CLI_UART_BYTES_PER_RX_TIME    ( CLI_UART_FRAMES_PER_SEC * CLI_UART_RX_HW_TIMEOUT_MS / 1000 )

#define CLI_UART_RX_READ_SZ_10MS      128

#define CLI_UART_TX_WRITE_SZ_5MS      64

#define CLI_UART_RX_STREAM_LEN        512

#define CLI_UART_TX_STREAM_LEN        2304

void Task_CLI( void * pvParameters );



#endif /* ifndef _CLI_H_ */
