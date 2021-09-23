/*
 * FreeRTOS STM32 Reference Integration
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
 */

/**
 * @file ota_pal.c Contains non-secure side platform abstraction layer implementations
 * for AWS OTA update library.
 */

#include <string.h>

#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "task.h"

#include "ota_pal.h"
#include "stm32u5xx.h"
#include "stm32u5xx_hal_flash.h"
#include "lfs.h"
#include "lfs_port.h"


#define FLASH_START_CURRENT_BANK       ((uint32_t)FLASH_BASE)

#define FLASH_START_ALT_BANK           ((uint32_t)(FLASH_BASE + FLASH_BANK_SIZE ))

#define NUM_QUAD_WORDS( length )       ( length >> 4UL )

#define NUM_REMAINING_BYTES( length )  ( length & 0x0F )

#define IMAGE_CONTEXT_FILE_NAME        "/ota_metadata"

typedef struct OtaImageContext
{
	uint32_t bank;
	uint32_t ulBaseAddress;
	uint32_t ulImageSize;
	OtaPalImageState_t state;

} OtaImageContext_t;

static FLASH_OBProgramInitTypeDef    OBInit;

static OtaImageContext_t imageContext;

static BaseType_t prvLoadImageContextFromFlash( OtaImageContext_t *pContext )
{
	lfs_ssize_t lReturn = LFS_ERR_CORRUPT;
	BaseType_t status = pdFALSE;
	lfs_t * pLfsCtx = pxGetDefaultFsCtx();
	const char *pcFileName = IMAGE_CONTEXT_FILE_NAME;

	lfs_file_t xFile = { 0 };

	/* Open the file */
	lReturn = lfs_file_open( pLfsCtx, &xFile, pcFileName, LFS_O_RDONLY );

	/* Read the header */
	if( lReturn == LFS_ERR_OK )
	{
		lReturn = lfs_file_read( pLfsCtx, &xFile, pContext, sizeof( OtaImageContext_t ) );

		if( lReturn == sizeof( OtaImageContext_t ) )
		{
			status = pdTRUE;
		}
		else
		{
			LogError(( " Failed to read OTA image context from file %s, error = %d.\r\n ", pcFileName, lReturn ));
		}

		( void ) lfs_file_close( pLfsCtx, &xFile );

	}

	return status;

}

static BaseType_t prvSaveImageContextToFlash( OtaImageContext_t * pContext )
{
	lfs_ssize_t lReturn = LFS_ERR_CORRUPT;
	BaseType_t status = pdFALSE;
	lfs_t * pLfsCtx = pxGetDefaultFsCtx();
	const char *pcFileName = IMAGE_CONTEXT_FILE_NAME;

	lfs_file_t xFile = { 0 };

	/* Open the file */
	lReturn = lfs_file_open( pLfsCtx, &xFile, pcFileName, LFS_O_WRONLY | LFS_O_TRUNC );

	/* Read the header */
	if( lReturn == LFS_ERR_OK )
	{
		lReturn = lfs_file_write( pLfsCtx, &xFile, pContext, sizeof( OtaImageContext_t ) );

		if( lReturn == LFS_ERR_OK )
		{
			status = pdTRUE;
		}
		else
		{
			LogError(( "Failed to save OTA image context to file %s, error = %d.\r\n", pcFileName, lReturn ));
		}

		( void ) lfs_file_close( pLfsCtx, &xFile );

	}
	else
	{
		LogError(( "Failed to open file %s to save OTA image context, error = %d.\r\n ", pcFileName, lReturn ));
	}

	return status;

}

static BaseType_t prvRemoveImageContextFromFlash( void )
{
	lfs_ssize_t lReturn = LFS_ERR_CORRUPT;
	BaseType_t status = pdFALSE;
	lfs_t * pLfsCtx = pxGetDefaultFsCtx();
	const char *pcFileName = IMAGE_CONTEXT_FILE_NAME;
	struct lfs_info xFileInfo = { 0 };

	if( lfs_stat( pLfsCtx, pcFileName, &xFileInfo ) == LFS_ERR_OK )
	{
		/* Open the file */
		lReturn = lfs_remove( pLfsCtx, pcFileName );
		if( lReturn == LFS_ERR_OK )
		{
			status = pdTRUE;
		}
	}
	else
	{
		status = pdTRUE;
	}

	return status;

}

