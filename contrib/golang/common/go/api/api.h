#pragma once

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h> // NOLINT(modernize-deprecated-headers)

typedef struct { // NOLINT(modernize-use-using)
  const char* data;
  unsigned long long int len;
} Cstring;

typedef struct { // NOLINT(modernize-use-using)
  Cstring plugin_name;
  unsigned long long int configId;
  int phase;
} httpRequest;

typedef struct { // NOLINT(modernize-use-using)
  unsigned long long int plugin_name_ptr;
  unsigned long long int plugin_name_len;
  unsigned long long int config_ptr;
  unsigned long long int config_len;
  int is_route_config;
} httpConfig;

typedef enum { // NOLINT(modernize-use-using)
  Set,
  Append,
  Prepend,
} bufferAction;

typedef enum { // NOLINT(modernize-use-using)
  HeaderSet,
  HeaderAdd,
} headerAction;

// The return value of C Api that invoking from Go.
typedef enum { // NOLINT(modernize-use-using)
  CAPIOK = 0,
  CAPIFilterIsGone = -1,
  CAPIFilterIsDestroy = -2,
  CAPINotInGo = -3,
  CAPIInvalidPhase = -4,
  CAPIValueNotFound = -5,
  CAPIYield = -6,
  CAPIInternalFailure = -7,
  CAPISerializationFailure = -8,
} CAPIStatus;

CAPIStatus envoyGoFilterHttpContinue(void* r, int status);
CAPIStatus envoyGoFilterHttpSendLocalReply(void* r, int response_code, void* body_text,
                                           void* headers, long long int grpc_status, void* details);
CAPIStatus envoyGoFilterHttpSendPanicReply(void* r, void* details);

CAPIStatus envoyGoFilterHttpGetHeader(void* r, void* key, void* value);
CAPIStatus envoyGoFilterHttpCopyHeaders(void* r, void* strs, void* buf);
CAPIStatus envoyGoFilterHttpSetHeaderHelper(void* r, void* key, void* value, headerAction action);
CAPIStatus envoyGoFilterHttpRemoveHeader(void* r, void* key);

CAPIStatus envoyGoFilterHttpGetBuffer(void* r, unsigned long long int buffer, void* value);
CAPIStatus envoyGoFilterHttpDrainBuffer(void* r, unsigned long long int buffer, uint64_t length);
CAPIStatus envoyGoFilterHttpSetBufferHelper(void* r, unsigned long long int buffer, void* data,
                                            int length, bufferAction action);

CAPIStatus envoyGoFilterHttpCopyTrailers(void* r, void* strs, void* buf);
CAPIStatus envoyGoFilterHttpSetTrailer(void* r, void* key, void* value, headerAction action);
CAPIStatus envoyGoFilterHttpRemoveTrailer(void* r, void* key);

CAPIStatus envoyGoFilterHttpGetStringValue(void* r, int id, void* value);
CAPIStatus envoyGoFilterHttpGetIntegerValue(void* r, int id, void* value);

CAPIStatus envoyGoFilterHttpGetDynamicMetadata(void* r, void* name, void* hand);
CAPIStatus envoyGoFilterHttpSetDynamicMetadata(void* r, void* name, void* key, void* buf);

void envoyGoFilterLog(uint32_t level, void* message);
uint32_t envoyGoFilterLogLevel();

void envoyGoFilterHttpFinalize(void* r, int reason);
void envoyGoConfigHttpFinalize(void* c);

CAPIStatus envoyGoFilterHttpSetStringFilterState(void* r, void* key, void* value, int state_type,
                                                 int life_span, int stream_sharing);
CAPIStatus envoyGoFilterHttpGetStringFilterState(void* r, void* key, void* value);
CAPIStatus envoyGoFilterHttpGetStringProperty(void* r, void* key, void* value, int* rc);

CAPIStatus envoyGoFilterHttpDefineMetric(void* c, uint32_t metric_type, void* name,
                                         void* metric_id);
CAPIStatus envoyGoFilterHttpIncrementMetric(void* c, uint32_t metric_id, int64_t offset);
CAPIStatus envoyGoFilterHttpGetMetric(void* c, uint32_t metric_id, void* value);
CAPIStatus envoyGoFilterHttpRecordMetric(void* c, uint32_t metric_id, uint64_t value);

// downstream
CAPIStatus envoyGoFilterDownstreamClose(void* wrapper,
                                        int closeType); // NOLINT(readability-identifier-naming)
CAPIStatus envoyGoFilterDownstreamWrite(void* wrapper, void* buffers,
                                        int buffersNum, // NOLINT(readability-identifier-naming)
                                        int endStream); // NOLINT(readability-identifier-naming)
void envoyGoFilterDownstreamFinalize(void* wrapper, int reason);
CAPIStatus envoyGoFilterDownstreamInfo(void* wrapper, int t, void* ret);

// upstream
void* envoyGoFilterUpstreamConnect(
    void* libraryID, void* addr,    // NOLINT(readability-identifier-naming)
    unsigned long long int connID); // NOLINT(readability-identifier-naming)
CAPIStatus envoyGoFilterUpstreamWrite(void* wrapper, void* buffers,
                                      int buffersNum, // NOLINT(readability-identifier-naming)
                                      int endStream); // NOLINT(readability-identifier-naming)
CAPIStatus envoyGoFilterUpstreamClose(void* wrapper,
                                      int closeType); // NOLINT(readability-identifier-naming)
void envoyGoFilterUpstreamFinalize(void* wrapper, int reason);
CAPIStatus envoyGoFilterUpstreamInfo(void* wrapper, int t, void* ret);

// filter state
CAPIStatus envoyGoFilterSetFilterState(void* wrapper, void* key, void* value, int state_type,
                                       int life_span, int stream_sharing);
CAPIStatus envoyGoFilterGetFilterState(void* wrapper, void* key, void* value);

#ifdef __cplusplus
} // extern "C"
#endif
