#include "mocks.h"

#include "envoy/buffer/buffer.h"
#include "envoy/common/optref.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/header_map.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Http {

MockConnectionCallbacks::MockConnectionCallbacks() = default;
MockConnectionCallbacks::~MockConnectionCallbacks() = default;

MockServerConnectionCallbacks::MockServerConnectionCallbacks() = default;
MockServerConnectionCallbacks::~MockServerConnectionCallbacks() = default;

MockFilterManagerCallbacks::MockFilterManagerCallbacks() {
  ON_CALL(*this, informationalHeaders()).WillByDefault(Invoke([this]() -> ResponseHeaderMapOptRef {
    return makeOptRefFromPtr(informational_headers_.get());
  }));
  ON_CALL(*this, responseHeaders()).WillByDefault(Invoke([this]() -> ResponseHeaderMapOptRef {
    return makeOptRefFromPtr(response_headers_.get());
  }));
  ON_CALL(*this, responseTrailers()).WillByDefault(Invoke([this]() -> ResponseTrailerMapOptRef {
    return makeOptRefFromPtr(response_trailers_.get());
  }));
}
MockFilterManagerCallbacks::~MockFilterManagerCallbacks() = default;

MockStreamCallbacks::MockStreamCallbacks() = default;
MockStreamCallbacks::~MockStreamCallbacks() = default;

MockServerConnection::MockServerConnection() {
  ON_CALL(*this, protocol()).WillByDefault(Invoke([this]() { return protocol_; }));
}

MockServerConnection::~MockServerConnection() = default;

MockClientConnection::MockClientConnection() = default;
MockClientConnection::~MockClientConnection() = default;

MockFilterChainManager::MockFilterChainManager() {
  ON_CALL(*this, applyFilterFactoryCb(_, _))
      .WillByDefault(
          Invoke([this](FilterContext, FilterFactoryCb& factory) { factory(callbacks_); }));
}

MockFilterChainManager::~MockFilterChainManager() = default;

MockFilterChainFactory::MockFilterChainFactory() = default;
MockFilterChainFactory::~MockFilterChainFactory() = default;

template <class T> static void initializeMockStreamFilterCallbacks(T& callbacks) {
  callbacks.cluster_info_.reset(new NiceMock<Upstream::MockClusterInfo>());
  callbacks.route_.reset(new NiceMock<Router::MockRoute>());
  ON_CALL(callbacks, dispatcher()).WillByDefault(ReturnRef(callbacks.dispatcher_));
  ON_CALL(callbacks, streamInfo()).WillByDefault(ReturnRef(callbacks.stream_info_));
  ON_CALL(callbacks, route()).WillByDefault(Return(callbacks.route_));
  ON_CALL(callbacks, clusterInfo()).WillByDefault(Return(callbacks.cluster_info_));
  ON_CALL(callbacks, downstreamCallbacks())
      .WillByDefault(
          Return(OptRef<DownstreamStreamFilterCallbacks>{callbacks.downstream_callbacks_}));
}

MockStreamDecoderFilterCallbacks::MockStreamDecoderFilterCallbacks() {
  initializeMockStreamFilterCallbacks(*this);
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
  ON_CALL(*this, decodingBuffer()).WillByDefault(Invoke(&buffer_, &Buffer::InstancePtr::get));

  ON_CALL(*this, addDownstreamWatermarkCallbacks(_))
      .WillByDefault(Invoke([this](DownstreamWatermarkCallbacks& callbacks) -> void {
        callbacks_.push_back(&callbacks);
      }));

  ON_CALL(*this, removeDownstreamWatermarkCallbacks(_))
      .WillByDefault(Invoke([this](DownstreamWatermarkCallbacks& callbacks) -> void {
        callbacks_.remove(&callbacks);
      }));

  ON_CALL(*this, activeSpan()).WillByDefault(ReturnRef(active_span_));
  ON_CALL(*this, tracingConfig())
      .WillByDefault(Return(makeOptRef<const Tracing::Config>(tracing_config_)));
  ON_CALL(*this, scope()).WillByDefault(ReturnRef(scope_));
  ON_CALL(*this, sendLocalReply(_, _, _, _, _))
      .WillByDefault(Invoke([this](Code code, absl::string_view body,
                                   std::function<void(ResponseHeaderMap & headers)> modify_headers,
                                   const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                                   absl::string_view details) {
        sendLocalReply_(code, body, modify_headers, grpc_status, details);
      }));
  ON_CALL(*this, routeConfig())
      .WillByDefault(Return(absl::optional<Router::ConfigConstSharedPtr>()));
  ON_CALL(*this, upstreamOverrideHost())
      .WillByDefault(Return(absl::optional<Upstream::LoadBalancerContext::OverrideHost>()));

  ON_CALL(*this, mostSpecificPerFilterConfig())
      .WillByDefault(Invoke([this]() -> const Router::RouteSpecificFilterConfig* {
        auto route = this->route();
        if (route == nullptr) {
          return nullptr;
        }
        return route->mostSpecificPerFilterConfig("envoy.filter");
      }));
  ON_CALL(*this, perFilterConfigs())
      .WillByDefault(Invoke([this]() -> Router::RouteSpecificFilterConfigs {
        auto route = this->route();
        if (route == nullptr) {
          return {};
        }
        return route->perFilterConfigs("envoy.filter");
      }));
}