static HAL_StatusTypeDef prvFlashSetDualBankMode( void )
{
	HAL_StatusTypeDef status = HAL_ERROR;

    /* Allow Access to Flash control registers and user Flash */
	status = HAL_FLASH_Unlock();

	if( status == HAL_OK )
	{
		/* Allow Access to option bytes sector */
		status = HAL_FLASH_OB_Unlock();

		if( status == HAL_OK )
		{
			/* Get the Dual bank configuration status */
			HAL_FLASHEx_OBGetConfig( &OBInit );

			if( ( OBInit.USERConfig & OB_DUALBANK_DUAL ) != OB_DUALBANK_DUAL )
			{
				OBInit.OptionType = OPTIONBYTE_USER;
				OBInit.USERType   = OB_USER_DUALBANK;
				OBInit.USERConfig = OB_DUALBANK_DUAL;
				status = HAL_FLASHEx_OBProgram ( &OBInit );

			}

			( void ) HAL_FLASH_OB_Lock();
		}

		( void ) HAL_FLASH_Lock();
	}

	return status;

}

static HAL_StatusTypeDef prvSwapBankAndBoot( void )
{
	HAL_StatusTypeDef status = HAL_ERROR;

    /* Allow Access to Flash control registers and user Flash */
	status = HAL_FLASH_Unlock();

	if( status == HAL_OK )
	{
		/* Allow Access to option bytes sector */
		status = HAL_FLASH_OB_Unlock();

		if( status == HAL_OK )
		{
			/* Get the Dual boot configuration status */
			HAL_FLASHEx_OBGetConfig( &OBInit );

			OBInit.OptionType = OPTIONBYTE_USER;
			OBInit.USERType   = OB_USER_SWAP_BANK;
			if( ( ( OBInit.USERConfig ) & ( OB_SWAP_BANK_ENABLE ) ) != OB_SWAP_BANK_ENABLE)
			{
				OBInit.USERConfig = OB_SWAP_BANK_ENABLE;
			}
			else
			{
				OBInit.USERConfig = OB_SWAP_BANK_DISABLE;
			}
			status = HAL_FLASHEx_OBProgram ( &OBInit );

			if( status == HAL_OK )
			{
				/* Generate System Reset to load the new option byte values ***************/
				/* On successful option bytes loading the system should reset and control should not return from this function. */
				status = HAL_FLASH_OB_Launch();
			}

			( void ) HAL_FLASH_OB_Lock();
		}

		( void ) HAL_FLASH_Lock();
	}

	return status;
}

static uint32_t prvGetAlternateBank( void )
{
	HAL_StatusTypeDef status = HAL_ERROR;
	uint32_t ulBank = 0UL;

	status = HAL_FLASH_Unlock();
	if( status == HAL_OK )
	{
		status = HAL_FLASH_OB_Unlock();
		if( status == HAL_OK )
		{
			/* Get the Dual boot configuration status */
			HAL_FLASHEx_OBGetConfig( &OBInit );
			ulBank = ( ( OBInit.USERConfig &  OB_SWAP_BANK_ENABLE ) == OB_SWAP_BANK_ENABLE ) ? FLASH_BANK_1 : FLASH_BANK_2;

			( void ) HAL_FLASH_OB_Lock();

		}

		( void )  HAL_FLASH_Lock();
	}

	return ulBank;

}

