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

// Deep copy Go memory into std::string,
// it's safe to use it after the current cgo call returns.
std::string copyStringFromGoPointer(void* p, int len) {
  return {static_cast<const char*>(p), static_cast<size_t>(len)};
}

// The returned absl::string_view only refer to Go memory,
// should not use it after the current cgo call returns.
absl::string_view stringViewFromGoPointer(void* p, int len) {
  return {static_cast<const char*>(p), static_cast<size_t>(len)};
}

absl::string_view stringViewFromGoSlice(void* slice) {
  if (slice == nullptr) {
    return "";
  }
  auto go_slice = reinterpret_cast<GoSlice*>(slice);
  return {static_cast<const char*>(go_slice->data), static_cast<size_t>(go_slice->len)};
}

std::vector<std::string> stringsFromGoSlice(void* slice_data, int slice_len) {
  std::vector<std::string> list;
  if (slice_len == 0) {
    return list;
  }
  auto strs = reinterpret_cast<char**>(slice_data);
  for (auto i = 0; i < slice_len; i += 2) {
    auto key = std::string(strs[i + 0]);
    auto value = std::string(strs[i + 1]);
    list.push_back(key);
    list.push_back(value);
  }
  return list;
}

#ifdef __cplusplus
extern "C" {
#endif

CAPIStatus envoyGoFilterProcessStateHandlerWrapper(
    void* s, std::function<CAPIStatus(std::shared_ptr<Filter>&, ProcessorState&)> f) {
  auto state = static_cast<ProcessorState*>(reinterpret_cast<processState*>(s));
  if (!state->isProcessingInGo()) {
    return CAPIStatus::CAPINotInGo;
  }
  auto req = static_cast<HttpRequestInternal*>(state->req);
  auto weak_filter = req->weakFilter();
  if (auto filter = weak_filter.lock()) {
    return f(filter, *state);
  }
  return CAPIStatus::CAPIFilterIsGone;
}

CAPIStatus envoyGoFilterHandlerWrapper(void* r,
                                       std::function<CAPIStatus(std::shared_ptr<Filter>&)> f) {
  auto req = reinterpret_cast<HttpRequestInternal*>(r);
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

CAPIStatus
envoyGoConfigHandlerWrapper(void* c, std::function<CAPIStatus(std::shared_ptr<FilterConfig>&)> fc) {
  auto config = reinterpret_cast<httpConfigInternal*>(c);
  auto weak_filter_config = config->weakFilterConfig();
  if (auto filter_config = weak_filter_config.lock()) {
    return fc(filter_config);
  }
  return CAPIStatus::CAPIFilterIsGone;
}

CAPIStatus envoyGoFilterHttpContinue(void* s, int status) {
  return envoyGoFilterProcessStateHandlerWrapper(
      s, [status](std::shared_ptr<Filter>& filter, ProcessorState& state) -> CAPIStatus {
        return filter->continueStatus(state, static_cast<GolangStatus>(status));
      });
}

CAPIStatus envoyGoFilterHttpSendLocalReply(void* s, int response_code, void* body_text_data,
                                           int body_text_len, void* headers, int headers_num,
                                           long long int grpc_status, void* details_data,
                                           int details_len) {
  return envoyGoFilterProcessStateHandlerWrapper(
      s,
      [response_code, body_text_data, body_text_len, headers, headers_num, grpc_status,
       details_data,
       details_len](std::shared_ptr<Filter>& filter, ProcessorState& state) -> CAPIStatus {
        auto header_values = stringsFromGoSlice(headers, headers_num);
        std::function<void(Http::ResponseHeaderMap&)> modify_headers =
            [header_values](Http::ResponseHeaderMap& headers) -> void {
          for (size_t i = 0; i < header_values.size(); i += 2) {
            const auto& key = header_values[i];
            const auto& value = header_values[i + 1];
            if (value.length() > 0) {
              headers.addCopy(Http::LowerCaseString(key), value);
            }
          }
        };
        auto status = static_cast<Grpc::Status::GrpcStatus>(grpc_status);

        // Deep clone the GoString into C++, since the GoString may be freed after the function
        // returns, while they may still be used in the callback.
        return filter->sendLocalReply(state, static_cast<Http::Code>(response_code),
                                      copyStringFromGoPointer(body_text_data, body_text_len),
                                      modify_headers, status,
                                      copyStringFromGoPointer(details_data, details_len));
      });
}

CAPIStatus envoyGoFilterHttpSendPanicReply(void* s, void* details_data, int details_len) {
  return envoyGoFilterProcessStateHandlerWrapper(
      s,
      [details_data, details_len](std::shared_ptr<Filter>& filter,
                                  ProcessorState& state) -> CAPIStatus {
        // Since this is only used for logs we don't need to deep copy.
        auto details = stringViewFromGoPointer(details_data, details_len);
        return filter->sendPanicReply(state, details);
      });
}

// unsafe API, without copy memory from c to go.
CAPIStatus envoyGoFilterHttpGetHeader(void* s, void* key_data, int key_len, uint64_t* value_data,
                                      int* value_len) {
  return envoyGoFilterProcessStateHandlerWrapper(
      s,
      [key_data, key_len, value_data, value_len](std::shared_ptr<Filter>& filter,
                                                 ProcessorState& state) -> CAPIStatus {
        auto key_str = stringViewFromGoPointer(key_data, key_len);
        return filter->getHeader(state, key_str, value_data, value_len);
      });
}

CAPIStatus envoyGoFilterHttpCopyHeaders(void* s, void* strs, void* buf) {
  return envoyGoFilterProcessStateHandlerWrapper(
      s, [strs, buf](std::shared_ptr<Filter>& filter, ProcessorState& state) -> CAPIStatus {
        auto go_strs = reinterpret_cast<GoString*>(strs);
        auto go_buf = reinterpret_cast<char*>(buf);
        return filter->copyHeaders(state, go_strs, go_buf);
      });
}

CAPIStatus envoyGoFilterHttpSetHeaderHelper(void* s, void* key_data, int key_len, void* value_data,
                                            int value_len, headerAction act) {
  return envoyGoFilterProcessStateHandlerWrapper(
      s,
      [key_data, key_len, value_data, value_len, act](std::shared_ptr<Filter>& filter,
                                                      ProcessorState& state) -> CAPIStatus {
        auto key_str = stringViewFromGoPointer(key_data, key_len);
        auto value_str = stringViewFromGoPointer(value_data, value_len);
        return filter->setHeader(state, key_str, value_str, act);
      });
}

CAPIStatus envoyGoFilterHttpRemoveHeader(void* s, void* key_data, int key_len) {
  return envoyGoFilterProcessStateHandlerWrapper(
      s, [key_data, key_len](std::shared_ptr<Filter>& filter, ProcessorState& state) -> CAPIStatus {
        auto key_str = stringViewFromGoPointer(key_data, key_len);
        return filter->removeHeader(state, key_str);
      });
}

CAPIStatus envoyGoFilterHttpGetBuffer(void* s, uint64_t buffer_ptr, void* data) {
  return envoyGoFilterProcessStateHandlerWrapper(
      s, [buffer_ptr, data](std::shared_ptr<Filter>& filter, ProcessorState& state) -> CAPIStatus {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        return filter->copyBuffer(state, buffer, reinterpret_cast<char*>(data));
      });
}

CAPIStatus envoyGoFilterHttpDrainBuffer(void* s, uint64_t buffer_ptr, uint64_t length) {
  return envoyGoFilterProcessStateHandlerWrapper(
      s,
      [buffer_ptr, length](std::shared_ptr<Filter>& filter, ProcessorState& state) -> CAPIStatus {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        return filter->drainBuffer(state, buffer, length);
      });
}

CAPIStatus envoyGoFilterHttpSetBufferHelper(void* s, uint64_t buffer_ptr, void* data, int length,
                                            bufferAction action) {
  return envoyGoFilterProcessStateHandlerWrapper(
      s,
      [buffer_ptr, data, length, action](std::shared_ptr<Filter>& filter,
                                         ProcessorState& state) -> CAPIStatus {
        auto buffer = reinterpret_cast<Buffer::Instance*>(buffer_ptr);
        auto value = stringViewFromGoPointer(data, length);
        return filter->setBufferHelper(state, buffer, value, action);
      });
}

CAPIStatus envoyGoFilterHttpCopyTrailers(void* s, void* strs, void* buf) {
  return envoyGoFilterProcessStateHandlerWrapper(
      s, [strs, buf](std::shared_ptr<Filter>& filter, ProcessorState& state) -> CAPIStatus {
        auto go_strs = reinterpret_cast<GoString*>(strs);
        auto go_buf = reinterpret_cast<char*>(buf);
        return filter->copyTrailers(state, go_strs, go_buf);
      });
}

CAPIStatus envoyGoFilterHttpSetTrailer(void* s, void* key_data, int key_len, void* value_data,
                                       int value_len, headerAction act) {
  return envoyGoFilterProcessStateHandlerWrapper(
      s,
      [key_data, key_len, value_data, value_len, act](std::shared_ptr<Filter>& filter,
                                                      ProcessorState& state) -> CAPIStatus {
        auto key_str = stringViewFromGoPointer(key_data, key_len);
        auto value_str = stringViewFromGoPointer(value_data, value_len);
        return filter->setTrailer(state, key_str, value_str, act);
      });
}

CAPIStatus envoyGoFilterHttpRemoveTrailer(void* s, void* key_data, int key_len) {
  return envoyGoFilterProcessStateHandlerWrapper(
      s, [key_data, key_len](std::shared_ptr<Filter>& filter, ProcessorState& state) -> CAPIStatus {
        auto key_str = stringViewFromGoPointer(key_data, key_len);
        return filter->removeTrailer(state, key_str);
      });
}

CAPIStatus envoyGoFilterHttpClearRouteCache(void* r) {
  return envoyGoFilterHandlerWrapper(
      r, [](std::shared_ptr<Filter>& filter) -> CAPIStatus { return filter->clearRouteCache(); });
}

CAPIStatus envoyGoFilterHttpGetStringValue(void* r, int id, uint64_t* value_data, int* value_len) {
  return envoyGoFilterHandlerWrapper(
      r, [id, value_data, value_len](std::shared_ptr<Filter>& filter) -> CAPIStatus {
        return filter->getStringValue(id, value_data, value_len);
      });
}

CAPIStatus envoyGoFilterHttpGetIntegerValue(void* r, int id, uint64_t* value) {
  return envoyGoFilterHandlerWrapper(r, [id, value](std::shared_ptr<Filter>& filter) -> CAPIStatus {
    return filter->getIntegerValue(id, value);
  });
}

CAPIStatus envoyGoFilterHttpGetDynamicMetadata(void* r, void* name_data, int name_len,
                                               uint64_t* buf_data, int* buf_len) {
  return envoyGoFilterHandlerWrapper(
      r, [name_data, name_len, buf_data, buf_len](std::shared_ptr<Filter>& filter) -> CAPIStatus {
        auto name_str = copyStringFromGoPointer(name_data, name_len);
        return filter->getDynamicMetadata(name_str, buf_data, buf_len);
      });
}

CAPIStatus envoyGoFilterHttpSetDynamicMetadata(void* r, void* name_data, int name_len,
                                               void* key_data, int key_len, void* buf_data,
                                               int buf_len) {
  return envoyGoFilterHandlerWrapper(r,
                                     [name_data, name_len, key_data, key_len, buf_data,
                                      buf_len](std::shared_ptr<Filter>& filter) -> CAPIStatus {
                                       auto name_str = copyStringFromGoPointer(name_data, name_len);
                                       auto key_str = copyStringFromGoPointer(key_data, key_len);
                                       auto buf_str = stringViewFromGoPointer(buf_data, buf_len);
                                       return filter->setDynamicMetadata(name_str, key_str,
                                                                         buf_str);
                                     });
}

void envoyGoFilterHttpFinalize(void* r, int reason) {
  UNREFERENCED_PARAMETER(reason);
  // req is used by go, so need to use raw memory and then it is safe to release at the gc finalize
  // phase of the go object.
  auto req = reinterpret_cast<HttpRequestInternal*>(r);
  auto weak_filter = req->weakFilter();
  if (auto filter = weak_filter.lock()) {
    // Finalize must happens after onDestory, that means Filter is marked as destroyed.
    // When filter is still existing, it could happens in very low rate, since Golang GC
    // finalizer delays execution.
    // Now, the race is there might be filter method running, i.e. continueStatusInternal may invoke
    // onDestroy, and check state in request after it.
    // So, we'd better to defer delete the request.
    filter->deferredDeleteRequest(req);
  } else {
    // It's safe to delete directly since filter is not existing.
    delete req;
  }
}

void envoyGoConfigHttpFinalize(void* c) {
  // config is used by go, so need to use raw memory and then it is safe to release at the gc
  // finalize phase of the go object.
  auto config = reinterpret_cast<httpConfigInternal*>(c);
  delete config;
}

CAPIStatus envoyGoFilterHttpSetStringFilterState(void* r, void* key_data, int key_len,
                                                 void* value_data, int value_len, int state_type,
                                                 int life_span, int stream_sharing) {
  return envoyGoFilterHandlerWrapper(
      r,
      [key_data, key_len, value_data, value_len, state_type, life_span,
       stream_sharing](std::shared_ptr<Filter>& filter) -> CAPIStatus {
        auto key_str = stringViewFromGoPointer(key_data, key_len);
        auto value_str = stringViewFromGoPointer(value_data, value_len);
        return filter->setStringFilterState(key_str, value_str, state_type, life_span,
                                            stream_sharing);
      });
}

CAPIStatus envoyGoFilterHttpGetStringFilterState(void* r, void* key_data, int key_len,
                                                 uint64_t* value_data, int* value_len) {
  return envoyGoFilterHandlerWrapper(
      r, [key_data, key_len, value_data, value_len](std::shared_ptr<Filter>& filter) -> CAPIStatus {
        auto key_str = stringViewFromGoPointer(key_data, key_len);
        return filter->getStringFilterState(key_str, value_data, value_len);
      });
}

CAPIStatus envoyGoFilterHttpGetStringProperty(void* r, void* key_data, int key_len,
                                              uint64_t* value_data, int* value_len, int* rc) {
  return envoyGoFilterHandlerWrapper(r,
                                     [key_data, key_len, value_data, value_len,
                                      rc](std::shared_ptr<Filter>& filter) -> CAPIStatus {
                                       auto key_str = stringViewFromGoPointer(key_data, key_len);
                                       return filter->getStringProperty(key_str, value_data,
                                                                        value_len, rc);
                                     });
}

CAPIStatus envoyGoFilterHttpDefineMetric(void* c, uint32_t metric_type, void* name_data,
                                         int name_len, uint32_t* metric_id) {
  return envoyGoConfigHandlerWrapper(
      c,
      [metric_type, name_data, name_len,
       metric_id](std::shared_ptr<FilterConfig>& filter_config) -> CAPIStatus {
        auto name_str = stringViewFromGoPointer(name_data, name_len);
        return filter_config->defineMetric(metric_type, name_str, metric_id);
      });
}

CAPIStatus envoyGoFilterHttpIncrementMetric(void* c, uint32_t metric_id, int64_t offset) {
  return envoyGoConfigHandlerWrapper(
      c, [metric_id, offset](std::shared_ptr<FilterConfig>& filter_config) -> CAPIStatus {
        return filter_config->incrementMetric(metric_id, offset);
      });
}

CAPIStatus envoyGoFilterHttpGetMetric(void* c, uint32_t metric_id, uint64_t* value) {
  return envoyGoConfigHandlerWrapper(
      c, [metric_id, value](std::shared_ptr<FilterConfig>& filter_config) -> CAPIStatus {
        return filter_config->getMetric(metric_id, value);
      });
}

CAPIStatus envoyGoFilterHttpRecordMetric(void* c, uint32_t metric_id, uint64_t value) {
  return envoyGoConfigHandlerWrapper(
      c, [metric_id, value](std::shared_ptr<FilterConfig>& filter_config) -> CAPIStatus {
        return filter_config->recordMetric(metric_id, value);
      });
}

#ifdef __cplusplus
}
#endif

} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
