#pragma once

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
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
} httpRequest;

typedef struct { // NOLINT(modernize-use-using)
  uint64_t plugin_name_ptr;
  uint64_t plugin_name_len;
  uint64_t config_ptr;
  uint64_t config_len;
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
  CAPINotInGo = -3,
  CAPIInvalidPhase = -4,
} CAPIStatus;

// tcp upstream
CAPIStatus envoyGoHttpTcpBridgeCopyHeaders(void* s, void* strs, void* buf);
CAPIStatus envoyGoHttpTcpBridgeSetRespHeader(void* s, void* key_data, int key_len, void* value_data,
                                             int value_len, headerAction action);
CAPIStatus envoyGoHttpTcpBridgeRemoveRespHeader(void* s, void* key_data, int key_len);
CAPIStatus envoyGoHttpTcpBridgeGetBuffer(void* s, uint64_t buffer, void* value);
CAPIStatus envoyGoHttpTcpBridgeDrainBuffer(void* s, uint64_t buffer, uint64_t length);
CAPIStatus envoyGoHttpTcpBridgeSetBufferHelper(void* s, uint64_t buffer, void* data, int length,
                                               bufferAction action);
CAPIStatus envoyGoHttpTcpBridgeGetStringValue(void* r, int id, uint64_t* value_data,
                                              int* value_len);
CAPIStatus envoyGoHttpTcpBridgeSetSelfHalfCloseForUpstreamConn(void* r, int enabled);

#ifdef __cplusplus
} // extern "C"
#endif
