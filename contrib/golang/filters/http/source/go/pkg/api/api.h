#pragma once

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct { // NOLINT(modernize-use-using)
  const char* data;
  unsigned long long int len;
} Cstring;

typedef struct { // NOLINT(modernize-use-using)
  Cstring plugin_name;
  unsigned long long int configId;
  int phase;
} httpRequest;

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
CAPIStatus envoyGoFilterHttpSetBufferHelper(void* r, unsigned long long int buffer, void* data,
                                            int length, bufferAction action);

CAPIStatus envoyGoFilterHttpCopyTrailers(void* r, void* strs, void* buf);
CAPIStatus envoyGoFilterHttpSetTrailer(void* r, void* key, void* value);

CAPIStatus envoyGoFilterHttpGetStringValue(void* r, int id, void* value);
CAPIStatus envoyGoFilterHttpGetIntegerValue(void* r, int id, void* value);

// TODO: implement get dynamic metadata
CAPIStatus envoyGoFilterHttpSetDynamicMetadata(void* r, void* name, void* key, void* buf);

void envoyGoFilterHttpLog(uint32_t level, void* message);

void envoyGoFilterHttpFinalize(void* r, int reason);

#ifdef __cplusplus
} // extern "C"
#endif
