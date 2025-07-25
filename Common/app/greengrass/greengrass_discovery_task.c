/**
  ******************************************************************************
  * @file           : greengrass_discovery_task.c
  * @brief          : Task implementation for AWS Greengrass discovery
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#include "greengrass_discovery_task.h"

#include "logging_levels.h"
#define LOG_LEVEL    LOG_INFO
#include "logging.h"

/* Standard includes. */
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "sys_evt.h"
#include "task.h"
#include "kvstore.h"

/*coreHTTP includes*/
#include "core_http_client.h"

/* tls library includes. */
#include "tls_transport_config.h"
#include "mbedtls_transport.h"

/* Includes to handle reception errors*/
#include "mbedtls/net_sockets.h"       // for MBEDTLS_ERR_NET_CONN_RESET
#include "mbedtls/ssl.h"               // for MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY

/* Include for parameters*/
#include "kvstore_config.h"

/*cJSON parsing library*/
#include "core_json.h"

#define HEADER_BUFFER_LENGTH 1024
#define RESPONSE_BUFFER_LENGTH 2048
#define TIMEOUT_TRANSPORT_AFTER_FIRST_RCV_MS 200
#define TIMEOUT_TRANSPORT_FIRST_RCV_MS 5000
#define DELAY_TRANSPORT_RCV_MS 10
#define PATH_BUFFER_LENGHT 128
#define REGION_BUF_SIZE 38
#define GG_DISCOVERY_URL_SIZE 128
#define DISCOVERY_PORT  8443

/* Greengrass config queries */
#define HOST_ADDRESS_JSON_QUERY_STR "GGGroups[0].Cores[0].Connectivity[0].HostAddress" //Use the HostAdrress of the first Connectivity info of the first Cores of the first GGroup
#define PORT_JSON_QUERY_STR "GGGroups[0].Cores[0].Connectivity[0].PortNumber" //Use the PortNumber of the first Connectivity of the first Cores of the first GGroup
#define CA_JSON_QUERY_STR "GGGroups[0].CAs[0]" // Use the first CA in CAs of the the first GGGroup

/* Labels for writing GG config in NVM */
#define GG_ENDPOINT_LABEL "gg_endpoint"
#define GG_PORT_LABEL "gg_port"

/* ALPN protocols must be a NULL-terminated list of strings. */
static const char * pcAlpnProtocols[] = { NULL};


/* The transport interface send function. */
static int32_t transportSend(NetworkContext_t *pNetworkContext, const void *pBuffer, size_t bytesToSend)
{
	LogDebug( "Http discovery request \n%.*s", bytesToSend, pBuffer );
    return mbedtls_transport_send(pNetworkContext, pBuffer, bytesToSend);
}

/* The transport interface receive function. 
** loop to receive data until having a response */
static int32_t transportRecv(NetworkContext_t *pNetworkContext, void *pBuffer, size_t bufferSize)
{
    int32_t receivedBytes = 0;
    int32_t totalReceived = 0;
    int32_t result = 0;
    TickType_t xTimeout = pdMS_TO_TICKS(TIMEOUT_TRANSPORT_FIRST_RCV_MS);
    TickType_t xStartTime = xTaskGetTickCount();
    TickType_t xElapsedTime = 0;

    while (totalReceived < bufferSize )
    {
        receivedBytes = mbedtls_transport_recv(pNetworkContext, pBuffer, bufferSize - totalReceived);

        if (receivedBytes < 0)
        {
            LogError("mbedtls_transport_recv failed with error code: %d", receivedBytes);
            result = receivedBytes;
            break;
        }
        else if (receivedBytes == 0)
        {
            // No data received, check if timeout has occurred
            xElapsedTime = xTaskGetTickCount() - xStartTime;
            if (xElapsedTime >= xTimeout)
            {
                result = totalReceived;
                break; // Exit the loop if the maximum delay has been reached
            }
            vTaskDelay(pdMS_TO_TICKS(DELAY_TRANSPORT_RCV_MS)); 
        }
        else if (receivedBytes > 0)
        {
            xTimeout = pdMS_TO_TICKS(TIMEOUT_TRANSPORT_AFTER_FIRST_RCV_MS);
            xStartTime = xTaskGetTickCount();
            totalReceived += receivedBytes;
            pBuffer = (uint8_t *)pBuffer + receivedBytes;
            result = totalReceived;
        }
    }

    return result;
}