MockStreamDecoderFilterCallbacks::~MockStreamDecoderFilterCallbacks() = default;

void MockStreamDecoderFilterCallbacks::sendLocalReply_(
    Code code, absl::string_view body,
    std::function<void(ResponseHeaderMap& headers)> modify_headers,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status, absl::string_view details) {
  Utility::sendLocalReply(
      stream_destroyed_,
      Utility::EncodeFunctions{
          nullptr, nullptr,
          [this, modify_headers, details](ResponseHeaderMapPtr&& headers, bool end_stream) -> void {
            if (modify_headers != nullptr) {
              modify_headers(*headers);
            }
            encodeHeaders(std::move(headers), end_stream, details);
          },
          [this](Buffer::Instance& data, bool end_stream) -> void {
            encodeData(data, end_stream);
          }},
      Utility::LocalReplyData{is_grpc_request_, code, body, grpc_status, is_head_request_});
}

MockStreamEncoderFilterCallbacks::MockStreamEncoderFilterCallbacks() {
  initializeMockStreamFilterCallbacks(*this);
  ON_CALL(*this, encodingBuffer()).WillByDefault(Invoke(&buffer_, &Buffer::InstancePtr::get));
  ON_CALL(*this, activeSpan()).WillByDefault(ReturnRef(active_span_));
  ON_CALL(*this, tracingConfig())
      .WillByDefault(Return(makeOptRef<const Tracing::Config>(tracing_config_)));
  ON_CALL(*this, scope()).WillByDefault(ReturnRef(scope_));

  ON_CALL(*this, mostSpecificPerFilterConfig())
      .WillByDefault(Invoke([this]() -> const Router::RouteSpecificFilterConfig* {
        auto route = this->route();
        if (route == nullptr) {
          return nullptr;
        }
        return route->mostSpecificPerFilterConfig("envoy.filter");
      }));
  ON_CALL(*this, perFilterConfigs())
      .WillByDefault(Invoke([this]() -> Router::RouteSpecificFilterConfigs {
        auto route = this->route();
        if (route == nullptr) {
          return {};
        }
        return route->perFilterConfigs("envoy.filter");
      }));
}

MockStreamEncoderFilterCallbacks::~MockStreamEncoderFilterCallbacks() = default;

MockStreamDecoderFilter::MockStreamDecoderFilter() {
  ON_CALL(*this, setDecoderFilterCallbacks(_))
      .WillByDefault(Invoke(
          [this](StreamDecoderFilterCallbacks& callbacks) -> void { callbacks_ = &callbacks; }));
}

MockStreamDecoderFilter::~MockStreamDecoderFilter() = default;

MockStreamEncoderFilter::MockStreamEncoderFilter() {
  ON_CALL(*this, setEncoderFilterCallbacks(_))
      .WillByDefault(Invoke(
          [this](StreamEncoderFilterCallbacks& callbacks) -> void { callbacks_ = &callbacks; }));
}

MockStreamEncoderFilter::~MockStreamEncoderFilter() = default;

MockStreamFilter::MockStreamFilter() {
  ON_CALL(*this, setDecoderFilterCallbacks(_))
      .WillByDefault(Invoke([this](StreamDecoderFilterCallbacks& callbacks) -> void {
        decoder_callbacks_ = &callbacks;
      }));
  ON_CALL(*this, setEncoderFilterCallbacks(_))
      .WillByDefault(Invoke([this](StreamEncoderFilterCallbacks& callbacks) -> void {
        encoder_callbacks_ = &callbacks;
      }));
}

MockStreamFilter::~MockStreamFilter() = default;

MockAsyncClient::MockAsyncClient() {
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
}
MockAsyncClient::~MockAsyncClient() = default;

MockAsyncClientCallbacks::MockAsyncClientCallbacks() = default;
MockAsyncClientCallbacks::~MockAsyncClientCallbacks() = default;

MockAsyncClientStreamCallbacks::MockAsyncClientStreamCallbacks() = default;
MockAsyncClientStreamCallbacks::~MockAsyncClientStreamCallbacks() = default;

MockAsyncClientRequest::MockAsyncClientRequest(MockAsyncClient* client) : client_(client) {}
MockAsyncClientRequest::~MockAsyncClientRequest() { client_->onRequestDestroy(); }

MockAsyncClientStream::MockAsyncClientStream() = default;
MockAsyncClientStream::~MockAsyncClientStream() {
  if (destructor_callback_) {
    (*destructor_callback_)();
  }
};

MockFilterChainFactoryCallbacks::MockFilterChainFactoryCallbacks() = default;
MockFilterChainFactoryCallbacks::~MockFilterChainFactoryCallbacks() = default;

} // namespace Http

namespace Http {

IsSubsetOfHeadersMatcher IsSubsetOfHeaders(const HeaderMap& expected_headers) {
  return {expected_headers};
}

IsSupersetOfHeadersMatcher IsSupersetOfHeaders(const HeaderMap& expected_headers) {
  return {expected_headers};
}

MockReceivedSettings::MockReceivedSettings() {
  ON_CALL(*this, maxConcurrentStreams()).WillByDefault(ReturnRef(max_concurrent_streams_));
}

} // namespace Http
} // namespace Envoy
