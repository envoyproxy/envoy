#pragma once

// NOLINT(namespace-envoy)

#ifdef __cplusplus
#include <atomic>

#define _Atomic(X) std::atomic<X>

extern "C" {
#else
#include <stdatomic.h> // NOLINT(modernize-deprecated-headers)
#endif

#include <stdint.h> // NOLINT(modernize-deprecated-headers)

typedef struct { // NOLINT(modernize-use-using)
  const char* data;
  uint64_t len;
} Cstring;

struct httpRequest;

typedef struct { // NOLINT(modernize-use-using)
  struct httpRequest* req;
  int is_encoding;
  int state;
} processState;

typedef struct httpRequest { // NOLINT(modernize-use-using)
  Cstring plugin_name;
  uint64_t configId;
  // The ID of the worker that is processing this request, this enables the go filter to dedicate
  // memory to each worker and not require locks
  uint32_t worker_id;
  // This flag will be read & written by different threads, so it need to be atomic
  _Atomic(int) is_golang_processing_log;
} httpRequest;

typedef struct { // NOLINT(modernize-use-using)
  uint64_t plugin_name_ptr;
  uint64_t plugin_name_len;
  uint64_t config_ptr;
  uint64_t config_len;
  int is_route_config;
  uint32_t concurrency;
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

/* These APIs are related to the decode/encode phase, use the pointer of processState. */
CAPIStatus envoyGoFilterHttpContinue(void* s, int status);
CAPIStatus envoyGoFilterHttpSendLocalReply(void* s, int response_code, void* body_text_data,
                                           int body_text_len, void* headers, int headers_num,
                                           long long int grpc_status, void* details_data,
                                           int details_len);
CAPIStatus envoyGoFilterHttpSendPanicReply(void* s, void* details_data, int details_len);

CAPIStatus envoyGoFilterHttpGetHeader(void* s, void* key_data, int key_len, uint64_t* value_data,
                                      int* value_len);
CAPIStatus envoyGoFilterHttpCopyHeaders(void* s, void* strs, void* buf);
CAPIStatus envoyGoFilterHttpSetHeaderHelper(void* s, void* key_data, int key_len, void* value_data,
                                            int value_len, headerAction action);
CAPIStatus envoyGoFilterHttpRemoveHeader(void* s, void* key_data, int key_len);

CAPIStatus envoyGoFilterHttpGetBuffer(void* s, uint64_t buffer, void* value);
CAPIStatus envoyGoFilterHttpDrainBuffer(void* s, uint64_t buffer, uint64_t length);
CAPIStatus envoyGoFilterHttpSetBufferHelper(void* s, uint64_t buffer, void* data, int length,
                                            bufferAction action);

CAPIStatus envoyGoFilterHttpCopyTrailers(void* s, void* strs, void* buf);
CAPIStatus envoyGoFilterHttpSetTrailer(void* s, void* key_data, int key_len, void* value,
                                       int value_len, headerAction action);
CAPIStatus envoyGoFilterHttpRemoveTrailer(void* s, void* key_data, int key_len);

/* These APIs have nothing to do with the decode/encode phase, use the pointer of httpRequest. */
CAPIStatus envoyGoFilterHttpClearRouteCache(void* r);
CAPIStatus envoyGoFilterHttpGetStringValue(void* r, int id, uint64_t* value_data, int* value_len);
CAPIStatus envoyGoFilterHttpGetIntegerValue(void* r, int id, uint64_t* value);

CAPIStatus envoyGoFilterHttpGetDynamicMetadata(void* r, void* name_data, int name_len,
                                               uint64_t* value_data, int* value_len);
CAPIStatus envoyGoFilterHttpSetDynamicMetadata(void* r, void* name_data, int name_len,
                                               void* key_data, int key_len, void* buf_data,
                                               int buf_len);
void envoyGoFilterHttpFinalize(void* r, int reason);

CAPIStatus envoyGoFilterHttpSetStringFilterState(void* r, void* key_data, int key_len,
                                                 void* value_data, int value_len, int state_type,
                                                 int life_span, int stream_sharing);
CAPIStatus envoyGoFilterHttpGetStringFilterState(void* r, void* key_data, int key_len,
                                                 uint64_t* value_data, int* value_len);
CAPIStatus envoyGoFilterHttpGetStringProperty(void* r, void* key_data, int key_len,
                                              uint64_t* value_data, int* value_len, int* rc);

/* These APIs have nothing to do with request */
void envoyGoFilterLog(uint32_t level, void* message_data, int message_len);
uint32_t envoyGoFilterLogLevel();

/* These APIs are related to config, use the pointer of config. */
void envoyGoConfigHttpFinalize(void* c);
CAPIStatus envoyGoFilterHttpDefineMetric(void* c, uint32_t metric_type, void* name_data,
                                         int name_len, uint32_t* metric_id);
CAPIStatus envoyGoFilterHttpIncrementMetric(void* c, uint32_t metric_id, int64_t offset);
CAPIStatus envoyGoFilterHttpGetMetric(void* c, uint32_t metric_id, uint64_t* value);
CAPIStatus envoyGoFilterHttpRecordMetric(void* c, uint32_t metric_id, uint64_t value);

// downstream
CAPIStatus envoyGoFilterDownstreamClose(void* wrapper, int close_type);
CAPIStatus envoyGoFilterDownstreamWrite(void* f, void* buffer_ptr, int buffer_len, int end_stream);
void envoyGoFilterDownstreamFinalize(void* wrapper, int reason);
CAPIStatus envoyGoFilterDownstreamInfo(void* wrapper, int t, void* ret);

void* envoyGoFilterUpstreamConnect(void* library_id, void* addr, uint64_t conn_id);
CAPIStatus envoyGoFilterUpstreamConnEnableHalfClose(void* u, int enable_half_close);
CAPIStatus envoyGoFilterUpstreamWrite(void* u, void* buffer_ptr, int buffer_len, int end_stream);
CAPIStatus envoyGoFilterUpstreamClose(void* wrapper, int close_type);
void envoyGoFilterUpstreamFinalize(void* wrapper, int reason);
CAPIStatus envoyGoFilterUpstreamInfo(void* wrapper, int t, void* ret);

// filter state
CAPIStatus envoyGoFilterSetFilterState(void* wrapper, void* key, void* value, int state_type,
                                       int life_span, int stream_sharing);
CAPIStatus envoyGoFilterGetFilterState(void* wrapper, void* key, void* value);

#ifdef __cplusplus
} // extern "C"
#endif
