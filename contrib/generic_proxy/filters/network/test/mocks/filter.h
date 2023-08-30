#pragma once

#include "test/mocks/tcp/mocks.h"
#include "test/mocks/upstream/host.h"

#include "contrib/generic_proxy/filters/network/source/interface/config.h"
#include "contrib/generic_proxy/filters/network/source/interface/filter.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

class MockDecoderFilter : public DecoderFilter {
public:
  MockDecoderFilter();

  MOCK_METHOD(void, onDestroy, ());

  MOCK_METHOD(void, setDecoderFilterCallbacks, (DecoderFilterCallback & callbacks));
  MOCK_METHOD(FilterStatus, onStreamDecoded, (Request & request));
};

class MockEncoderFilter : public EncoderFilter {
public:
  MockEncoderFilter();

  MOCK_METHOD(void, onDestroy, ());

  MOCK_METHOD(void, setEncoderFilterCallbacks, (EncoderFilterCallback & callbacks));
  MOCK_METHOD(FilterStatus, onStreamEncoded, (Response & response));
};

class MockStreamFilter : public StreamFilter {
public:
  MockStreamFilter();

  MOCK_METHOD(void, onDestroy, ());

  MOCK_METHOD(void, setEncoderFilterCallbacks, (EncoderFilterCallback & callbacks));
  MOCK_METHOD(FilterStatus, onStreamEncoded, (Response & response));

  MOCK_METHOD(void, setDecoderFilterCallbacks, (DecoderFilterCallback & callbacks));
  MOCK_METHOD(FilterStatus, onStreamDecoded, (Request & request));
};

class MockStreamFilterConfig : public NamedFilterConfigFactory {
public:
  MockStreamFilterConfig();

  MOCK_METHOD(FilterFactoryCb, createFilterFactoryFromProto,
              (const Protobuf::Message& config, const std::string& stat_prefix,
               Server::Configuration::FactoryContext& context));
  MOCK_METHOD(ProtobufTypes::MessagePtr, createEmptyConfigProto, ());
  MOCK_METHOD(ProtobufTypes::MessagePtr, createEmptyRouteConfigProto, ());
  MOCK_METHOD(RouteSpecificFilterConfigConstSharedPtr, createRouteSpecificFilterConfig,
              (const Protobuf::Message&, Server::Configuration::ServerFactoryContext&,
               ProtobufMessage::ValidationVisitor&));
  MOCK_METHOD(std::string, name, (), (const));
  MOCK_METHOD(std::set<std::string>, configTypes, ());
  MOCK_METHOD(bool, isTerminalFilter, ());
};

class MockFilterChainFactoryCallbacks : public FilterChainFactoryCallbacks {
public:
  MockFilterChainFactoryCallbacks() = default;

  MOCK_METHOD(void, addDecoderFilter, (DecoderFilterSharedPtr filter));
  MOCK_METHOD(void, addEncoderFilter, (EncoderFilterSharedPtr filter));
  MOCK_METHOD(void, addFilter, (StreamFilterSharedPtr filter));
};

class MockUpstreamBindingCallback : public UpstreamBindingCallback {
public:
  MockUpstreamBindingCallback();

  MOCK_METHOD(void, onBindFailure,
              (ConnectionPool::PoolFailureReason reason, absl::string_view transport_failure_reason,
               Upstream::HostDescriptionConstSharedPtr host));
  MOCK_METHOD(void, onBindSuccess,
              (Network::ClientConnection & conn, Upstream::HostDescriptionConstSharedPtr host));
};

class MockPendingResponseCallback : public PendingResponseCallback {
public:
  MockPendingResponseCallback();

  MOCK_METHOD(void, onDecodingSuccess, (ResponsePtr response, ExtendedOptions options));
  MOCK_METHOD(void, onDecodingFailure, ());
  MOCK_METHOD(void, writeToConnection, (Buffer::Instance & buffer));
  MOCK_METHOD(void, onConnectionClose, (Network::ConnectionEvent event));
};

class MockFilterChainManager : public FilterChainManager {
public:
  MockFilterChainManager();

  MOCK_METHOD(void, applyFilterFactoryCb, (FilterContext context, FilterFactoryCb& factory));

  testing::NiceMock<MockFilterChainFactoryCallbacks> callbacks_;
  std::vector<FilterContext> contexts_;
};