/**
 * @brief Extracts the AWS region from the given endpoint and formats a Greengrass endpoint URL.
 *
 * This function extracts the region from the provided AWS IoT endpoint and constructs
 * a Greengrass endpoint URL (GGDisccoveryURL) using the extracted region. The region is dynamically determined
 * from the endpoint, avoiding hardcoding the region.
 *
 * @param[out] GGDisccoveryURL The buffer where the formatted Greengrass endpoint will be stored.
 *                    This buffer should be pre-allocated and large enough to hold the resulting URL.
 * @param[in]  endpoint The endpoint string from which the region will be extracted.
 *                      This should be a valid AWS IoT endpoint in the format "something.iot.<region>.amazonaws.com".
 *
 * @return HTTPStatus_t The status of the operation.
 *                      - HTTPSuccess if the region is successfully extracted and the Greengrass endpoint is formatted.
 *                      - HTTPInvalidParameter if the region extraction fails.
 *                      - HTTPInsufficientMemory if memory allocation fails.
 */
HTTPStatus_t create_discovery_url_from_endpoint(char * GGDisccoveryURL , const char * endpoint)
{
    HTTPStatus_t xHTTPStatus = HTTPSuccess;

    if (endpoint == NULL || GGDisccoveryURL == NULL) {
        LogError("Endpoint or GGDisccoveryURL cannot be NULL");
        xHTTPStatus = HTTPInvalidParameter;
    }
    else
    {
        const char* sep = "-ats.iot.";
        char * subStr = strstr ( endpoint, sep );

        if (subStr == NULL) {
            LogError("Separator (%s) not found in endpoint (%s).", sep, endpoint);
            xHTTPStatus = HTTPInvalidParameter;
        }
        else
        {
            subStr += strlen(sep); 

            if (strlen(subStr) + strlen("greengrass-ats.iot.") < GG_DISCOVERY_URL_SIZE) {
                snprintf(GGDisccoveryURL, GG_DISCOVERY_URL_SIZE, "greengrass-ats.iot.%s", subStr);
            } else
            {
                size_t resultingSize = strlen(subStr) + strlen("greengrass-ats.iot.");
                LogError("Resulting GG URL string would be too long. Max size: %d, Actual size: %zu", GG_DISCOVERY_URL_SIZE, resultingSize);
                xHTTPStatus = HTTPInsufficientMemory;
            }
        }
    }
    return xHTTPStatus;
}

/**
 * @brief Extracts a value from a JSON response based on a specified JSON path.
 *
 * This function searches for a value in the JSON response using a given path and validates
 * that the extracted value matches the expected JSON type.
 *
 * @param[in] pResponse The JSON document as a string.
 * @param[in] responseLen The length of the JSON document.
 * @param[in] path The JSON path to the desired value.
 * @param[out] pValue A pointer to the extracted value as a string.
 * @param[out] pValueLen The length of the extracted value.
 * @param[in] expectedType The expected JSON type of the value (e.g., JSONString for CA and HostAddress, JSONNumber for PortNumber).
 *
 * @return JSONStatus_t Returns JSONSuccess if the value is successfully extracted and matches the expected type.
 *                      Returns JSONNotFound if the value is not found or does not match the expected type.
 *
 */
