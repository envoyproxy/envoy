#include "contrib/golang/filters/network/source/golang.h"

#include <cstdint>

#include "envoy/network/connection.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Golang {

FilterConfig::FilterConfig(
    const envoy::extensions::filters::network::golang::v3alpha::Config& proto_config)
    : library_id_(proto_config.library_id()), library_path_(proto_config.library_path()),
      plugin_name_(proto_config.plugin_name()), plugin_config_(proto_config.plugin_config()) {}

void Filter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  dispatcher_ = &read_callbacks_->connection().dispatcher();
  read_callbacks_->connection().addConnectionCallbacks(*this);

  local_addr_ = callbacks.connection().connectionInfoProvider().localAddress()->asString();
  addr_ = callbacks.connection().connectionInfoProvider().directRemoteAddress()->asString();
}

void Filter::close(Network::ConnectionCloseType close_type) {
  if (closed_) {
    ENVOY_CONN_LOG(warn, "connection has closed, addr: {}", read_callbacks_->connection(), addr_);
    return;
  }
  ENVOY_CONN_LOG(debug, "close addr: {}, type: {}", read_callbacks_->connection(), addr_,
                 static_cast<int>(close_type));
  read_callbacks_->connection().close(close_type, "go_downstream_close");
}

void Filter::write(Buffer::Instance& buf, bool end_stream) {
  if (closed_) {
    ENVOY_CONN_LOG(warn, "connection has closed, addr: {}", read_callbacks_->connection(), addr_);
    return;
  }
  ENVOY_CONN_LOG(debug, "write to addr: {}, len: {}, end: {}", read_callbacks_->connection(), addr_,
                 buf.length(), end_stream);
  read_callbacks_->connection().write(buf, end_stream);
}

Network::FilterStatus Filter::onNewConnection() {
  ENVOY_CONN_LOG(debug, "onNewConnection, addr: {}, localAddr: {}", read_callbacks_->connection(),
                 addr_, local_addr_);
  wrapper_ = new FilterWrapper(weak_from_this());

  return Network::FilterStatus(dynamic_lib_->envoyGoFilterOnDownstreamConnection(
      wrapper_, reinterpret_cast<unsigned long long>(plugin_name_.data()), plugin_name_.length(),
      config_id_));
}

void Filter::onEvent(Network::ConnectionEvent event) {
  ENVOY_CONN_LOG(debug, "onEvent addr: {}, event: {}", read_callbacks_->connection(), addr_,
                 static_cast<int>(event));

  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    closed_ = true;
  }

  dynamic_lib_->envoyGoFilterOnDownstreamEvent(wrapper_, static_cast<int>(event));
}

Network::FilterStatus Filter::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(debug, "onData, addr: {}, len: {}, end: {}", read_callbacks_->connection(), addr_,
                 data.length(), end_stream);

  Buffer::RawSliceVector slice_vector = data.getRawSlices();
  int slice_num = slice_vector.size();
  unsigned long long* slices = new unsigned long long[2 * slice_num];
  for (int i = 0; i < slice_num; i++) {
    const Buffer::RawSlice& s = slice_vector[i];
    slices[2 * i] = reinterpret_cast<unsigned long long>(s.mem_);
    slices[2 * i + 1] = s.len_;
  }

  auto ret = dynamic_lib_->envoyGoFilterOnDownstreamData(
      wrapper_, data.length(), reinterpret_cast<GoUint64>(slices), slice_num, end_stream);

  // TODO: do not drain buffer by default
  data.drain(data.length());

  delete[] slices;

  return Network::FilterStatus(ret);
}

Network::FilterStatus Filter::onWrite(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(debug, "onWrite, addr: {}, len: {}, end: {}", read_callbacks_->connection(), addr_,
                 data.length(), end_stream);

  Buffer::RawSliceVector slice_vector = data.getRawSlices();
  int slice_num = slice_vector.size();
  unsigned long long* slices = new unsigned long long[2 * slice_num];
  for (int i = 0; i < slice_num; i++) {
    const Buffer::RawSlice& s = slice_vector[i];
    slices[2 * i] = reinterpret_cast<unsigned long long>(s.mem_);
    slices[2 * i + 1] = s.len_;
  }

  auto ret = dynamic_lib_->envoyGoFilterOnDownstreamWrite(
      wrapper_, data.length(), reinterpret_cast<GoUint64>(slices), slice_num, end_stream);

  delete[] slices;

  return Network::FilterStatus(ret);
}