static HAL_StatusTypeDef prvWriteToFlash(uint32_t destination, uint8_t * pSource, uint32_t length)
{
  HAL_StatusTypeDef status = HAL_OK;
  uint32_t i = 0U;
  uint32_t quadWord[4] = { 0 };
  uint32_t numQuadWords = NUM_QUAD_WORDS( length );
  uint32_t remainingBytes = NUM_REMAINING_BYTES( length );

  /* Unlock the Flash to enable the flash control register access *************/
  HAL_FLASH_Unlock();

  for (i = 0U; i < numQuadWords; i++ )
  {
	  /* Device voltage range supposed to be [2.7V to 3.6V], the operation will
       be done by word */

	  memcpy( quadWord, pSource, 16UL );
	  status = HAL_FLASH_Program( FLASH_TYPEPROGRAM_QUADWORD, destination, ( uint32_t ) quadWord );
	  if( status == HAL_OK )
	  {
		  /* Check the written value */
		  if( memcmp( ( void * ) destination, quadWord, 16UL ) != 0 )
		  {
			  /* Flash content doesn't match SRAM content */
			  status = HAL_ERROR;
		  }
	  }

	  if( status == HAL_OK )
	  {
		  /* Increment FLASH destination address and the source address. */
		  destination += 16UL;
		  pSource += 16UL;
	  }
	  else
	  {
		  break;
	  }
  }

  if( status == HAL_OK )
  {
	  if( remainingBytes > 0 )
	  {
		  memcpy( quadWord, pSource, remainingBytes );
		  memset( ( quadWord + remainingBytes ) , 0xFF, ( 16UL - remainingBytes ) );

		  status = HAL_FLASH_Program( FLASH_TYPEPROGRAM_QUADWORD, destination, ( uint32_t ) quadWord );
		  if( status == HAL_OK )
		  {
			  /* Check the written value */
			  if( memcmp( ( void * ) destination, quadWord, 16UL ) != 0 )
			  {
				  /* Flash content doesn't match SRAM content */
				  status = HAL_ERROR;
			  }
		  }
	  }
  }

  /* Lock the Flash to disable the flash control register access (recommended
     to protect the FLASH memory against possible unwanted operation) *********/
  HAL_FLASH_Lock();

  return status;
}

static HAL_StatusTypeDef prvEraseBank( uint32_t bankNumber )
{

	uint32_t error = 0U;
	FLASH_EraseInitTypeDef pEraseInit;
	HAL_StatusTypeDef status = HAL_ERROR;

	/* Unlock the Flash to enable the flash control register access *************/
	status = HAL_FLASH_Unlock();

	if( status == HAL_OK )
	{

		pEraseInit.Banks = bankNumber;
		pEraseInit.NbPages = FLASH_PAGE_NB;
		pEraseInit.Page = 0U;
		pEraseInit.TypeErase = FLASH_TYPEERASE_MASSERASE;

		status = HAL_FLASHEx_Erase(&pEraseInit, &error);

		/* Lock the Flash to disable the flash control register access (recommended
	     to protect the FLASH memory against possible unwanted operation) *********/
		( void ) HAL_FLASH_Lock();
	}

	return status;
}


OtaPalStatus_t xOtaPalCreateImage( OtaFileContext_t * const pFileContext )
{
	OtaPalStatus_t otaStatus = OtaPalRxFileCreateFailed;
	HAL_StatusTypeDef status = HAL_ERROR;
	uint32_t ulOtherBank;

	/* Try to load an image context if one exists in the flash. */
	( void ) prvLoadImageContextFromFlash( &imageContext );

	if( ( pFileContext->pFile == NULL ) &&
		( pFileContext->fileSize <= FLASH_BANK_SIZE ) &&
		( imageContext.state != OtaPalImageStatePendingCommit ) )
	{
		/* Set dual bank mode if not already set. */
		status = prvFlashSetDualBankMode();

		if( status == HAL_OK )
		{
			/* Get Alternate bank and erase the flash. */
			ulOtherBank = prvGetAlternateBank();
			status = prvEraseBank( ulOtherBank );
		}

		if( status == HAL_OK )
		{
			imageContext.bank = ulOtherBank;
			imageContext.ulBaseAddress = FLASH_START_ALT_BANK;
			imageContext.ulImageSize = pFileContext->fileSize;
			imageContext.state = OtaPalImageStateUnknown;
			pFileContext->pFile = ( uint8_t * ) ( &imageContext );
			otaStatus = OtaPalSuccess;
		}
	}

	return otaStatus;
}