template <class Base> class MockStreamFilterCallbacks : public Base {
public:
  MOCK_METHOD(Envoy::Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(const CodecFactory&, downstreamCodec, ());
  MOCK_METHOD(void, resetStream, ());
  MOCK_METHOD(const RouteEntry*, routeEntry, (), (const));
  MOCK_METHOD(const RouteSpecificFilterConfig*, perFilterConfig, (), (const));
  MOCK_METHOD(const StreamInfo::StreamInfo&, streamInfo, (), (const));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
  MOCK_METHOD(Tracing::Span&, activeSpan, ());
  MOCK_METHOD(OptRef<const Tracing::Config>, tracingConfig, (), (const));
  MOCK_METHOD(absl::optional<ExtendedOptions>, requestOptions, (), (const));
  MOCK_METHOD(absl::optional<ExtendedOptions>, responseOptions, (), (const));
  MOCK_METHOD(const Network::Connection*, connection, (), (const));
};

class MockUpstreamManager : public UpstreamManager {
public:
  MockUpstreamManager();

  MOCK_METHOD(void, registerUpstreamCallback, (uint64_t stream_id, UpstreamBindingCallback& cb));
  MOCK_METHOD(void, unregisterUpstreamCallback, (uint64_t stream_id));

  MOCK_METHOD(void, registerResponseCallback, (uint64_t stream_id, PendingResponseCallback& cb));
  MOCK_METHOD(void, unregisterResponseCallback, (uint64_t stream_id));

  void setupConnectionPool(Upstream::TcpPoolData&& data) {
    // Mock the connection creation and callbacks.
    upstream_cancellable_ = data.newConnection(pool_callbacks_);
  }

  void callOnBindSuccess(uint64_t stream_id) {
    auto it = upstream_callbacks_.find(stream_id);
    auto cb = it->second;

    upstream_callbacks_.erase(it);
    cb->onBindSuccess(upstream_conn_, upstream_host_);
  }

  void callOnBindFailure(uint64_t stream_id, ConnectionPool::PoolFailureReason reason) {
    auto it = upstream_callbacks_.find(stream_id);
    auto cb = it->second;

    upstream_callbacks_.erase(it);
    cb->onBindFailure(reason, "", upstream_host_);
  }

  void callOnDecodingSuccess(uint64_t stream_id, ResponsePtr response, ExtendedOptions options) {
    auto it = response_callbacks_.find(stream_id);
    auto cb = it->second;

    response_callbacks_.erase(it);

    cb->onDecodingSuccess(std::move(response), options);
  }

  void callOnConnectionClose(uint64_t stream_id, Network::ConnectionEvent event) {
    auto it = response_callbacks_.find(stream_id);
    auto cb = it->second;

    response_callbacks_.erase(it);

    cb->onConnectionClose(event);
  }

  void callOnDecodingFailure(uint64_t stream_id) {
    auto it = response_callbacks_.find(stream_id);
    auto cb = it->second;

    response_callbacks_.erase(it);
    cb->onDecodingFailure();
  }

  bool call_on_bind_success_immediately_{};
  bool call_on_bind_failure_immediately_{};

  std::shared_ptr<testing::NiceMock<Upstream::MockHostDescription>> upstream_host_;
  testing::NiceMock<Network::MockClientConnection> upstream_conn_;

  Tcp::ConnectionPool::Cancellable* upstream_cancellable_{};
  testing::NiceMock<Tcp::ConnectionPool::MockCallbacks> pool_callbacks_;

  absl::flat_hash_map<uint64_t, UpstreamBindingCallback*> upstream_callbacks_;
  absl::flat_hash_map<uint64_t, PendingResponseCallback*> response_callbacks_;
};

class MockDecoderFilterCallback : public MockStreamFilterCallbacks<DecoderFilterCallback> {
public:
  MockDecoderFilterCallback();

  MOCK_METHOD(void, sendLocalReply, (Status, ResponseUpdateFunction&&));
  MOCK_METHOD(void, continueDecoding, ());
  MOCK_METHOD(void, upstreamResponse, (ResponsePtr response, ExtendedOptions options));
  MOCK_METHOD(void, completeDirectly, ());
  MOCK_METHOD(void, bindUpstreamConn, (Upstream::TcpPoolData &&));
  MOCK_METHOD(OptRef<UpstreamManager>, boundUpstreamConn, ());

  bool has_upstream_manager_{};
  testing::NiceMock<MockUpstreamManager> upstream_manager_;
};

class MockEncoderFilterCallback : public MockStreamFilterCallbacks<EncoderFilterCallback> {
public:
  MOCK_METHOD(void, continueEncoding, ());
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
