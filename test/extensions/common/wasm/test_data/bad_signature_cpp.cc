// NOLINT(namespace-envoy)
#include <string>

#define EMSCRIPTEN_KEEPALIVE __attribute__((used)) __attribute__((visibility("default")))

extern "C" uint32_t proxy_log(uint32_t level, const char* logMessage, size_t messageSize);

extern "C" EMSCRIPTEN_KEEPALIVE uint32_t proxy_on_configure(uint32_t, int bad, char* configuration,
                                                            int size) {
  std::string message = "bad signature";
  proxy_log(4 /* error */, message.c_str(), message.size());
  return 1;
}
