#include "FreeRTOS.h"
#include <stddef.h>

typedef enum{
	CS_CORE_THING_NAME,
	CS_CORE_MQTT_ENDPOINT,
	CS_WIFI_PREFERRED_AP_SSID,
	CS_WIFI_PREFERRED_AP_CREDENTIALS,
//	CS_WIFI_PREFERRED_AP_AUTH,
//	CS_TLS_VERIFY_CA,
//	CS_TLS_VERIFY_SNI,
//	CS_TIME_HWM,
	CS_NUM_KEYS
} ConfigStoreKey_t;

typedef enum{
	CS_ENTRY_TYPE_CHAR,
	CS_ENTRY_TYPE_BOOL,
	CS_ENTRY_TYPE_INT32,
	CS_ENTRY_TYPE_UINT32,
	CS_ENTRY_TYPE_JSON,
	CS_ENTRY_TYPE_CBOR
} ConfigStoreEntryType_t;

void configStore_init( void );

BaseType_t ConfigStore_getEntryStatic( ConfigStoreKey_t key, void * dataPtr, size_t maxLen );
void * ConfigStore_getEntryData( ConfigStoreKey_t key );
ConfigStoreEntryType_t ConfigStore_getEntryType( ConfigStoreKey_t key );
size_t ConfigStore_getEntrySize( ConfigStoreKey_t key );
