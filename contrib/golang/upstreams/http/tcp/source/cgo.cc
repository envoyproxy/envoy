// #include "contrib/golang/filters/network/source/golang.h"
#include "contrib/golang/upstreams/http/tcp/source/upstream_request.h"
#include "processor_state.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {
namespace Golang {

// The returned absl::string_view only refer to Go memory,
// should not use it after the current cgo call returns.
absl::string_view stringViewFromGoPointer(void* p, int len) {
  return {static_cast<const char*>(p), static_cast<size_t>(len)};
}

extern "C" {

CAPIStatus envoyGoHttpTcpBridgeProcessStateHandlerWrapper(
    void* s, std::function<CAPIStatus(HttpTcpBridge&, ProcessorState&)> f) {
  auto state = static_cast<ProcessorState*>(reinterpret_cast<processState*>(s));
  auto req = static_cast<HttpTcpBridge*>(state->req);
  return f(*req, *state);
}

CAPIStatus envoyGoHttpTcpBridgeHandlerWrapper(void* r,
                                              std::function<CAPIStatus(HttpTcpBridge&)> f) {
  auto req = static_cast<HttpTcpBridge*>(reinterpret_cast<httpRequest*>(r));
  return f(*req);
}

CAPIStatus envoyGoHttpTcpBridgeCopyHeaders(void* s, void* strs, void* buf) {
  return envoyGoHttpTcpBridgeProcessStateHandlerWrapper(
      s, [strs, buf](HttpTcpBridge& t, ProcessorState& state) -> CAPIStatus {
        auto go_strs = reinterpret_cast<GoString*>(strs);
        auto go_buf = reinterpret_cast<char*>(buf);
        return t.copyHeaders(state, go_strs, go_buf);
      });
}

CAPIStatus envoyGoHttpTcpBridgeSetRespHeader(void* s, void* key_data, int key_len, void* value_data,
                                             int value_len, headerAction act) {
  return envoyGoHttpTcpBridgeProcessStateHandlerWrapper(
      s,
      [key_data, key_len, value_data, value_len, act](HttpTcpBridge& t,
                                                      ProcessorState& state) -> CAPIStatus {
        auto key_str = stringViewFromGoPointer(key_data, key_len);
        auto value_str = stringViewFromGoPointer(value_data, value_len);
        return t.setRespHeader(state, key_str, value_str, act);
      });
}

CAPIStatus envoyGoHttpTcpBridgeRemoveRespHeader(void* s, void* key_data, int key_len) {
  return envoyGoHttpTcpBridgeProcessStateHandlerWrapper(
      s, [key_data, key_len](HttpTcpBridge& t, ProcessorState& state) -> CAPIStatus {
        auto key_str = stringViewFromGoPointer(key_data, key_len);
        return t.removeRespHeader(state, key_str);
      });
}

CAPIStatus envoyGoHttpTcpBridgeGetBuffer(void* s, uint64_t buffer_ptr, void* data) {
  return envoyGoHttpTcpBridgeProcessStateHandlerWrapper(
      s, [buffer_ptr, data](HttpTcpBridge& t, ProcessorState& state) -> CAPIStatus {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        return t.copyBuffer(state, buffer, reinterpret_cast<char*>(data));
      });
}

CAPIStatus envoyGoHttpTcpBridgeDrainBuffer(void* s, uint64_t buffer_ptr, uint64_t length) {
  return envoyGoHttpTcpBridgeProcessStateHandlerWrapper(
      s, [buffer_ptr, length](HttpTcpBridge& t, ProcessorState& state) -> CAPIStatus {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        return t.drainBuffer(state, buffer, length);
      });
}

CAPIStatus envoyGoHttpTcpBridgeSetBufferHelper(void* s, uint64_t buffer_ptr, void* data, int length,
                                               bufferAction action) {
  return envoyGoHttpTcpBridgeProcessStateHandlerWrapper(
      s, [buffer_ptr, data, length, action](HttpTcpBridge& t, ProcessorState& state) -> CAPIStatus {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        auto value = stringViewFromGoPointer(data, length);
        return t.setBufferHelper(state, buffer, value, action);
      });
}

CAPIStatus envoyGoHttpTcpBridgeGetStringValue(void* r, int id, uint64_t* value_data,
                                              int* value_len) {
  return envoyGoHttpTcpBridgeHandlerWrapper(
      r, [id, value_data, value_len](HttpTcpBridge& t) -> CAPIStatus {
        return t.getStringValue(id, value_data, value_len);
      });
}

CAPIStatus envoyGoHttpTcpBridgeSetSelfHalfCloseForUpstreamConn(void* r, int enabled) {
  return envoyGoHttpTcpBridgeHandlerWrapper(r, [enabled](HttpTcpBridge& t) -> CAPIStatus {
    return t.setSelfHalfCloseForUpstreamConn(enabled);
  });
}

} // extern "C"
} // namespace Golang
} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
