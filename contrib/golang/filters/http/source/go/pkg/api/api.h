#pragma once

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" {
#endif

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

void envoyGoFilterHttpContinue(void* r, int status);
void envoyGoFilterHttpSendLocalReply(void* r, int response_code, void* body_text, void* headers,
                                     long long int grpc_status, void* details);

void envoyGoFilterHttpGetHeader(void* r, void* key, void* value);
void envoyGoFilterHttpCopyHeaders(void* r, void* strs, void* buf);
void envoyGoFilterHttpSetHeader(void* r, void* key, void* value);
void envoyGoFilterHttpRemoveHeader(void* r, void* key);

void envoyGoFilterHttpGetBuffer(void* r, unsigned long long int buffer, void* value);
void envoyGoFilterHttpSetBufferHelper(void* r, unsigned long long int buffer, void* data,
                                      int length, bufferAction action);

void envoyGoFilterHttpCopyTrailers(void* r, void* strs, void* buf);
void envoyGoFilterHttpSetTrailer(void* r, void* key, void* value);

void envoyGoFilterHttpGetStringValue(void* r, int id, void* value);

void envoyGoFilterHttpFinalize(void* r, int reason);

#ifdef __cplusplus
} // extern "C"
#endif
