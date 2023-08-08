#include "contrib/golang/filters/http/source/golang_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Golang {

//
// These functions may be invoked in another go thread,
// which means may introduce race between go thread and envoy thread.
// So we use the envoy's dispatcher in the filter to post it, and make it only executes in the envoy
// thread.
//

// Deep copy GoString into std::string, including the string content,
// it's safe to use it after the current cgo call returns.
std::string copyGoString(void* str) {
  if (str == nullptr) {
    return "";
  }
  auto go_str = reinterpret_cast<GoString*>(str);
  return {go_str->p, static_cast<size_t>(go_str->n)};
}

// The returned absl::string_view only refer to the GoString, won't copy the string content into
// C++, should not use it after the current cgo call returns.
absl::string_view referGoString(void* str) {
  if (str == nullptr) {
    return "";
  }
  auto go_str = reinterpret_cast<GoString*>(str);
  return {go_str->p, static_cast<size_t>(go_str->n)};
}

absl::string_view stringViewFromGoSlice(void* slice) {
  if (slice == nullptr) {
    return "";
  }
  auto go_slice = reinterpret_cast<GoSlice*>(slice);
  return {static_cast<const char*>(go_slice->data), static_cast<size_t>(go_slice->len)};
}

std::vector<std::string> stringsFromGoSlice(void* slice) {
  std::vector<std::string> list;
  if (slice == nullptr) {
    return list;
  }
  auto go_slice = reinterpret_cast<GoSlice*>(slice);
  auto str = reinterpret_cast<GoString*>(go_slice->data);
  for (auto i = 0; i < go_slice->len; i += 2) {
    auto key = std::string(static_cast<const char*>(str->p), str->n);
    str++;
    auto value = std::string(static_cast<const char*>(str->p), str->n);
    str++;
    list.push_back(key);
    list.push_back(value);
  }
  return list;
}

