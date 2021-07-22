#include "ConfigStore.h"
#include <string.h>

#define THING_NAME "TestSTM32U5Name"
#define ENDOPOINT_ADDRESS "aws.xzy"

#define SSID "heliosiot"
#define AUTH ""
#define WIFI_PASSWORD ""

typedef struct
{
	ConfigStoreEntryType_t type;
	size_t length; 	/* Length of value portion (excludes type and length fields */
	char * data;
} ConfigStoreTLVEntry;

static ConfigStoreTLVEntry cs[ CS_NUM_KEYS ] =
{
	{ CS_ENTRY_TYPE_CHAR, sizeof( THING_NAME ), 		THING_NAME }, 		/* CS_CORE_THING_NAME */
	{ CS_ENTRY_TYPE_CHAR, sizeof( ENDOPOINT_ADDRESS ), 	ENDOPOINT_ADDRESS },/* CS_CORE_MQTT_ENDPOINT */
	{ CS_ENTRY_TYPE_CHAR, sizeof( SSID ), 				SSID },				/* CS_WIFI_PREFERRED_AP_SSID */
	{ CS_ENTRY_TYPE_CHAR, sizeof( AUTH ),				AUTH },				/* CS_WIFI_PREFERRED_AP_AUTH */
	{ CS_ENTRY_TYPE_CHAR, sizeof( WIFI_PASSWORD ), 		WIFI_PASSWORD }		/* CS_WIFI_PREFERRED_AP_CREDENTIALS */
	/* CS_TIME_HWM */
};

void configStore_init( void )
{

}

size_t ConfigStore_getEntrySize( ConfigStoreKey_t key )
{
	if( key < CS_NUM_KEYS )
	{
		return cs[key].length;
	}
	return 0;
}

ConfigStoreEntryType_t ConfigStore_getEntryType( ConfigStoreKey_t key )
{
	if( key < CS_NUM_KEYS )
	{
		return cs[key].type;
	}
	return 0;
}

void * ConfigStore_getEntryData( ConfigStoreKey_t key )
{
	if( key < CS_NUM_KEYS )
	{
		return ( void * ) cs[key].data;
	}
	return NULL;
}

BaseType_t ConfigStore_getEntryStatic( ConfigStoreKey_t key, void * dataPtr, size_t maxLen )
{
	if( key < CS_NUM_KEYS &&
		maxLen >= cs[key].length )
	{
		memcpy( dataPtr, cs[key].data, cs[key].length );
		return pdTRUE;
	}
	return pdFALSE;
}
