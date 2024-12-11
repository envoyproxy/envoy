// #include "contrib/golang/filters/network/source/golang.h"
#include "contrib/golang/upstreams/http/tcp/source/upstream_request.h"
#include "processor_state.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {
namespace Golang {

//
// These functions may be invoked in another go thread,
// which means may introduce race between go thread and envoy thread.
// So we use the envoy's dispatcher in the filter to post it, and make it only executes in the envoy
// thread.
//

// The returned absl::string_view only refer to Go memory,
// should not use it after the current cgo call returns.
absl::string_view stringViewFromGoPointer(void* p, int len) {
  return {static_cast<const char*>(p), static_cast<size_t>(len)};
}

extern "C" {

CAPIStatus envoyGoTcpUpstreamProcessStateHandlerWrapper(
    void* s, std::function<CAPIStatus(TcpUpstream&, ProcessorState&)> f) {
  auto state = static_cast<ProcessorState*>(reinterpret_cast<processState*>(s));
  auto req = static_cast<TcpUpstream*>(state->req);
  return f(*req, *state);
}

CAPIStatus envoyGoTcpUpstreamHandlerWrapper(void* r,
                                       std::function<CAPIStatus(TcpUpstream&)> f) {
  auto req = static_cast<TcpUpstream*>(reinterpret_cast<httpRequest*>(r));
  return f(*req);
}

CAPIStatus envoyGoTcpUpstreamCopyHeaders(void* s, void* strs, void* buf) {
  return envoyGoTcpUpstreamProcessStateHandlerWrapper(
      s, [strs, buf](TcpUpstream& t, ProcessorState& state) -> CAPIStatus {
        auto go_strs = reinterpret_cast<GoString*>(strs);
        auto go_buf = reinterpret_cast<char*>(buf);
        return t.copyHeaders(state, go_strs, go_buf);
      });
}

CAPIStatus envoyGoTcpUpstreamSetRespHeader(void* s, void* key_data, int key_len, void* value_data,
                                            int value_len, headerAction act) {
  return envoyGoTcpUpstreamProcessStateHandlerWrapper(
      s,
      [key_data, key_len, value_data, value_len, act](TcpUpstream& t,
                                                      ProcessorState& state) -> CAPIStatus {
        auto key_str = stringViewFromGoPointer(key_data, key_len);
        auto value_str = stringViewFromGoPointer(value_data, value_len);
        return t.setRespHeader(state, key_str, value_str, act);
      });
}

CAPIStatus envoyGoTcpUpstreamGetBuffer(void* s, uint64_t buffer_ptr, void* data) {
  return envoyGoTcpUpstreamProcessStateHandlerWrapper(
      s, [buffer_ptr, data](TcpUpstream& t, ProcessorState& state) -> CAPIStatus {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        return t.copyBuffer(state, buffer, reinterpret_cast<char*>(data));
      });
}

CAPIStatus envoyGoTcpUpstreamDrainBuffer(void* s, uint64_t buffer_ptr, uint64_t length) {
  return envoyGoTcpUpstreamProcessStateHandlerWrapper(
      s,
      [buffer_ptr, length](TcpUpstream& t, ProcessorState& state) -> CAPIStatus {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        return t.drainBuffer(state, buffer, length);
      });
}

CAPIStatus envoyGoTcpUpstreamSetBufferHelper(void* s, uint64_t buffer_ptr, void* data, int length,
                                            bufferAction action) {
  return envoyGoTcpUpstreamProcessStateHandlerWrapper(
      s,
      [buffer_ptr, data, length, action](TcpUpstream& t,
                                         ProcessorState& state) -> CAPIStatus {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        auto value = stringViewFromGoPointer(data, length);
        return t.setBufferHelper(state, buffer, value, action);
      });
}

CAPIStatus envoyGoTcpUpstreamGetStringValue(void* r, int id, uint64_t* value_data, int* value_len) {
  return envoyGoTcpUpstreamHandlerWrapper(
      r, [id, value_data, value_len](TcpUpstream& t) -> CAPIStatus {
        return t.getStringValue(id, value_data, value_len);
      });
}

CAPIStatus envoyGoTcpUpstreamSetSelfHalfCloseForUpstreamConn(void* r, int enabled) {
  return envoyGoTcpUpstreamHandlerWrapper(
      r, [enabled](TcpUpstream& t) {
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