#ifdef __cplusplus
extern "C" {
#endif

CAPIStatus envoyGoFilterHandlerWrapper(void* r,
                                       std::function<CAPIStatus(std::shared_ptr<Filter>&)> f) {
  auto req = reinterpret_cast<httpRequestInternal*>(r);
  auto weak_filter = req->weakFilter();
  if (auto filter = weak_filter.lock()) {
    return f(filter);
  }
  return CAPIStatus::CAPIFilterIsGone;
}

CAPIStatus envoyGoFilterHttpContinue(void* r, int status) {
  return envoyGoFilterHandlerWrapper(r, [status](std::shared_ptr<Filter>& filter) -> CAPIStatus {
    return filter->continueStatus(static_cast<GolangStatus>(status));
  });
}

CAPIStatus envoyGoFilterHttpSendLocalReply(void* r, int response_code, void* body_text,
                                           void* headers, long long int grpc_status,
                                           void* details) {
  return envoyGoFilterHandlerWrapper(
      r,
      [response_code, body_text, headers, grpc_status,
       details](std::shared_ptr<Filter>& filter) -> CAPIStatus {
        auto header_values = stringsFromGoSlice(headers);
        std::function<void(Http::ResponseHeaderMap&)> modify_headers =
            [header_values](Http::ResponseHeaderMap& headers) -> void {
          for (size_t i = 0; i < header_values.size(); i += 2) {
            const auto& key = header_values[i];
            const auto& value = header_values[i + 1];
            if (value.length() > 0) {
              headers.setCopy(Http::LowerCaseString(key), value);
            }
          }
        };
        auto status = static_cast<Grpc::Status::GrpcStatus>(grpc_status);

        // Deep clone the GoString into C++, since the GoString may be freed after the function
        // returns, while they may still be used in the callback.
        return filter->sendLocalReply(static_cast<Http::Code>(response_code),
                                      copyGoString(body_text), modify_headers, status,
                                      copyGoString(details));
      });
}

// unsafe API, without copy memory from c to go.
CAPIStatus envoyGoFilterHttpGetHeader(void* r, void* key, void* value) {
  return envoyGoFilterHandlerWrapper(r,
                                     [key, value](std::shared_ptr<Filter>& filter) -> CAPIStatus {
                                       auto key_str = referGoString(key);
                                       auto go_value = reinterpret_cast<GoString*>(value);
                                       return filter->getHeader(key_str, go_value);
                                     });
}

CAPIStatus envoyGoFilterHttpCopyHeaders(void* r, void* strs, void* buf) {
  return envoyGoFilterHandlerWrapper(r, [strs, buf](std::shared_ptr<Filter>& filter) -> CAPIStatus {
    auto go_strs = reinterpret_cast<GoString*>(strs);
    auto go_buf = reinterpret_cast<char*>(buf);
    return filter->copyHeaders(go_strs, go_buf);
  });
}

CAPIStatus envoyGoFilterHttpSetHeaderHelper(void* r, void* key, void* value, headerAction act) {
  return envoyGoFilterHandlerWrapper(
      r, [key, value, act](std::shared_ptr<Filter>& filter) -> CAPIStatus {
        auto key_str = referGoString(key);
        auto value_str = referGoString(value);
        return filter->setHeader(key_str, value_str, act);
      });
}

CAPIStatus envoyGoFilterHttpRemoveHeader(void* r, void* key) {
  return envoyGoFilterHandlerWrapper(r, [key](std::shared_ptr<Filter>& filter) -> CAPIStatus {
    auto key_str = referGoString(key);
    return filter->removeHeader(key_str);
  });
}

CAPIStatus envoyGoFilterHttpGetBuffer(void* r, unsigned long long int buffer_ptr, void* data) {
  return envoyGoFilterHandlerWrapper(
      r, [buffer_ptr, data](std::shared_ptr<Filter>& filter) -> CAPIStatus {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        return filter->copyBuffer(buffer, reinterpret_cast<char*>(data));
      });
}

CAPIStatus envoyGoFilterHttpSetBufferHelper(void* r, unsigned long long int buffer_ptr, void* data,
                                            int length, bufferAction action) {
  return envoyGoFilterHandlerWrapper(
      r, [buffer_ptr, data, length, action](std::shared_ptr<Filter>& filter) -> CAPIStatus {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        auto value = absl::string_view(reinterpret_cast<const char*>(data), length);
        return filter->setBufferHelper(buffer, value, action);
      });
}

CAPIStatus envoyGoFilterHttpCopyTrailers(void* r, void* strs, void* buf) {
  return envoyGoFilterHandlerWrapper(r, [strs, buf](std::shared_ptr<Filter>& filter) -> CAPIStatus {
    auto go_strs = reinterpret_cast<GoString*>(strs);
    auto go_buf = reinterpret_cast<char*>(buf);
    return filter->copyTrailers(go_strs, go_buf);
  });
}

CAPIStatus envoyGoFilterHttpSetTrailer(void* r, void* key, void* value, headerAction act) {
  return envoyGoFilterHandlerWrapper(
      r, [key, value, act](std::shared_ptr<Filter>& filter) -> CAPIStatus {
        auto key_str = referGoString(key);
        auto value_str = referGoString(value);
        return filter->setTrailer(key_str, value_str, act);
      });
}

CAPIStatus envoyGoFilterHttpRemoveTrailer(void* r, void* key) {
  return envoyGoFilterHandlerWrapper(r, [key](std::shared_ptr<Filter>& filter) -> CAPIStatus {
    auto key_str = referGoString(key);
    return filter->removeTrailer(key_str);
  });
}

CAPIStatus envoyGoFilterHttpGetStringValue(void* r, int id, void* value) {
  return envoyGoFilterHandlerWrapper(r, [id, value](std::shared_ptr<Filter>& filter) -> CAPIStatus {
    auto value_str = reinterpret_cast<GoString*>(value);
    return filter->getStringValue(id, value_str);
  });
}

CAPIStatus envoyGoFilterHttpGetIntegerValue(void* r, int id, void* value) {
  return envoyGoFilterHandlerWrapper(r, [id, value](std::shared_ptr<Filter>& filter) -> CAPIStatus {
    auto value_int = reinterpret_cast<uint64_t*>(value);
    return filter->getIntegerValue(id, value_int);
  });
}

CAPIStatus envoyGoFilterHttpGetDynamicMetadata(void* r, void* name, void* buf) {
  return envoyGoFilterHandlerWrapper(r, [name, buf](std::shared_ptr<Filter>& filter) -> CAPIStatus {
    auto name_str = copyGoString(name);
    auto buf_slice = reinterpret_cast<GoSlice*>(buf);
    return filter->getDynamicMetadata(name_str, buf_slice);
  });
}

CAPIStatus envoyGoFilterHttpSetDynamicMetadata(void* r, void* name, void* key, void* buf) {
  return envoyGoFilterHandlerWrapper(
      r, [name, key, buf](std::shared_ptr<Filter>& filter) -> CAPIStatus {
        auto name_str = copyGoString(name);
        auto key_str = copyGoString(key);
        auto buf_str = stringViewFromGoSlice(buf);
        return filter->setDynamicMetadata(name_str, key_str, buf_str);
      });
}

void envoyGoFilterHttpFinalize(void* r, int reason) {
  UNREFERENCED_PARAMETER(reason);
  // req is used by go, so need to use raw memory and then it is safe to release at the gc finalize
  // phase of the go object.
  auto req = reinterpret_cast<httpRequestInternal*>(r);
  delete req;
}

CAPIStatus envoyGoFilterHttpSendPanicReply(void* r, void* details) {
  return envoyGoFilterHandlerWrapper(r, [details](std::shared_ptr<Filter>& filter) -> CAPIStatus {
    // Since this is only used for logs we don't need to deep copy.
    return filter->sendPanicReply(referGoString(details));
  });
}

CAPIStatus envoyGoFilterHttpSetStringFilterState(void* r, void* key, void* value, int state_type,
                                                 int life_span, int stream_sharing) {
  return envoyGoFilterHandlerWrapper(r,
                                     [key, value, state_type, life_span, stream_sharing](
                                         std::shared_ptr<Filter>& filter) -> CAPIStatus {
                                       auto key_str = referGoString(key);
                                       auto value_str = referGoString(value);
                                       return filter->setStringFilterState(key_str, value_str,
                                                                           state_type, life_span,
                                                                           stream_sharing);
                                     });
}

CAPIStatus envoyGoFilterHttpGetStringFilterState(void* r, void* key, void* value) {
  return envoyGoFilterHandlerWrapper(r,
                                     [key, value](std::shared_ptr<Filter>& filter) -> CAPIStatus {
                                       auto key_str = referGoString(key);
                                       auto value_str = reinterpret_cast<GoString*>(value);
                                       return filter->getStringFilterState(key_str, value_str);
                                     });
}

#ifdef __cplusplus
}
#endif

} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
