#include "contrib/golang/filters/network/source/golang.h"
#include "contrib/golang/filters/network/source/upstream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
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
  auto goStr = reinterpret_cast<GoString*>(str);
  return std::string{goStr->p, size_t(goStr->n)};
}

enum class ConnectionInfoType {
  LocalAddr,
  RemoteAddr,
};

extern "C" {

//
// Downstream
//

CAPIStatus envoyGoFilterDownstreamClose(void* f, int close_type) {
  auto* wrapper = reinterpret_cast<FilterWrapper*>(f);
  FilterWeakPtr& weak_ptr = wrapper->weakPtr;
  if (FilterSharedPtr f = weak_ptr.lock()) {
    f->dispatcher()->post([weak_ptr, close_type] {
      if (FilterSharedPtr filter = weak_ptr.lock()) {
        filter->close(static_cast<Network::ConnectionCloseType>(close_type));
      }
    });
    return CAPIStatus::CAPIOK;
  }
  return CAPIStatus::CAPIFilterIsGone;
}

CAPIStatus envoyGoFilterDownstreamWrite(void* f, void* buffer_ptr, int buffer_len, int end_stream) {
  auto* wrapper = reinterpret_cast<FilterWrapper*>(f);
  FilterWeakPtr& weak_ptr = wrapper->weakPtr;
  if (FilterSharedPtr f = weak_ptr.lock()) {
    // should do the copy right now, because the 'data' pointer still point at the go's heap
    Buffer::InstancePtr buffer =
        std::make_unique<Buffer::OwnedImpl>(reinterpret_cast<char*>(buffer_ptr), buffer_len);
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    f->dispatcher()->post([weak_ptr, end_stream, buf_raw_ptr = buffer.release()] {
      Buffer::InstancePtr buf = absl::WrapUnique(buf_raw_ptr);
      if (FilterSharedPtr filter = weak_ptr.lock()) {
        filter->write(*buf.get(), end_stream);
      }
    });
    return CAPIStatus::CAPIOK;
  }
  return CAPIStatus::CAPIFilterIsGone;
}

void envoyGoFilterDownstreamFinalize(void* f, int reason) {
  UNREFERENCED_PARAMETER(reason);
  auto* wrapper = reinterpret_cast<FilterWrapper*>(f);
  FilterWeakPtr& weak_ptr = wrapper->weakPtr;
  if (FilterSharedPtr filter = weak_ptr.lock()) {
    // make sure that the deconstructor is also executed by envoy wrk thread.
    filter->dispatcher()->post([wrapper] { delete wrapper; });
  } else {
    // the Filter is already deleted, just release the wrapper.
    delete wrapper;
  }
}

CAPIStatus envoyGoFilterDownstreamInfo(void* f, int info_type, void* ret) {
  auto* wrapper = reinterpret_cast<FilterWrapper*>(f);
  FilterWeakPtr& weak_ptr = wrapper->weakPtr;
  if (FilterSharedPtr filter = weak_ptr.lock()) {
    auto* goStr = reinterpret_cast<GoString*>(ret);
    switch (static_cast<ConnectionInfoType>(info_type)) {
    case ConnectionInfoType::LocalAddr:
      wrapper->strValue = filter->getLocalAddrStr();
      break;
    case ConnectionInfoType::RemoteAddr:
      wrapper->strValue = filter->getRemoteAddrStr();
      break;
    default:
      ASSERT(false, "invalid connection info type");
    }
    goStr->p = wrapper->strValue.data();
    goStr->n = wrapper->strValue.length();
    return CAPIStatus::CAPIOK;
  }
  return CAPIStatus::CAPIFilterIsGone;
}

//
// Upstream
//

void* envoyGoFilterUpstreamConnect(void* library_id, void* addr) {
  std::string id = copyGoString(library_id);
  auto dynamicLib = Dso::DsoManager<Dso::NetworkFilterDsoImpl>::getDsoByID(id);
  UpstreamConnPtr connPtr = std::make_shared<UpstreamConn>(copyGoString(addr), dynamicLib);
  UpstreamConnWrapper* wrapper = new UpstreamConnWrapper(connPtr);
  connPtr->setWrapper(wrapper);

  connPtr->dispatcher()->post([connPtr] { connPtr->connect(); });

  return static_cast<void*>(wrapper);
}

CAPIStatus envoyGoFilterUpstreamWrite(void* u, void* buffer_ptr, int buffer_len, int end_stream) {
  auto* wrapper = reinterpret_cast<UpstreamConnWrapper*>(u);
  UpstreamConnPtr& upConn = wrapper->sharedPtr;
  // should do the copy right now, because the 'data' pointer still point at the go's heap
  Buffer::InstancePtr buffer =
      std::make_unique<Buffer::OwnedImpl>(reinterpret_cast<char*>(buffer_ptr), buffer_len);
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  upConn->dispatcher()->post([upConn, end_stream, buf_raw_ptr = buffer.release()] {
    Buffer::InstancePtr buf = absl::WrapUnique(buf_raw_ptr);
    upConn->write(*buf.get(), end_stream);
  });
  return CAPIOK;
}

CAPIStatus envoyGoFilterUpstreamClose(void* u, int close_type) {
  auto* wrapper = reinterpret_cast<UpstreamConnWrapper*>(u);
  UpstreamConnPtr& upConn = wrapper->sharedPtr;
  upConn->dispatcher()->post([upConn, close_type] {
    upConn->close(static_cast<Network::ConnectionCloseType>(close_type));
  });
  return CAPIOK;
}

void envoyGoFilterUpstreamFinalize(void* u, int reason) {
  UNREFERENCED_PARAMETER(reason);
  auto* wrapper = reinterpret_cast<UpstreamConnWrapper*>(u);
  UpstreamConnPtr& upConn = wrapper->sharedPtr;
  // make sure that the deconstructor is also executed by envoy wrk thread
  upConn->dispatcher()->post([wrapper] { delete wrapper; });
}

CAPIStatus envoyGoFilterUpstreamInfo(void* u, int info_type, void* ret) {
  auto* wrapper = reinterpret_cast<UpstreamConnWrapper*>(u);
  UpstreamConnPtr& upConn = wrapper->sharedPtr;
  auto* goStr = reinterpret_cast<GoString*>(ret);
  switch (static_cast<ConnectionInfoType>(info_type)) {
  case ConnectionInfoType::LocalAddr:
    wrapper->strValue = upConn->getLocalAddrStr();
    break;
  case ConnectionInfoType::RemoteAddr:
    wrapper->strValue = upConn->getRemoteAddrStr();
    break;
  default:
    ASSERT(false, "invalid connection info type");
  }
  goStr->p = wrapper->strValue.data();
  goStr->n = wrapper->strValue.length();
  return CAPIStatus::CAPIOK;
}

} // extern "C"
} // namespace Golang
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
