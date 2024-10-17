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

CAPIStatus envoyGoTcpUpstreamProcessStateHandlerWrapper(
    void* s, std::function<CAPIStatus(std::shared_ptr<TcpUpstream>&, ProcessorState&)> f) {
  auto state = static_cast<ProcessorState*>(reinterpret_cast<processState*>(s));
  if (!state->isProcessingInGo()) {
    return CAPIStatus::CAPINotInGo;
  }
  auto req = static_cast<RequestInternal*>(state->req);
  auto weak_filter = req->weakFilter();
  if (auto filter = weak_filter.lock()) {
    return f(filter, *state);
  }
  return CAPIStatus::CAPIFilterIsGone;
}

CAPIStatus envoyGoTcpUpstreamHandlerWrapper(void* r,
                                       std::function<CAPIStatus(std::shared_ptr<TcpUpstream>&)> f) {
  auto req = reinterpret_cast<RequestInternal*>(r);
  auto weak_filter = req->weakFilter();
  if (auto filter = weak_filter.lock()) {
    // Though it's memory safe without this limitation.
    // But it's not a good idea to run Go code after continue back to Envoy C++,
    // so, add this limitation.
    if (!filter->isProcessingInGo()) {
      return CAPIStatus::CAPINotInGo;
    }
    return f(filter);
  }
  return CAPIStatus::CAPIFilterIsGone;
}

void envoyGoTcpUpstreamFinalize(void* r, int reason) {
  UNREFERENCED_PARAMETER(reason);
  // req is used by go, so need to use raw memory and then it is safe to release at the gc finalize
  // phase of the go object.
  auto req = reinterpret_cast<RequestInternal*>(r);
  // TODO defer delete req
  delete req;
}

void envoyGoConfigTcpUpstreamFinalize(void* c) {
  // config is used by go, so need to use raw memory and then it is safe to release at the gc
  // finalize phase of the go object.
  auto config = reinterpret_cast<HttpConfigInternal*>(c);
  delete config;
}

CAPIStatus envoyGoTcpUpstreamGetBuffer(void* s, uint64_t buffer_ptr, void* data) {
  return envoyGoTcpUpstreamProcessStateHandlerWrapper(
      s, [buffer_ptr, data](std::shared_ptr<TcpUpstream>& filter, ProcessorState& state) -> CAPIStatus {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        return filter->copyBuffer(state, buffer, reinterpret_cast<char*>(data));
      });
}

CAPIStatus envoyGoTcpUpstreamDrainBuffer(void* s, uint64_t buffer_ptr, uint64_t length) {
  return envoyGoTcpUpstreamProcessStateHandlerWrapper(
      s,
      [buffer_ptr, length](std::shared_ptr<TcpUpstream>& filter, ProcessorState& state) -> CAPIStatus {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        return filter->drainBuffer(state, buffer, length);
      });
}

CAPIStatus envoyGoTcpUpstreamSetBufferHelper(void* s, uint64_t buffer_ptr, void* data, int length,
                                            bufferAction action) {
  return envoyGoTcpUpstreamProcessStateHandlerWrapper(
      s,
      [buffer_ptr, data, length, action](std::shared_ptr<TcpUpstream>& filter,
                                         ProcessorState& state) -> CAPIStatus {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        auto value = stringViewFromGoPointer(data, length);
        return filter->setBufferHelper(state, buffer, value, action);
      });
}

CAPIStatus envoyGoTcpUpstreamGetStringValue(void* r, int id, uint64_t* value_data, int* value_len) {
  return envoyGoTcpUpstreamHandlerWrapper(
      r, [id, value_data, value_len](std::shared_ptr<TcpUpstream>& filter) -> CAPIStatus {
        return filter->getStringValue(id, value_data, value_len);
      });
}

CAPIStatus envoyGoTcpUpstreamConnEnableHalfClose(void* r, int enabled) {
  return envoyGoTcpUpstreamHandlerWrapper(
      r, [enabled](std::shared_ptr<TcpUpstream>& filter) -> CAPIStatus {
        filter->enableHalfClose(enabled == 0 ? false:true);
        return CAPIOK;
      });
}

} // extern "C"
} // namespace Golang
} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