int16_t iOtaPalWriteImageBlock ( OtaFileContext_t * const pFileContext,
                                 uint32_t offset,
                                 uint8_t * const pData,
                                 uint32_t blockSize )
{
	int16_t bytesWritten = 0;
	HAL_StatusTypeDef status = HAL_ERROR;

	if( ( pFileContext->pFile == ( uint8_t *) ( &imageContext ) ) &&
		( offset + blockSize ) <= imageContext.ulImageSize )
	{
		status = prvWriteToFlash( ( imageContext.ulBaseAddress + offset ), pData, blockSize );
		if( status == HAL_OK )
		{
			bytesWritten = blockSize;
		}
	}

	return bytesWritten;
}

OtaPalStatus_t xOtaPalFinalizeImage( OtaFileContext_t * const pFileContext )
{
	OtaPalStatus_t otaStatus = OtaPalCommitFailed;

	if( pFileContext->pFile == ( uint8_t *) ( &imageContext ) )
	{
		/* To do signature validation and confirm image here. */
		imageContext.state = OtaPalImageStatePendingCommit;
		otaStatus = OtaPalSuccess;
	}

	return otaStatus;
}


OtaPalStatus_t xOtaPalAbortImage( OtaFileContext_t * const pFileContext )
{
	OtaPalStatus_t otaStatus = OtaPalAbortFailed;
	HAL_StatusTypeDef status = HAL_ERROR;


	if( ( pFileContext->pFile == ( uint8_t *) ( &imageContext ) ) &&
		( imageContext.bank == prvGetAlternateBank() ) )
	{
		status = prvEraseBank( imageContext.bank );
		if( status == HAL_OK )
		{
			imageContext.state = OtaPalImageStateUnknown;
			otaStatus = OtaPalSuccess;
		}
	}

	return otaStatus;
}

OtaPalStatus_t xOtaPalActivateImage( OtaFileContext_t * const pFileContext )
{
	OtaPalStatus_t otaStatus = OtaPalActivateFailed;
	BaseType_t status = pdFALSE;

	if( ( pFileContext->pFile == ( uint8_t *) ( &imageContext ) ) &&
		( imageContext.state  == OtaPalImageStatePendingCommit ) &&
		( imageContext.bank   == prvGetAlternateBank() ) )
	{
		/** Save current image context to flash so as to get the context after boot. */
		status = prvSaveImageContextToFlash( &imageContext );

		if( status == pdTRUE )
		{
			( void )  prvSwapBankAndBoot();

			/** Boot failed. Removed the save context from flash. */

			( void ) prvRemoveImageContextFromFlash();
		}

	}

	return otaStatus;
}

OtaPalStatus_t xOtaPalSetImageState( OtaFileContext_t * const pFileContext,
                                     OtaImageState_t eState )
{
	OtaPalStatus_t otaStatus = OtaPalBadImageState;
	BaseType_t status = pdPASS;
	uint32_t alternateBank;

	if( imageContext.state == OtaPalImageStateUnknown )
	{
		/*
		 * Function is called before starting a new OTA image download. This can occur
		 * either we have booted with the new image in pending commit, or due to an error from
		 * the upper layer (there wont be an OTA image context in flash). Try loading the context
		 * from flash and return error if not found.
		 */
		status = prvLoadImageContextFromFlash( &imageContext );
	}

	if( status == pdPASS )
	{
		switch( eState )
		{
		case OtaImageStateTesting:
			if( ( imageContext.state == OtaPalImageStatePendingCommit ) &&
				( imageContext.bank != prvGetAlternateBank() ) )
			{
				/** New image bank is booted successfully and it's pending for commit. */
				otaStatus = OtaPalSuccess;
			}
			break;

		case OtaImageStateAccepted:
			alternateBank = prvGetAlternateBank();
			if( ( imageContext.state == OtaPalImageStatePendingCommit ) &&
			    ( imageContext.bank != alternateBank ) )
			{
				/** New image bank is booted successfully and it have passed self test. Make it as accepted
				 * by removing the image context from flash and setting image state to valid. */

				prvRemoveImageContextFromFlash();
				( void ) prvEraseBank( alternateBank );
				imageContext.state = OtaPalImageStateValid;
				otaStatus = OtaPalSuccess;
			}
			break;

		case OtaImageStateRejected:
			if( ( imageContext.state == OtaPalImageStatePendingCommit ) &&
			    ( imageContext.bank != prvGetAlternateBank() ) )
			{
				prvRemoveImageContextFromFlash();
				( void ) prvSwapBankAndBoot();

			}
			otaStatus = OtaPalRejectFailed;
			break;

		case OtaImageStateAborted:
			if( imageContext.bank != prvGetAlternateBank() )
			{
				/* We are booting from the new bank and the image is pending commit. */
				if( imageContext.state == OtaPalImageStatePendingCommit )
				{
					prvRemoveImageContextFromFlash();
					( void ) prvSwapBankAndBoot();
					otaStatus = OtaPalAbortFailed;
				}
			}
			else
			{
				otaStatus = xOtaPalAbortImage( pFileContext );
			}
			break;

		default:
			LogError(( "Unknown state transition, current state = %d, expected state = %d.\r\n",imageContext.state, eState ));
			break;
		}
	}


	return otaStatus;
}


