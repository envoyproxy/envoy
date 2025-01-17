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
  auto go_str = reinterpret_cast<GoString*>(str);
  return std::string{go_str->p, size_t(go_str->n)};
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
  FilterWeakPtr& weak_ptr = wrapper->filter_ptr_;
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
  FilterWeakPtr& weak_ptr = wrapper->filter_ptr_;
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
  FilterWeakPtr& weak_ptr = wrapper->filter_ptr_;
  if (FilterSharedPtr filter = weak_ptr.lock()) {
    // make sure that the deconstructor is also executed by envoy work thread.
    filter->dispatcher()->post([wrapper] { delete wrapper; });
  } else {
    // the Filter is already deleted, just release the wrapper.
    delete wrapper;
  }
}

CAPIStatus envoyGoFilterDownstreamInfo(void* f, int info_type, void* ret) {
  auto* wrapper = reinterpret_cast<FilterWrapper*>(f);
  FilterWeakPtr& weak_ptr = wrapper->filter_ptr_;
  if (FilterSharedPtr filter = weak_ptr.lock()) {
    auto* goStr = reinterpret_cast<GoString*>(ret);
    switch (static_cast<ConnectionInfoType>(info_type)) {
    case ConnectionInfoType::LocalAddr:
      wrapper->str_value_ = filter->getLocalAddrStr();
      break;
    case ConnectionInfoType::RemoteAddr:
      wrapper->str_value_ = filter->getRemoteAddrStr();
      break;
    default:
      PANIC_DUE_TO_CORRUPT_ENUM;
    }
    goStr->p = wrapper->str_value_.data();
    goStr->n = wrapper->str_value_.length();
    return CAPIStatus::CAPIOK;
  }
  return CAPIStatus::CAPIFilterIsGone;
}

//
// Upstream
//

void* envoyGoFilterUpstreamConnect(void* library_id, void* addr, uint64_t conn_id) {
  std::string id = copyGoString(library_id);
  auto dynamic_lib = Dso::DsoManager<Dso::NetworkFilterDsoImpl>::getDsoByID(id);
  UpstreamConnPtr conn_ptr =
      std::make_shared<UpstreamConn>(copyGoString(addr), dynamic_lib, conn_id);
  // the upstream connect wrapper will be deleted by envoyGoFilterUpstreamFinalize
  UpstreamConnWrapper* wrapper = new UpstreamConnWrapper(conn_ptr);
  conn_ptr->setWrapper(wrapper);

  conn_ptr->dispatcher()->post([conn_ptr] { conn_ptr->connect(); });

  return static_cast<void*>(wrapper);
}

CAPIStatus envoyGoFilterUpstreamConnEnableHalfClose(void* u, int enable_half_close) {
  auto* wrapper = reinterpret_cast<UpstreamConnWrapper*>(u);
  UpstreamConnPtr& conn_ptr = wrapper->conn_ptr_;

  conn_ptr->enableHalfClose(static_cast<bool>(enable_half_close));

  return CAPIOK;
}

CAPIStatus envoyGoFilterUpstreamWrite(void* u, void* buffer_ptr, int buffer_len, int end_stream) {
  auto* wrapper = reinterpret_cast<UpstreamConnWrapper*>(u);
  UpstreamConnPtr& conn_ptr = wrapper->conn_ptr_;
  // should do the copy right now, because the 'data' pointer still point at the go's heap
  Buffer::InstancePtr buffer =
      std::make_unique<Buffer::OwnedImpl>(reinterpret_cast<char*>(buffer_ptr), buffer_len);
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  conn_ptr->dispatcher()->post([conn_ptr, end_stream, buf_raw_ptr = buffer.release()] {
    Buffer::InstancePtr buf = absl::WrapUnique(buf_raw_ptr);
    conn_ptr->write(*buf.get(), end_stream);
  });
  return CAPIOK;
}

CAPIStatus envoyGoFilterUpstreamClose(void* u, int close_type) {
  auto* wrapper = reinterpret_cast<UpstreamConnWrapper*>(u);
  UpstreamConnPtr& conn_ptr = wrapper->conn_ptr_;
  conn_ptr->dispatcher()->post([conn_ptr, close_type] {
    conn_ptr->close(static_cast<Network::ConnectionCloseType>(close_type));
  });
  return CAPIOK;
}

void envoyGoFilterUpstreamFinalize(void* u, int reason) {
  UNREFERENCED_PARAMETER(reason);
  auto* wrapper = reinterpret_cast<UpstreamConnWrapper*>(u);
  UpstreamConnPtr& conn_ptr = wrapper->conn_ptr_;
  // make sure that the deconstructor is also executed by envoy work thread
  conn_ptr->dispatcher()->post([wrapper] { delete wrapper; });
}

CAPIStatus envoyGoFilterUpstreamInfo(void* u, int info_type, void* ret) {
  auto* wrapper = reinterpret_cast<UpstreamConnWrapper*>(u);
  UpstreamConnPtr& conn_ptr = wrapper->conn_ptr_;
  auto* goStr = reinterpret_cast<GoString*>(ret);
  switch (static_cast<ConnectionInfoType>(info_type)) {
  case ConnectionInfoType::LocalAddr:
    wrapper->str_value_ = conn_ptr->getLocalAddrStr();
    break;
  case ConnectionInfoType::RemoteAddr:
    wrapper->str_value_ = conn_ptr->getRemoteAddrStr();
    break;
  default:
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
  goStr->p = wrapper->str_value_.data();
  goStr->n = wrapper->str_value_.length();
  return CAPIStatus::CAPIOK;
}

CAPIStatus envoyGoFilterSetFilterState(void* f, void* key, void* value, int state_type,
                                       int life_span, int stream_sharing) {
  auto* wrapper = reinterpret_cast<FilterWrapper*>(f);
  FilterWeakPtr& weak_ptr = wrapper->filter_ptr_;
  if (FilterSharedPtr f = weak_ptr.lock()) {
    auto key_str = referGoString(key);
    auto value_str = referGoString(value);
    return f->setFilterState(key_str, value_str, state_type, life_span, stream_sharing);
  }
  return CAPIStatus::CAPIFilterIsGone;
}

CAPIStatus envoyGoFilterGetFilterState(void* f, void* key, void* value) {
  auto* wrapper = reinterpret_cast<FilterWrapper*>(f);
  FilterWeakPtr& weak_ptr = wrapper->filter_ptr_;
  if (FilterSharedPtr f = weak_ptr.lock()) {
    auto key_str = referGoString(key);
    auto value_str = reinterpret_cast<GoString*>(value);
    return f->getFilterState(key_str, value_str);
  }
  return CAPIStatus::CAPIFilterIsGone;
}

} // extern "C"
} // namespace Golang
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
