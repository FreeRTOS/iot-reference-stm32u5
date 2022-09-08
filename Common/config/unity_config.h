#ifndef _UNITY_CONFIG_H
#define _UNITY_CONFIG_H

#define UNITY_OUTPUT_CHAR( a )     TEST_CacheResult( a )
#define UNITY_OUTPUT_FLUSH()       TEST_SubmitResultBuffer()
#define UNITY_OUTPUT_START()       TEST_NotifyTestStart()
#define UNITY_OUTPUT_COMPLETE()    TEST_NotifyTestFinished()

#endif