JSONStatus_t extractJsonValue(const char *pResponse, size_t responseLen, const char *path, char **pValue, size_t *pValueLen, JSONTypes_t expectedType)
{
    JSONStatus_t jsonStatus;
    JSONTypes_t valueType;
    
    // Validate the JSON document
    jsonStatus = JSON_Validate(pResponse, responseLen);
    if (jsonStatus != JSONSuccess)
    {
        LogError("Failed to validate JSON response (jsonStatus: %d).", jsonStatus);
    }

    //Extract the value
    if (jsonStatus == JSONSuccess)
    {
        jsonStatus = JSON_SearchT(pResponse, responseLen, path, strlen(path), pValue, pValueLen, &valueType);
        if (jsonStatus != JSONSuccess || valueType != expectedType)
        {
            LogError("%s not found or type error (jsonStatus: %d, valueType: %d).", path, jsonStatus, valueType);
        }
    }

    return jsonStatus;
}

/**
 * @brief Parses a JSON response to extract MQTT endpoint, certificate, and port number.
 *
 * This function takes a JSON response containing information about Greengrass core device,
 * parses it to extract the MQTT endpoint, ca certificate, and port number.
 *
 * @param[in] pResponse Pointer to the JSON response string.
 * @param[in] responseLen Length of the JSON response string.
 * @param[out] pIpAddress Pointer to a char pointer where the extracted endpoint will be stored.
 * @param[out] pCertificate Pointer to a char pointer where the extracted certificate will be stored.
 * @param[out] pPort Pointer to a uint16_t where the extracted port number will be stored.
 *
 * @return JSONStatus_t Returns JSONSuccess on successful parsing and extraction, or an error code
 * indicating the type of failure (e.g., parsing error, object not found, memory allocation failure).
 */
JSONStatus_t parseGGDiscoveryResponse(const char *pResponse, 
                                      size_t responseLen,
                                      char **pIpAddress, 
                                      size_t *pIpAddressLen,
                                      char **pCertificate,
                                      size_t *pCertificateLen,
                                      uint16_t *pPort)
{
    JSONStatus_t jsonStatus = JSONSuccess;

    // Extract HostAddress
    jsonStatus = extractJsonValue(pResponse, responseLen, HOST_ADDRESS_JSON_QUERY_STR, pIpAddress, pIpAddressLen, JSONString);
    if (jsonStatus != JSONSuccess)
    {
        LogError("Failed to extract HostAddress from JSON response" );
    }

     // Extract PortNumber
    if (jsonStatus == JSONSuccess)
    {
        const char *portValue;
        size_t portSize;
        jsonStatus = extractJsonValue(pResponse, responseLen, PORT_JSON_QUERY_STR, &portValue, &portSize, JSONNumber);
        if (jsonStatus != JSONSuccess)
        {
            LogError("Failed to extract PortNumber from JSON response" );
        }
        *pPort = (uint16_t)strtol(portValue, NULL, 10);
    }
    
    // Extract CA
    if (jsonStatus == JSONSuccess){
        jsonStatus = extractJsonValue(pResponse, responseLen, CA_JSON_QUERY_STR, pCertificate, pCertificateLen, JSONString);
        if (jsonStatus != JSONSuccess)
        {
            LogError("Failed to extract CA from JSON response" );
        }
    }
    
    return jsonStatus;
}

/**
 * @brief Writes a CA certificate, endpoint, and port into NVM under specified labels.
 *
 * This function stores the provided CA certificate, Greengrass endpoint, and port number
 * into Non-Volatile Memory (NVM) using the given labels.
 *
 * @param[in] CaLabel Pointer to the string containing the label under which the certificate will be stored.
 * @param[in] certificate Pointer to the string containing the CA certificate in PEM format.
 * @param[in] GGendpoint Pointer to the string containing the Greengrass endpoint.
 * @param[in] ggPort The port number for the Greengrass connection.
 *
 * @return PkiStatus_t Returns PKI_SUCCESS on successful parsing and writing of the certificate,
 * endpoint, and port, or an error code indicating the type of failure.
 */
