#include "contrib/generic_proxy/filters/network/test/mocks/filter.h"

#include <cstdint>

#include "source/common/protobuf/protobuf.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

MockStreamFilterConfig::MockStreamFilterConfig() {
  ON_CALL(*this, createEmptyConfigProto()).WillByDefault(Invoke([]() {
    return std::make_unique<ProtobufWkt::Struct>();
  }));
  ON_CALL(*this, createFilterFactoryFromProto(_, _, _))
      .WillByDefault(Return([](FilterChainFactoryCallbacks&) {}));
  ON_CALL(*this, name()).WillByDefault(Return("envoy.filters.generic.mock_filter"));
  ON_CALL(*this, configTypes()).WillByDefault(Return(std::set<std::string>{}));
}

MockFilterChainManager::MockFilterChainManager() {
  ON_CALL(*this, applyFilterFactoryCb(_, _))
      .WillByDefault(Invoke([this](FilterContext context, FilterFactoryCb& factory) {
        contexts_.push_back(context);
        factory(callbacks_);
      }));
}

MockDecoderFilter::MockDecoderFilter() {
  ON_CALL(*this, onStreamDecoded(_)).WillByDefault(Return(FilterStatus::Continue));
}

MockEncoderFilter::MockEncoderFilter() {
  ON_CALL(*this, onStreamEncoded(_)).WillByDefault(Return(FilterStatus::Continue));
}

MockStreamFilter::MockStreamFilter() {
  ON_CALL(*this, onStreamEncoded(_)).WillByDefault(Return(FilterStatus::Continue));
  ON_CALL(*this, onStreamDecoded(_)).WillByDefault(Return(FilterStatus::Continue));
}

MockPendingResponseCallback::MockPendingResponseCallback() = default;

MockUpstreamBindingCallback::MockUpstreamBindingCallback() = default;

MockUpstreamManager::MockUpstreamManager() {
  ON_CALL(*this, registerUpstreamCallback(_, _))
      .WillByDefault(Invoke([this](uint64_t stream_id, UpstreamBindingCallback& cb) {
        if (call_on_bind_success_immediately_) {
          cb.onBindSuccess(upstream_conn_, upstream_host_);
          return;
        }

        if (call_on_bind_failure_immediately_) {
          cb.onBindFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure, "",
                           upstream_host_);
          return;
        }
        upstream_callbacks_[stream_id] = &cb;
      }));
  ON_CALL(*this, unregisterUpstreamCallback(_)).WillByDefault(Invoke([this](uint64_t stream_id) {
    upstream_callbacks_.erase(stream_id);
  }));

  ON_CALL(*this, registerResponseCallback(_, _))
      .WillByDefault(Invoke([this](uint64_t stream_id, PendingResponseCallback& cb) {
        response_callbacks_[stream_id] = &cb;
      }));

  ON_CALL(*this, unregisterResponseCallback(_)).WillByDefault(Invoke([this](uint64_t stream_id) {
    response_callbacks_.erase(stream_id);
  }));
}

MockDecoderFilterCallback::MockDecoderFilterCallback() {
  ON_CALL(*this, boundUpstreamConn()).WillByDefault(Invoke([this]() -> OptRef<UpstreamManager> {
    if (has_upstream_manager_) {
      return upstream_manager_;
    }
    return {};
  }));
}

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
