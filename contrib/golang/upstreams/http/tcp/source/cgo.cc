// #include "contrib/golang/filters/network/source/golang.h"
#include "contrib/golang/upstreams/http/tcp/source/upstream_request.h"

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

CAPIStatus envoyGoTcpUpstreamHandlerWrapper(void* r,
                                       std::function<CAPIStatus(TcpUpstream&)> f) {
  auto req = static_cast<TcpUpstream*>(reinterpret_cast<httpRequest*>(r));
  return f(*req);
}

CAPIStatus envoyGoTcpUpstreamCopyHeaders(void* r, void* strs, void* buf) {
  return envoyGoTcpUpstreamHandlerWrapper(
      r, [strs, buf](TcpUpstream& t) -> CAPIStatus {
        auto go_strs = reinterpret_cast<GoString*>(strs);
        auto go_buf = reinterpret_cast<char*>(buf);
        return t.copyHeaders(go_strs, go_buf);
      });
}

CAPIStatus envoyGoTcpUpstreamSetRespHeader(void* r, void* key_data, int key_len, void* value_data,
                                            int value_len, headerAction act) {
  return envoyGoTcpUpstreamHandlerWrapper(
      r,
      [key_data, key_len, value_data, value_len, act](TcpUpstream& t) -> CAPIStatus {
        auto key_str = stringViewFromGoPointer(key_data, key_len);
        auto value_str = stringViewFromGoPointer(value_data, value_len);
        return t.setRespHeader(key_str, value_str, act);
      });
}

CAPIStatus envoyGoTcpUpstreamGetBuffer(void* r, uint64_t buffer_ptr, void* data) {
  return envoyGoTcpUpstreamHandlerWrapper(
      r, [buffer_ptr, data](TcpUpstream& t) -> CAPIStatus {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        return t.copyBuffer(buffer, reinterpret_cast<char*>(data));
      });
}

CAPIStatus envoyGoTcpUpstreamDrainBuffer(void* r, uint64_t buffer_ptr, uint64_t length) {
  return envoyGoTcpUpstreamHandlerWrapper(
      r,
      [buffer_ptr, length](TcpUpstream& t) -> CAPIStatus {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        return t.drainBuffer(buffer, length);
      });
}

CAPIStatus envoyGoTcpUpstreamSetBufferHelper(void* s, uint64_t buffer_ptr, void* data, int length,
                                            bufferAction action) {
  return envoyGoTcpUpstreamHandlerWrapper(
      s,
      [buffer_ptr, data, length, action](TcpUpstream& t) -> CAPIStatus {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        auto value = stringViewFromGoPointer(data, length);
        return t.setBufferHelper(buffer, value, action);
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