BaseType_t Write_GG_config_into_NVM(const char *certificate, 
                                    const char *GGendpoint,
                                    uint16_t ggPort )
{
    BaseType_t xResult = pdTRUE;

    /* Write the gg certificate to NVM */
    if(xResult == pdTRUE){
        /* init the cert context */
        mbedtls_x509_crt xCertContext;
        mbedtls_x509_crt_init(&xCertContext);

        int ret = mbedtls_x509_crt_parse(&xCertContext, 
                                        (const unsigned char *)certificate,
                                        strlen(certificate) + 1);
        if (ret != 0)
        {
            LogError("Failed to parse certificate. mbedtls_x509_crt_parse returned -0x%x", -ret);
            xResult = pdFALSE;
        }
        else{

            PkiStatus_t status = xPkiWriteCertificate(TLS_ROOT_GG_CA_CERT_LABEL, &xCertContext);
            if (status == PKI_SUCCESS)
            {
                LogDebug("Success: CA Certificate loaded to label");
            }
            else
            {
                LogError("Error: Failed to save certificate to label");
                xResult = pdFALSE;
            }
        }
        mbedtls_x509_crt_free(&xCertContext);
    }

    /* Write the gg endpoint to NVM */
    if (xResult == pdTRUE)
    {
        KVStoreKey_t xKey_endpoint = kvStringToKey( GG_ENDPOINT_LABEL );

        xResult = KVStore_setString( xKey_endpoint, GGendpoint );
        if(xResult == pdTRUE){
            LogDebug("Success: greengrass endpoint loaded in NVM");
        }
        else
        {
            LogError("Error: Failed to save gg endpoint to NVM (code %d)", xResult);
        }
    }

     /* Write the gg port to NVM */
    if (xResult == pdTRUE)
    {
        KVStoreKey_t xKey_port = kvStringToKey( GG_PORT_LABEL );

        xResult = KVStore_setUInt32( xKey_port, ggPort );
        if(xResult == pdTRUE){
            LogDebug("Success: greengrass port loaded in NVM");
        }
        else
        {
            LogError("Error: Failed to save gg port to NVM (code %d)", xResult);
        }
    }
    
    /* commit changes to NVM */
    if ( xResult == pdTRUE )
    {
        xResult = KVStore_xCommitChanges();
       if( xResult == pdTRUE )
        {
            LogDebug( "Configuration saved to NVM." );
        }
        else
        {
            LogError( "Error: Could not save configuration to NVM (code %d).", xResult );
        }
    }
    
    return xResult;
}

/**
 * Replaces all found instances of the passed substring in the passed string.
 * 
 * @param orig The string in which to look
 * @param rep The substring with which to replace the found substrings
 * @param with The substring to look for
 *
 * @return A new string with the search/replacement performed
 * 
 * @note The caller of the function should ensure to free the memory
 * allocated for the returned string.
 **/
char *str_replace(const char *orig, const char *rep, const char *with) {
    char *result = NULL; // the return string
    BaseType_t xResult = pdTRUE;
    char *ins;    // the next insert point
    char *tmp;    // varies
    int len_rep;  // length of rep (the string to remove)
    int len_with; // length of with (the string to replace rep with)
    int len_front; // distance between rep and end of last rep
    int count;    // number of replacements

    // sanity checks and initialization
    if (!orig || !rep){
        LogError("Original string or substring to replace is NULL.");
        xResult = pdFALSE;
    }
        
    if (xResult == pdTRUE)
    {
        len_rep = strlen(rep);
        if (len_rep == 0){
            LogError("Substring to replace is empty, cannot proceed.");
            xResult = pdFALSE;
        }
    }
    
    if (xResult == pdTRUE)
    {
        if (!with){
            with = "";
            LogWarn("Replacement substring is NULL, using empty string instead.");
        }
            
        len_with = strlen(with);
    
        // count the number of replacements needed
        ins = orig;
        for (count = 0; (tmp = strstr(ins, rep)); ++count) {
            ins = tmp + len_rep;
        }
    
        tmp = result = pvPortMalloc(strlen(orig) + (len_with - len_rep) * count + 1);
    
        if (!result){
            LogError("Memory allocation failed. Required size: %zu", (strlen(orig) + (len_with - len_rep) * count + 1));
            xResult = pdFALSE;
        }
        
        if(xResult == pdTRUE){
            // first time through the loop, all the variable are set correctly
            // from here on,
            //    tmp points to the end of the result string
            //    ins points to the next occurrence of rep in orig
            //    orig points to the remainder of orig after "end of rep"
            while (count--) {
                ins = strstr(orig, rep);
                len_front = ins - orig;
                tmp = strncpy(tmp, orig, len_front) + len_front;
                tmp = strcpy(tmp, with) + len_with;
                orig += len_front + len_rep; // move to next "end of rep"
            }
            strcpy(tmp, orig);
            }
    }
    
    return result;
}

