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

#include "FreeRTOS.h"
#include "string.h"

/* copied from heap_4.c */
typedef struct A_BLOCK_LINK
{
    struct A_BLOCK_LINK * pxNextFreeBlock; /*<< The next free block in the list. */
    size_t xBlockSize;                     /*<< The size of the free block. */
} BlockLink_t;

/* Override newlibc memory allocator functions */
void * malloc( size_t xLen )
{
    return pvPortMalloc( xLen );
}

void * calloc( size_t xNum, size_t xLen )
{
	void * pvBuffer = pvPortMalloc( xNum * xLen );

	if( pvBuffer != NULL )
	{
		( void ) memset( pvBuffer, 0, xNum * xLen );
	}

    return pvBuffer;
}

#define SIZE_MASK  ( ~( 0b1 << 31 ) )
static const uintptr_t xHeapStructSize = ( sizeof( BlockLink_t ) + ( ( size_t ) ( portBYTE_ALIGNMENT - 1 ) ) ) & ~( ( size_t ) portBYTE_ALIGNMENT_MASK );

size_t malloc_usable_size( void * pvPtr )
{
    BlockLink_t * pxLink;
    uint8_t * puc;
    size_t xLen = 0;

    if( pvPtr != NULL )
    {
        configASSERT( ( uintptr_t ) pvPtr > xHeapStructSize );
        puc = ( uint8_t * ) pvPtr - xHeapStructSize;

        pxLink = ( void * ) puc;

        xLen = ( SIZE_MASK & pxLink->xBlockSize ) - xHeapStructSize;
        configASSERT( xLen >= 0 );
    }
    return xLen;
}

/* non-optimized realloc implementation */
void * realloc( void * pvPtr, size_t xNewLen )
{
    void * pvNewBuff = NULL;
    size_t xCurLen = 0;

    if( pvPtr == NULL )
    {
        pvNewBuff = pvPortMalloc( xNewLen );
    }
    else /* pvPtr is not NULL */
    {
        xCurLen = malloc_usable_size( pvPtr );
        /* New length is zero, free the block and return null */
        if( xNewLen == 0 )
        {
            vPortFree( pvPtr );
        }
        else
        {
            pvNewBuff = pvPortMalloc( xNewLen );
        }

        if( pvNewBuff != NULL )
        {
            if( xCurLen >= xNewLen )
            {
                ( void )memcpy( pvNewBuff, pvPtr, xNewLen );
            }
            else  /* xCurLen < xNewLen */
            {
                ( void )memcpy( pvNewBuff, pvPtr, xCurLen );
            }
            /* Free the original buffer */
            vPortFree( pvPtr );
        }
    }
    /* Return the new buffer with copied data */
    return pvNewBuff;
}

void * reallocf( void * pvPtr, size_t xNewLen )
{
    void * pvNewBuff = realloc( pvPtr, xNewLen );

    if( pvPtr != NULL &&
        xNewLen > 0 &&
        pvNewBuff == NULL )
    {
        vPortFree( pvPtr );
    }
    return pvNewBuff;
}

void free( void * pvPtr )
{
    return vPortFree( pvPtr );
}

/* Not supported */
void * memalign( size_t xAlignment, size_t xLen )
{
    return NULL;
}

/* Not supported */
void * _sbrk ( intptr_t xIncrement )
{
	return NULL;
}
