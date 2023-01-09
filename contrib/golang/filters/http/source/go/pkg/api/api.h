#pragma once

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
#endif

// This describes the return value of a C Api from Go.
#define CAPIOK 0
#define CAPIFilterIsGone -1
#define CAPIFilterIsDestroy -2
#define CAPINotInGo -3
#define CAPIInvalidPhase -4

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

int envoyGoFilterHttpContinue(void* r, int status);
int envoyGoFilterHttpSendLocalReply(void* r, int response_code, void* body_text, void* headers,
                                    long long int grpc_status, void* details);

int envoyGoFilterHttpGetHeader(void* r, void* key, void* value);
int envoyGoFilterHttpCopyHeaders(void* r, void* strs, void* buf);
int envoyGoFilterHttpSetHeader(void* r, void* key, void* value);
int envoyGoFilterHttpRemoveHeader(void* r, void* key);

int envoyGoFilterHttpGetBuffer(void* r, unsigned long long int buffer, void* value);
int envoyGoFilterHttpSetBufferHelper(void* r, unsigned long long int buffer, void* data, int length,
                                     bufferAction action);

int envoyGoFilterHttpCopyTrailers(void* r, void* strs, void* buf);
int envoyGoFilterHttpSetTrailer(void* r, void* key, void* value);

int envoyGoFilterHttpGetStringValue(void* r, int id, void* value);

void envoyGoFilterHttpFinalize(void* r, int reason);

#ifdef __cplusplus
} // extern "C"
#endif