/**
 * @brief Formats the parsed certificate.
 *
 * This function replaces escaped newlines (\\n) in the certificate with newlines (\n) to ensure proper
 * formatting.
 *
 * @param parsedCertificate Pointer to the buffer containing the parsed certificate.
 * @return Pointer to a copy of the formatted certificate.
 * 
 * @note The caller of the function should ensure to free the memory
 * allocated for the returned certificate.
 *
 */
char * copyAndFormatCertificateForNVM(const char *parsedCertificate) {
    
    char * formatedCertificate = str_replace(parsedCertificate, "\\n", "\n");
    return formatedCertificate;

}

/* Discovery Task*/
void vGGDiscoveryTask( void * pvParameters )
{
	HTTPStatus_t xHTTPStatus = HTTPSuccess;
	TlsTransportStatus_t xTlsStatus = TLS_TRANSPORT_CONNECT_FAILURE;
    size_t uxTempSize = 0;

	NetworkContext_t * pxNetworkContext = NULL;
    LogInfo("Starting discovery");

    PkiObject_t xPrivateKey = xPkiObjectFromLabel( TLS_KEY_PRV_LABEL );
    PkiObject_t xClientCertificate = xPkiObjectFromLabel( TLS_CERT_LABEL );
    PkiObject_t pxRootCaChain[ 1 ] = { xPkiObjectFromLabel( TLS_ROOT_CA_CERT_LABEL ) };

    const char *endpoint = KVStore_getStringHeap( CS_CORE_MQTT_ENDPOINT, &uxTempSize );
    uint16_t discovery_port = DISCOVERY_PORT;

    GGDiscoveryContext *pGGContext = ( GGDiscoveryContext *) pvParameters;

    /* Extract region from endPoint */
    char *GGDisccoveryURL = (char *)pvPortMalloc(GG_DISCOVERY_URL_SIZE);
    if (GGDisccoveryURL == NULL)
    {
        LogError( "Failed to allocate memory for GGDisccoveryURL." );
        xHTTPStatus = HTTPInsufficientMemory;
    }else
    {
        xHTTPStatus = create_discovery_url_from_endpoint(GGDisccoveryURL , endpoint);
    }

	pxNetworkContext = mbedtls_transport_allocate();

	if( pxNetworkContext == NULL )
	{
		LogError( "Failed to allocate mbedtls transport context." );
		xHTTPStatus = HTTPInsufficientMemory;
	}

	if( xHTTPStatus == HTTPSuccess )
	{
		  xTlsStatus = mbedtls_transport_configure( pxNetworkContext,
												  pcAlpnProtocols,
												  &xPrivateKey,
												  &xClientCertificate,
												  pxRootCaChain,
												  1 );

		  if( xTlsStatus != TLS_TRANSPORT_SUCCESS )
		{
			LogError( "Failed to configure mbedtls transport." );
			xHTTPStatus = HTTPInvalidParameter;
		}
	}

	xTlsStatus = TLS_TRANSPORT_UNKNOWN_ERROR;

	if( xHTTPStatus == HTTPSuccess )
	{

		/* Block until the network interface is connected */
		( void ) xEventGroupWaitBits( xSystemEvents,
									  EVT_MASK_NET_CONNECTED,
									  0x00,
									  pdTRUE,
									  portMAX_DELAY );

		LogInfo( "Connecting to Greengrass discovery server %s:%d.",
                                                GGDisccoveryURL, discovery_port );

		xTlsStatus = mbedtls_transport_connect( pxNetworkContext,
                                                GGDisccoveryURL,
												discovery_port,
												0, 0 );
		if( xTlsStatus == TLS_TRANSPORT_SUCCESS ){
			LogInfo( "Connected to HTTP server." );

			HTTPRequestInfo_t requestInfo;
			HTTPRequestHeaders_t requestHeaders;
			HTTPResponse_t response;
            uint8_t *responseBuffer = NULL;
            TransportInterface_t transportInterface;
            const char *thing_name;

           ( void ) memset( &response, 0, sizeof( response ) );

			/* Allocate memory for the header buffer on the heap. */
            uint8_t *headerBuffer = ( uint8_t * ) pvPortMalloc( HEADER_BUFFER_LENGTH );
            if ( headerBuffer == NULL )
            {
                LogError( "Failed to allocate memory for header buffer." );
                xHTTPStatus = HTTPInsufficientMemory;
            }

            /* Initialize the HTTP request information. */
            if ( xHTTPStatus == HTTPSuccess )
            {
                /* Get thing name from KVStore*/
                thing_name = KVStore_getStringHeap( CS_CORE_THING_NAME, &uxTempSize );
                if( thing_name == NULL || uxTempSize == 0 )
                {
                    LogError( "Invalid client identifier read from KVStore." );
                    xHTTPStatus = HTTPInvalidParameter;
                }
            } 
            
            /* Initialize the HTTP request headers. */
            if ( xHTTPStatus == HTTPSuccess )
            {
                char pathBuffer[PATH_BUFFER_LENGHT]; 
                snprintf(pathBuffer, sizeof(pathBuffer), "/greengrass/discover/thing/%s", thing_name);

                requestInfo.pHost = GGDisccoveryURL;
                requestInfo.hostLen = strlen( GGDisccoveryURL );
                requestInfo.pMethod = HTTP_METHOD_GET;
                requestInfo.methodLen = strlen( HTTP_METHOD_GET );
                requestInfo.pPath = pathBuffer;
                requestInfo.pathLen = strlen(pathBuffer);
            
                requestHeaders.pBuffer = headerBuffer;
                requestHeaders.bufferLen = HEADER_BUFFER_LENGTH;
            
                xHTTPStatus = HTTPClient_InitializeRequestHeaders( &requestHeaders, &requestInfo );
                if ( xHTTPStatus != HTTPSuccess )
                {
                    LogError( "Failed to initialize HTTP request headers." );
                }
            }

            /* Allocate memory for the response buffer on the heap. */
            if ( xHTTPStatus == HTTPSuccess )
            {
                responseBuffer = ( uint8_t * ) pvPortMalloc( RESPONSE_BUFFER_LENGTH );
                if ( responseBuffer == NULL )
                {
                    LogError( "Failed to allocate memory for response buffer." );
                    xHTTPStatus = HTTPInsufficientMemory;
                }
            }

            /* Send the HTTP GET request. */
            if ( xHTTPStatus == HTTPSuccess )
            {
                /* Initialize the transport interface. */
                transportInterface.pNetworkContext = pxNetworkContext;
                transportInterface.send = transportSend;
                transportInterface.recv = transportRecv;
        
                /* Initialize the HTTP response. */
                response.pBuffer = responseBuffer;
                response.bufferLen = RESPONSE_BUFFER_LENGTH;
        
                xHTTPStatus = HTTPClient_Send( &transportInterface, &requestHeaders,
                                            NULL, 0, &response, 0 );
                if ( xHTTPStatus != HTTPSuccess )
                {
                    LogError( ( "Failed to send HTTP request: Error=%s.",
                                HTTPClient_strerror( xHTTPStatus ) ) );
                }
            }

            /* Parse and write JSON reposne into the NVM */
            if ( xHTTPStatus == HTTPSuccess )
            {
                /* Print the HTTP response. */
                LogInfo( "HTTP Response: %.*s\n", ( int ) response.bodyLen, response.pBody );

                /* Parse the JSON response */
                char *p_parsedEndPoint = NULL;
                char *p_parsedCertificate = NULL;
                size_t parsedEndPointLen;
                size_t parsedCertificateLen;
                uint16_t mqttPort = 0;
                BaseType_t xStatus = pdTRUE;

                JSONStatus_t parseResult = parseGGDiscoveryResponse((const char *)response.pBody,
                                                                response.bodyLen,
                                                                &p_parsedEndPoint,
                                                                &parsedEndPointLen,
                                                                &p_parsedCertificate,
                                                                &parsedCertificateLen,
                                                                &mqttPort);

                if (parseResult == JSONSuccess)
                {
                    LogDebug("Extracted mqtt endpoint: %.*s", parsedEndPointLen, p_parsedEndPoint);
                    LogDebug("Extracted mqtt broker port: %d", mqttPort);
                    LogDebug("Extracted certificate: %.*s", parsedCertificateLen, p_parsedCertificate);

                    LogInfo("Successfully Parsed Json response");

                    // Allocate memory and copy the parsed values
                    char *endPointCopy = (char *)pvPortMalloc(parsedEndPointLen);
                    char *certificateCopy = NULL;

                    if (endPointCopy == NULL )
                    {
                        LogError("Memory allocation failed for endpoint.");
                        xStatus = pdFALSE;
                    }
                    
                    if( xStatus == pdTRUE ){

                        /* Copy the parsed values to ensure the original JSON document remains unaltered if needed later */
                        strncpy(endPointCopy, p_parsedEndPoint, parsedEndPointLen);

                        /* Replace \\n newlines with actual \n newlines in certificate */
                        certificateCopy = copyAndFormatCertificateForNVM(p_parsedCertificate);

                        /* Write received GreenGrass config params into NVM */
                        xStatus = Write_GG_config_into_NVM(certificateCopy,
                                                            endPointCopy,
                                                            mqttPort);
                    }

                    if(xStatus == pdTRUE)
                    {
                        LogInfo("Successfully stored GG config into NVM");
                        pGGContext->ggDiscoverySuccess = true;
                    }
                    else{
                        LogError("Failed to store GG config into NVM");
                        pGGContext->ggDiscoverySuccess = false;
                    }
                
                    vPortFree(endPointCopy);

                    if(certificateCopy != NULL){
                        vPortFree(certificateCopy);
                    }
                }
                else
                {
                    LogError("Failed to parse JSON response with error code: %d", parseResult);
                }
            }

            /* Free the allocated memory for the header buffer. */
            if (headerBuffer != NULL) {
                vPortFree(headerBuffer);
            }
            /* Free the allocated memory for the response buffer. */
            if (responseBuffer != NULL)
            {
                vPortFree( responseBuffer );
            }
        }
		else
		{
			LogError( "Failed to connect to http server." );
		}
	}
    if( pxNetworkContext != NULL )
    {
        
        mbedtls_transport_disconnect( pxNetworkContext );
        mbedtls_transport_free( pxNetworkContext );
        pxNetworkContext = NULL;
        ( void ) xEventGroupClearBits( xSystemEvents, EVT_MASK_MQTT_AGENT_FINISHED );
    }
    if(GGDisccoveryURL != NULL){
        vPortFree (GGDisccoveryURL);
    }
    LogInfo("Greengrass Discovery done");

    xEventGroupSetBits(xSystemEvents, EVT_MASK_GG_DISCOVERY_PERFORMED);

    LogInfo("Terminating GGDiscovery Task.");

	vTaskDelete( NULL );
}
