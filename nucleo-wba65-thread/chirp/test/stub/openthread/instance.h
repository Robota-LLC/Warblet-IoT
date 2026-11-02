/* Host stand-ins for the OpenThread types and calls chirp_prov.c and
 * chirp_report.c use (test only): the names and shapes of OpenThread's own,
 * cut down to what those two files touch. chirp/test/boottest.c fakes the
 * calls. The other openthread/ headers here include this one. */
#pragma once
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct otInstance otInstance;

typedef int otError;
#define OT_ERROR_NONE          0
#define OT_ERROR_PARSE         6
#define OT_ERROR_INVALID_STATE 13

typedef enum {
  OT_DEVICE_ROLE_DISABLED = 0,
  OT_DEVICE_ROLE_DETACHED = 1,
  OT_DEVICE_ROLE_CHILD    = 2,
  OT_DEVICE_ROLE_ROUTER   = 3,
  OT_DEVICE_ROLE_LEADER   = 4
} otDeviceRole;

#define OT_CHANGED_THREAD_ROLE            (1u << 2)
#define OT_OPERATIONAL_DATASET_MAX_LENGTH 254

typedef uint16_t otPanId;
typedef struct { uint8_t mTlvs[OT_OPERATIONAL_DATASET_MAX_LENGTH]; uint8_t mLength; } otOperationalDatasetTlvs;
typedef struct { uint64_t mSeconds; } otTimestamp;
typedef struct { uint8_t m8[8]; } otExtendedPanId;
typedef struct { uint8_t m8[16]; } otNetworkKey;
typedef struct { char m8[17]; } otNetworkName;
typedef struct {
  bool mIsActiveTimestampPresent, mIsChannelPresent, mIsPanIdPresent,
       mIsExtendedPanIdPresent, mIsNetworkKeyPresent, mIsNetworkNamePresent;
} otOperationalDatasetComponents;
typedef struct {
  otTimestamp                    mActiveTimestamp;
  otNetworkKey                   mNetworkKey;
  otNetworkName                  mNetworkName;
  otExtendedPanId                mExtendedPanId;
  uint16_t                       mChannel;
  otPanId                        mPanId;
  otOperationalDatasetComponents mComponents;
} otOperationalDataset;

typedef struct { uint8_t m8[16]; } otIp6Address;
typedef struct otMessage otMessage;
typedef struct otMessageSettings otMessageSettings;
typedef struct { otIp6Address mSockAddr, mPeerAddr; uint16_t mSockPort, mPeerPort; } otMessageInfo;
typedef struct { int mOpen; } otUdpSocket;
typedef struct otDnsAddressResponse otDnsAddressResponse;
typedef struct otDnsQueryConfig otDnsQueryConfig;

typedef int  (*otCliOutputCallback)(void *aContext, const char *aFormat, va_list aArguments);
typedef void (*otStateChangedCallback)(uint32_t aFlags, void *aContext);
typedef void (*otUdpReceive)(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo);
typedef void (*otDnsAddressCallback)(otError aError, const otDnsAddressResponse *aResponse, void *aContext);

otDeviceRole otThreadGetDeviceRole(otInstance *aInstance);
const char  *otThreadDeviceRoleToString(otDeviceRole aRole);
const char  *otThreadErrorToString(otError aError);
otError      otThreadSetEnabled(otInstance *aInstance, bool aEnabled);
bool         otIp6IsEnabled(otInstance *aInstance);
otError      otIp6SetEnabled(otInstance *aInstance, bool aEnabled);
otError      otSetStateChangedCallback(otInstance *aInstance, otStateChangedCallback aCallback, void *aContext);

void    otDatasetConvertToTlvs(const otOperationalDataset *aDataset, otOperationalDatasetTlvs *aTlvs);
otError otDatasetGetActiveTlvs(otInstance *aInstance, otOperationalDatasetTlvs *aTlvs);
otError otDatasetSetActiveTlvs(otInstance *aInstance, const otOperationalDatasetTlvs *aTlvs);
otError otDatasetSetActive(otInstance *aInstance, const otOperationalDataset *aDataset);

otError    otDnsClientResolveAddress(otInstance *aInstance, const char *aHostName, otDnsAddressCallback aCallback,
                                     void *aContext, const otDnsQueryConfig *aConfig);
otError    otDnsAddressResponseGetAddress(const otDnsAddressResponse *aResponse, uint16_t aIndex,
                                          otIp6Address *aAddress, uint32_t *aTtl);
otError    otUdpOpen(otInstance *aInstance, otUdpSocket *aSocket, otUdpReceive aCallback, void *aContext);
otMessage *otUdpNewMessage(otInstance *aInstance, const otMessageSettings *aSettings);
otError    otMessageAppend(otMessage *aMessage, const void *aBuf, uint16_t aLength);
otError    otUdpSend(otInstance *aInstance, otUdpSocket *aSocket, otMessage *aMessage, const otMessageInfo *aMessageInfo);
void       otMessageFree(otMessage *aMessage);