CAPIStatus Filter::setFilterState(absl::string_view key, absl::string_view value, int state_type,
                                  int life_span, int stream_sharing) {
  // lock until this function return since it may running in a Go thread.
  Thread::LockGuard lock(mutex_);
  if (closed_) {
    ENVOY_CONN_LOG(warn, "connection has closed, addr: {}", read_callbacks_->connection(), addr_);
    return CAPIStatus::CAPIFilterIsDestroy;
  }

  if (dispatcher_->isThreadSafe()) {
    read_callbacks_->connection().streamInfo().filterState()->setData(
        key, std::make_shared<GoStringFilterState>(value),
        static_cast<StreamInfo::FilterState::StateType>(state_type),
        static_cast<StreamInfo::FilterState::LifeSpan>(life_span),
        static_cast<StreamInfo::StreamSharingMayImpactPooling>(stream_sharing));
  } else {
    auto key_str = std::string(key);
    auto filter_state = std::make_shared<GoStringFilterState>(value);
    auto weak_ptr = weak_from_this();
    dispatcher_->post(
        [this, weak_ptr, key_str, filter_state, state_type, life_span, stream_sharing] {
          if (!weak_ptr.expired() && !closed_) {
            Thread::LockGuard lock(mutex_);
            read_callbacks_->connection().streamInfo().filterState()->setData(
                key_str, filter_state, static_cast<StreamInfo::FilterState::StateType>(state_type),
                static_cast<StreamInfo::FilterState::LifeSpan>(life_span),
                static_cast<StreamInfo::StreamSharingMayImpactPooling>(stream_sharing));
          } else {
            ENVOY_CONN_LOG(info, "golang filter has gone or destroyed in setStringFilterState",
                           read_callbacks_->connection());
          }
        });
  }
  return CAPIStatus::CAPIOK;
}

CAPIStatus Filter::getFilterState(absl::string_view key, GoString* value_str) {
  // lock until this function return since it may running in a Go thread.
  Thread::LockGuard lock(mutex_);
  if (closed_) {
    ENVOY_CONN_LOG(warn, "connection has closed, addr: {}", read_callbacks_->connection(), addr_);
    return CAPIStatus::CAPIFilterIsDestroy;
  }

  if (dispatcher_->isThreadSafe()) {
    auto go_filter_state = read_callbacks_->connection()
                               .streamInfo()
                               .filterState()
                               ->getDataReadOnly<GoStringFilterState>(key);
    if (go_filter_state) {
      wrapper_->str_value_ = go_filter_state->value();
      value_str->p = wrapper_->str_value_.data();
      value_str->n = wrapper_->str_value_.length();
    }
  } else {
    auto key_str = std::string(key);
    auto weak_ptr = weak_from_this();
    dispatcher_->post([this, weak_ptr, key_str, value_str] {
      if (!weak_ptr.expired() && !closed_) {
        auto go_filter_state = read_callbacks_->connection()
                                   .streamInfo()
                                   .filterState()
                                   ->getDataReadOnly<GoStringFilterState>(key_str);
        if (go_filter_state) {
          wrapper_->str_value_ = go_filter_state->value();
          value_str->p = wrapper_->str_value_.data();
          value_str->n = wrapper_->str_value_.length();
        }
        dynamic_lib_->envoyGoFilterOnSemaDec(wrapper_);
      } else {
        ENVOY_CONN_LOG(info, "golang filter has gone or destroyed in setStringFilterState",
                       read_callbacks_->connection());
      }
    });
    return CAPIStatus::CAPIYield;
  }
  return CAPIStatus::CAPIOK;
}

} // namespace Golang
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
