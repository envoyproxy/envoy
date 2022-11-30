#include "contrib/golang/filters/http/source/golang_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Golang {

//
// These functions may be invoked in another go thread,
// which means may introduce race between go thread and envoy thread.
//

absl::string_view copyGoString(void* str) {
  if (str == nullptr) {
    return "";
  }
  auto goStr = reinterpret_cast<GoString*>(str);
  return absl::string_view(goStr->p, goStr->n); // NOLINT(modernize-return-braced-init-list)
}

extern "C" {

void envoyGoFilterHandlerWrapper(void* r, std::function<void(std::shared_ptr<Filter>&)> f) {
  auto req = reinterpret_cast<httpRequestInternal*>(r);
  auto weakFilter = req->weakFilter();
  if (auto filter = weakFilter.lock()) {
    f(filter);
  }
}

void envoyGoFilterHttpContinue(void* r, int status) {
  envoyGoFilterHandlerWrapper(r, [status](std::shared_ptr<Filter>& filter) {
    filter->continueStatus(static_cast<GolangStatus>(status));
  });
}

void envoyGoFilterHttpSendLocalReply(void* r, int response_code, void* body_text, void* headers,
                                     long long int grpc_status, void* details) {
  envoyGoFilterHandlerWrapper(r, [response_code, body_text, headers, grpc_status,
                                  details](std::shared_ptr<Filter>& filter) {
    UNREFERENCED_PARAMETER(headers);
    auto grpcStatus = static_cast<Grpc::Status::GrpcStatus>(grpc_status);
    filter->sendLocalReply(static_cast<Http::Code>(response_code), copyGoString(body_text), nullptr,
                           grpcStatus, copyGoString(details));
  });
}

// unsafe API, without copy memory from c to go.
void envoyGoFilterHttpGetHeader(void* r, void* key, void* value) {
  envoyGoFilterHandlerWrapper(r, [key, value](std::shared_ptr<Filter>& filter) {
    auto keyStr = copyGoString(key);
    auto v = filter->getHeader(keyStr);
    if (v.has_value()) {
      auto goValue = reinterpret_cast<GoString*>(value);
      goValue->p = v.value().data();
      goValue->n = v.value().length();
    }
  });
}

void envoyGoFilterHttpCopyHeaders(void* r, void* strs, void* buf) {
  envoyGoFilterHandlerWrapper(r, [strs, buf](std::shared_ptr<Filter>& filter) {
    auto goStrs = reinterpret_cast<GoString*>(strs);
    auto goBuf = reinterpret_cast<char*>(buf);
    filter->copyHeaders(goStrs, goBuf);
  });
}

void envoyGoFilterHttpSetHeader(void* r, void* key, void* value) {
  envoyGoFilterHandlerWrapper(r, [key, value](std::shared_ptr<Filter>& filter) {
    auto keyStr = copyGoString(key);
    auto valueStr = copyGoString(value);
    filter->setHeader(keyStr, valueStr);
  });
}

void envoyGoFilterHttpRemoveHeader(void* r, void* key) {
  envoyGoFilterHandlerWrapper(r, [key](std::shared_ptr<Filter>& filter) {
    // TODO: it's safe to skip copy
    auto keyStr = copyGoString(key);
    filter->removeHeader(keyStr);
  });
}

void envoyGoFilterHttpGetBuffer(void* r, unsigned long long int buffer_ptr, void* data) {
  envoyGoFilterHandlerWrapper(r, [buffer_ptr, data](std::shared_ptr<Filter>& filter) {
    auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
    filter->copyBuffer(buffer, reinterpret_cast<char*>(data));
  });
}

void envoyGoFilterHttpSetBufferHelper(void* r, unsigned long long int buffer_ptr, void* data,
                                      int length, bufferAction action) {
  envoyGoFilterHandlerWrapper(
      r, [buffer_ptr, data, length, action](std::shared_ptr<Filter>& filter) {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        auto value = absl::string_view(reinterpret_cast<const char*>(data), length);
        filter->setBufferHelper(buffer, value, action);
      });
}

void envoyGoFilterHttpCopyTrailers(void* r, void* strs, void* buf) {
  envoyGoFilterHandlerWrapper(r, [strs, buf](std::shared_ptr<Filter>& filter) {
    auto goStrs = reinterpret_cast<GoString*>(strs);
    auto goBuf = reinterpret_cast<char*>(buf);
    filter->copyTrailers(goStrs, goBuf);
  });
}

void envoyGoFilterHttpSetTrailer(void* r, void* key, void* value) {
  envoyGoFilterHandlerWrapper(r, [key, value](std::shared_ptr<Filter>& filter) {
    auto keyStr = copyGoString(key);
    auto valueStr = copyGoString(value);
    filter->setTrailer(keyStr, valueStr);
  });
}

void envoyGoFilterHttpGetStringValue(void* r, int id, void* value) {
  envoyGoFilterHandlerWrapper(r, [id, value](std::shared_ptr<Filter>& filter) {
    auto valueStr = reinterpret_cast<GoString*>(value);
    filter->getStringValue(id, valueStr);
  });
}

void envoyGoFilterHttpFinalize(void* r, int reason) {
  UNREFERENCED_PARAMETER(reason);
  auto req = reinterpret_cast<httpRequestInternal*>(r);
  delete req;
}
}

} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
