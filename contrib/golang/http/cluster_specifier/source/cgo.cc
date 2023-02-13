#include "contrib/golang/filters/http/source/golang_filter.h"

namespace Envoy {
namespace Extensions {
namespace ClusterSpecifier {
namespace Golang {

//
// These functions should only be invoked in the current Envoy worker thread.
//

extern "C" {

CAPIStatus envoyGoFilterHttpCopyHeaders(void* r, void* strs, void* buf) {
  return envoyGoFilterHandlerWrapper(r, [strs, buf](std::shared_ptr<Filter>& filter) -> CAPIStatus {
    auto goStrs = reinterpret_cast<GoString*>(strs);
    auto goBuf = reinterpret_cast<char*>(buf);
    return filter->copyHeaders(goStrs, goBuf);
  });
}

CAPIStatus envoyGoFilterHttpSetHeader(void* r, void* key, void* value) {
  return envoyGoFilterHandlerWrapper(r,
                                     [key, value](std::shared_ptr<Filter>& filter) -> CAPIStatus {
                                       auto keyStr = copyGoString(key);
                                       auto valueStr = copyGoString(value);
                                       return filter->setHeader(keyStr, valueStr);
                                     });
}

CAPIStatus envoyGoFilterHttpRemoveHeader(void* r, void* key) {
  return envoyGoFilterHandlerWrapper(r, [key](std::shared_ptr<Filter>& filter) -> CAPIStatus {
    // TODO: it's safe to skip copy
    auto keyStr = copyGoString(key);
    return filter->removeHeader(keyStr);
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
    auto goStrs = reinterpret_cast<GoString*>(strs);
    auto goBuf = reinterpret_cast<char*>(buf);
    return filter->copyTrailers(goStrs, goBuf);
  });
}

CAPIStatus envoyGoFilterHttpSetTrailer(void* r, void* key, void* value) {
  return envoyGoFilterHandlerWrapper(r,
                                     [key, value](std::shared_ptr<Filter>& filter) -> CAPIStatus {
                                       auto keyStr = copyGoString(key);
                                       auto valueStr = copyGoString(value);
                                       return filter->setTrailer(keyStr, valueStr);
                                     });
}

CAPIStatus envoyGoFilterHttpGetStringValue(void* r, int id, void* value) {
  return envoyGoFilterHandlerWrapper(r, [id, value](std::shared_ptr<Filter>& filter) -> CAPIStatus {
    auto valueStr = reinterpret_cast<GoString*>(value);
    return filter->getStringValue(id, valueStr);
  });
}

void envoyGoFilterHttpFinalize(void* r, int reason) {
  UNREFERENCED_PARAMETER(reason);
  // req is used by go, so need to use raw memory and then it is safe to release at the gc finalize
  // phase of the go object.
  auto req = reinterpret_cast<httpRequestInternal*>(r);
  delete req;
}
}

} // namespace Golang
} // namespace ClusterSpecifier
} // namespace Extensions
} // namespace Envoy
