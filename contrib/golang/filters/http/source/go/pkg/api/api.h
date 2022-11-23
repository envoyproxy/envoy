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

void moeHttpContinue(void* r, int status);
void moeHttpSendLocalReply(void* r, int response_code, void* body_text, void* headers,
                           long long int grpc_status, void* details);

void moeHttpGetHeader(void* r, void* key, void* value);
void moeHttpCopyHeaders(void* r, void* strs, void* buf);
void moeHttpSetHeader(void* r, void* key, void* value);
void moeHttpRemoveHeader(void* r, void* key);

void moeHttpGetBuffer(void* r, unsigned long long int buffer, void* value);
void moeHttpSetBufferHelper(void* r, unsigned long long int buffer, void* data, int length,
                            bufferAction action);

void moeHttpCopyTrailers(void* r, void* strs, void* buf);
void moeHttpSetTrailer(void* r, void* key, void* value);

void moeHttpGetStringValue(void* r, int id, void* value);

void moeHttpFinalize(void* r, int reason);

#ifdef __cplusplus
} // extern "C"
#endif