OtaPalImageState_t xOtaPalGetImageState( OtaFileContext_t * const pFileContext )
{
	OtaPalImageState_t state = OtaPalImageStateUnknown;
	BaseType_t status = pdPASS;

	if(imageContext.state == OtaPalImageStateUnknown )
	{
		/*
		 * API can be called before starting a new OTA image download. This can occur
		 * when OTA image enters self test mode after activation of new image.
		 * Try to get the state of the image stored in flash.
		 */
		status = prvLoadImageContextFromFlash( &imageContext );
	}

	if( status == pdPASS )
	{
		state = imageContext.state;
	}

	return state;
}


static HAL_StatusTypeDef prvCopyImageToAlternateBank( void )
{
	HAL_StatusTypeDef status = HAL_ERROR;
	uint32_t ulAltBank = prvGetAlternateBank();

	if( ulAltBank == FLASH_BANK_2)
	{
		LogInfo(("We are in bank 1, reprogramming bank 2..\r\n"));

		status = prvEraseBank( FLASH_BANK_2 );
		LogInfo(("Erasing bank 2 completed, status = %d\r\n", status ));

	}
	else if( ulAltBank == FLASH_BANK_1  )
	{
		LogInfo(("We are in bank 2, reprogramming bank 1..\r\n"));
		status = prvEraseBank( FLASH_BANK_1 );
		LogInfo(("Erasing bank 1 completed, status = %d\r\n", status ));
	}
	else
	{
		status = HAL_ERROR;
	}

	if( status == HAL_OK )
	{
		LogInfo(("Copying image to alternate bank..\r\n"));
		//status = prvWriteToFlash( FLASH_START_ALT_BANK, (uint32_t *) FLASH_START_CURRENT_BANK, FLASH_BANK_SIZE );
	}

	return status;

}



void Task_SwitchBank( void * pvParameters )
{

	HAL_StatusTypeDef status = HAL_ERROR;

	LogInfo(("Setting dual bank mode.\r\n"));
	status = prvFlashSetDualBankMode();

	while( 1 )
	{
		//status = prvLoadFlashConfiguration();
		if( status == HAL_OK )
		{
			LogInfo(("Dual bank mode: %d, swap_bank: %d\r\n",
					( ( OBInit.USERConfig & OB_DUALBANK_DUAL ) == OB_DUALBANK_DUAL ),
					( ( OBInit.USERConfig & OB_SWAP_BANK_ENABLE ) == OB_SWAP_BANK_ENABLE ) ));
		}
		vTaskDelay( pdMS_TO_TICKS( 1000 ) );
	}



	if( status == HAL_OK )
	{
		LogInfo(("Programming alternate bank.\r\n"));
		status = prvCopyImageToAlternateBank();
	}

	if( status == HAL_OK )
	{
		LogInfo(("Swap and boot new bank ..\r\n"));
		status  = prvSwapBankAndBoot();
	}

	if( status != HAL_OK )
	{
		while( 1 )
		{
			LogInfo(( "Failed last step.\r\n" ));
			vTaskDelay(pdMS_TO_TICKS( 1000 ) );
		}
	}

	vTaskDelete(NULL);
}

