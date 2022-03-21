/*
 * FreeRTOS STM32 Reference Integration
 *
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

#ifndef _OSPI_NOR_DRV
#define _OSPI_NOR_DRV


/*
 *  512 Mbit = 64 MByte
 *  1024 Blocks of 64KByte
 *  16 4096 byte Sectors per Block
 */

#define MX25LM_BLOCK_SZ              ( 64 * 1024 )
#define MX25LM_SECTOR_SZ             ( 4 * 1024 )
#define MX25LM_NUM_BLOCKS            ( 1024 )
#define MX25LM_SECTORS_PER_BLOCK     ( 16 )
#define MX25LM_NUM_SECTORS           ( MX25LM_NUM_BLOCKS * MX25LM_SECTORS_PER_BLOCK )
#define MX25LM_MEM_SZ_BYTES          ( 1024 * MX25LM_BLOCK_SZ )

#define OPI_START_ADDRESS            ( 10 * MX25LM_BLOCK_SZ )

#define MX25LM_NUM_SECTOR_USABLE     ( 1024 - 10 )
#define MX25LM_MEM_SZ_USABLE         ( MX25LM_NUM_SECTOR_USABLE * MX25LM_SECTOR_SZ )

#define MX25LM_DEFAULT_TIMEOUT_MS    ( 1000 )


#define MX25LM_8READ_DUMMY_CYCLES    ( 20 )

/* SPI mode command codes */
#define MX25LM_SPI_WREN              ( 0x06 )
#define MX25LM_SPI_WRCR2             ( 0x72 )
#define MX25LM_SPI_RDSR              ( 0x05 )

/* CR2 register definition */
#define MX25LM_REG_CR2_0_SPI         ( 0x00 )
#define MX25LM_REG_CR2_0_SOPI        ( 0x01 )
#define MX25LM_REG_CR2_0_DOPI        ( 0x02 )

#define MX25LM_REG_SR_WIP            ( 0x01 )   /* Write in progress  */
#define MX25LM_REG_SR_WEL            ( 0x02 )   /* Write enable latch */

/* OPI mode commands */
#define MX25LM_OPI_RDSR              ( 0x05FA )
#define MX25LM_OPI_WREN              ( 0x06F9 )
#define MX25LM_OPI_8READ             ( 0xEC13 )
#define MX25LM_OPI_PP                ( 0x12ED ) /* Page Program, starting address must be 0 in DTR OPI mode */
#define MX25LM_PROGRAM_FIFO_LEN      ( 256 )
#define MX25LM_OPI_SE                ( 0x21DE ) /* Sector Erase */

#define MX25LM_WRITE_TIMEOUT_MS      ( 10 * 1000 )
#define MX25LM_ERASE_TIMEOUT_MS      ( 10 * 1000 )
#define MX25LM_READ_TIMEOUT_MS       ( 10 * 1000 )


BaseType_t ospi_Init( OSPI_HandleTypeDef * pxOSPI );

BaseType_t ospi_WriteAddr( OSPI_HandleTypeDef * pxOSPI,
                           uint32_t ulAddr,
                           const void * pxBuffer,
                           uint32_t ulBufferLen,
                           TickType_t xTimeout );

BaseType_t ospi_EraseSector( OSPI_HandleTypeDef * pxOSPI,
                             uint32_t ulAddr,
                             TickType_t xTimeout );

BaseType_t ospi_ReadAddr( OSPI_HandleTypeDef * pxOSPI,
                          uint32_t ulAddr,
                          void * pxBuffer,
                          uint32_t ulBufferLen,
                          TickType_t xTimeout );


#endif /* _OSPI_NOR_DRV */
