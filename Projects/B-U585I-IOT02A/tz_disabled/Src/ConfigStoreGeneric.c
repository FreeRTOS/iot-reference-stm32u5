#include "ConfigStore.h"
#include <string.h>

#define THING_NAME "test_stm32u5_01"
#define ENDOPOINT_ADDRESS "a3dwi8g3v1qwwi-ats.iot.us-west-2.amazonaws.com"

#define SSID "Guest"
#define AUTH ""
#define WIFI_PASSWORD ""
const uint32_t port = 8883;

typedef struct
{
	ConfigStoreEntryType_t type;
	size_t length; 	/* Length of value portion (excludes type and length fields */
	char * data;
} ConfigStoreTLVEntry;

static ConfigStoreTLVEntry cs[ CS_NUM_KEYS ] =
{
	{ CS_ENTRY_TYPE_CHAR,   sizeof( THING_NAME ), 		    THING_NAME }, 		/* CS_CORE_THING_NAME */
	{ CS_ENTRY_TYPE_CHAR,   sizeof( ENDOPOINT_ADDRESS ), 	ENDOPOINT_ADDRESS },/* CS_CORE_MQTT_ENDPOINT */
	{ CS_ENTRY_TYPE_UINT32, sizeof( uint32_t ),             &port },            /* CS_CORE_MQTT_ENDPOINT_PORT */
	{ CS_ENTRY_TYPE_CHAR,   sizeof( SSID ), 				SSID },				/* CS_WIFI_PREFERRED_AP_SSID */
	{ CS_ENTRY_TYPE_CHAR,   sizeof( AUTH ),				    AUTH },				/* CS_WIFI_PREFERRED_AP_AUTH */
	{ CS_ENTRY_TYPE_CHAR,   sizeof( WIFI_PASSWORD ), 		WIFI_PASSWORD },	/* CS_WIFI_PREFERRED_AP_CREDENTIALS */
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

const void * ConfigStore_getEntryData( ConfigStoreKey_t key )
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
