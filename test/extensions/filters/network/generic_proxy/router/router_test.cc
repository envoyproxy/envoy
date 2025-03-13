#include <memory>

#include "source/common/tracing/common_values.h"
#include "source/extensions/filters/network/generic_proxy/router/router.h"

#include "test/extensions/filters/network/generic_proxy/fake_codec.h"
#include "test/extensions/filters/network/generic_proxy/mocks/codec.h"
#include "test/extensions/filters/network/generic_proxy/mocks/filter.h"
#include "test/extensions/filters/network/generic_proxy/mocks/route.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Router {
namespace {

class MockGenericUpstream : public GenericUpstream {
public:
  MockGenericUpstream() {
    ON_CALL(*this, appendUpstreamRequest(_, _))
        .WillByDefault(
            Invoke([this](uint64_t stream_id, UpstreamRequestCallbacks* pending_request) {
              requests_[stream_id] = pending_request;
            }));
    ON_CALL(*this, removeUpstreamRequest(_)).WillByDefault(Invoke([this](uint64_t stream_id) {
      requests_.erase(stream_id);
    }));
    ON_CALL(*this, upstreamConnection())
        .WillByDefault(Return(makeOptRef<Network::Connection>(mock_upstream_connection_)));
    ON_CALL(*this, upstreamHost()).WillByDefault(Return(host_description_));
    ON_CALL(*this, clientCodec()).WillByDefault(ReturnRef(mock_client_codec_));
  }

  MOCK_METHOD(void, appendUpstreamRequest,
              (uint64_t stream_id, UpstreamRequestCallbacks* pending_request));
  MOCK_METHOD(void, removeUpstreamRequest, (uint64_t stream_id));
  MOCK_METHOD(Upstream::HostDescriptionConstSharedPtr, upstreamHost, (), (const));
  MOCK_METHOD(ClientCodec&, clientCodec, ());
  MOCK_METHOD(OptRef<Network::Connection>, upstreamConnection, ());
  MOCK_METHOD(void, cleanUp, (bool close_connection));

  std::shared_ptr<Upstream::MockHostDescription> host_description_ =
      std::make_shared<NiceMock<Upstream::MockHostDescription>>();
  NiceMock<Network::MockClientConnection> mock_upstream_connection_;
  absl::flat_hash_map<uint32_t, UpstreamRequestCallbacks*> requests_;
  NiceMock<MockClientCodec> mock_client_codec_{};
};

class MockGenericUpstreamFactory : public GenericUpstreamFactory {
public:
  MOCK_METHOD(GenericUpstreamSharedPtr, createGenericUpstream,
              (Upstream::ThreadLocalCluster&, Upstream::LoadBalancerContext*, Network::Connection&,
               const CodecFactory&, bool),
              (const));
};

class RouterFilterTest : public testing::Test {
public:
  RouterFilterTest() {
    // Common mock calls.
    ON_CALL(mock_filter_callback_, routeEntry()).WillByDefault(Return(&mock_route_entry_));
    ON_CALL(mock_filter_callback_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
    ON_CALL(mock_filter_callback_, activeSpan()).WillByDefault(ReturnRef(active_span_));
    ON_CALL(mock_filter_callback_, codecFactory()).WillByDefault(ReturnRef(mock_codec_factory_));
    ON_CALL(mock_filter_callback_, streamInfo()).WillByDefault(ReturnRef(mock_stream_info_));
    ON_CALL(mock_filter_callback_, connection())
        .WillByDefault(Return(&mock_downstream_connection_));
    ON_CALL(mock_route_entry_, clusterName()).WillByDefault(ReturnRef(cluster_name_));
    factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
        {cluster_name_});

    mock_downstream_connection_.stream_info_.filter_state_ =
        std::make_shared<StreamInfo::FilterStateImpl>(
            StreamInfo::FilterState::LifeSpan::Connection);
  }

  void setup(FrameFlags frame_flags = FrameFlags{}, bool bound_upstream_connection = false) {
    envoy::extensions::filters::network::generic_proxy::router::v3::Router router_config;
    router_config.set_bind_upstream_connection(bound_upstream_connection);
    config_ = std::make_shared<Router::RouterConfig>(router_config);

    filter_ =
        std::make_shared<Router::RouterFilter>(config_, factory_context_, &mock_upstream_factory_);
    filter_->setDecoderFilterCallbacks(mock_filter_callback_);

    request_ = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
    request_->stream_frame_flags_ = frame_flags;
  }

  void cleanUp() {
    filter_->onDestroy();
    filter_.reset();
    request_.reset();
  }

  BoundGenericUpstream* boundUpstreamConnection() {
    return mock_downstream_connection_.stream_info_.filter_state_
        ->getDataMutable<BoundGenericUpstream>("envoy.filters.generic.router");
  }

  void notifyUpstreamFailure(Tcp::ConnectionPool::PoolFailureReason reason) {
    ASSERT(filter_->upstreamRequestsSize() != 0);
    mock_generic_upstream_->requests_.begin()->second->onUpstreamFailure(reason, "");
  }

  void notifyUpstreamSuccess() {
    ASSERT(filter_->upstreamRequestsSize() != 0);
    mock_generic_upstream_->requests_.begin()->second->onUpstreamSuccess();
  }

  UpstreamRequestCallbacks* notifyDecodingSuccess(ResponseHeaderFramePtr&& response,
                                                  absl::optional<StartTime> start_time = {}) {
    ASSERT(filter_->upstreamRequestsSize() != 0);
    auto upstream_request = mock_generic_upstream_->requests_.begin()->second;
    upstream_request->onDecodingSuccess(std::move(response), start_time);
    return upstream_request;
  }

  UpstreamRequestCallbacks* notifyDecodingSuccess(ResponseCommonFramePtr&& response) {
    ASSERT(filter_->upstreamRequestsSize() != 0);
    auto upstream_request = mock_generic_upstream_->requests_.begin()->second;
    upstream_request->onDecodingSuccess(std::move(response));
    return upstream_request;
  }

  UpstreamRequestCallbacks* notifyDecodingFailure(absl::string_view reason) {
    ASSERT(filter_->upstreamRequestsSize() != 0);
    auto upstream_request = mock_generic_upstream_->requests_.begin()->second;
    upstream_request->onDecodingFailure(reason);
    return upstream_request;
  }

  UpstreamRequestCallbacks* notifyConnectionClose(Network::ConnectionEvent event) {
    ASSERT(filter_->upstreamRequestsSize() != 0);
    auto upstream_request = mock_generic_upstream_->requests_.begin()->second;
    upstream_request->onConnectionClose(event);
    return upstream_request;
  }

  void expectNewUpstreamRequest(bool with_tracing = false) {
    if (with_tracing) {
      EXPECT_CALL(mock_filter_callback_, tracingConfig())
          .WillOnce(Return(OptRef<const Tracing::Config>{tracing_config_}));
      EXPECT_CALL(tracing_config_, spawnUpstreamSpan()).WillOnce(Return(true));
      EXPECT_CALL(active_span_, spawnChild_(_, "router observability_name egress", _))
          .WillOnce(Invoke([this](const Tracing::Config&, const std::string&, SystemTime) {
            child_span_ = new NiceMock<Tracing::MockSpan>();
            return child_span_;
          }));
    } else {
      EXPECT_CALL(mock_filter_callback_, tracingConfig())
          .WillOnce(Return(OptRef<const Tracing::Config>{}));
    }

    EXPECT_CALL(mock_upstream_factory_, createGenericUpstream(_, _, _, _, _))
        .WillOnce(Return(mock_generic_upstream_));
    EXPECT_CALL(*mock_generic_upstream_, appendUpstreamRequest(_, _));
  }

  void expectInjectContextToUpstreamRequest() { EXPECT_CALL(*child_span_, injectContext(_, _)); }
  void expectFinalizeUpstreamSpanAny() {
    EXPECT_CALL(*child_span_, setTag(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*child_span_, finishSpan());
  }
  void expectFinalizeUpstreamSpanWithError() {
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().UpstreamAddress, _));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().PeerAddress, _));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Error, "true"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ErrorReason, _));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Component, "proxy"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ResponseFlags, "-"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().UpstreamCluster, "fake_cluster"));
    EXPECT_CALL(*child_span_,
                setTag(Tracing::Tags::get().UpstreamClusterName, "observability_name"));
    EXPECT_CALL(*child_span_, finishSpan());
  }

  /**
   * Kick off a new upstream request.
   */
  void kickOffNewUpstreamRequest(bool with_tracing = false) {
    expectNewUpstreamRequest(with_tracing);
    EXPECT_EQ(filter_->decodeHeaderFrame(*request_), HeaderFilterStatus::StopIteration);
    EXPECT_EQ(1, filter_->upstreamRequestsSize());
  }

  void verifyMetadataMatchCriteria() {
    ProtobufWkt::Struct request_struct;
    ProtobufWkt::Value val;

    // Populate metadata like StreamInfo.setDynamicMetadata() would.
    auto& fields_map = *request_struct.mutable_fields();
    val.set_string_value("v3.1");
    fields_map["version"] = val;
    val.set_string_value("devel");
    fields_map["stage"] = val;
    val.set_string_value("1");
    fields_map["xkey_in_request"] = val;
    (*mock_stream_info_.metadata_
          .mutable_filter_metadata())[Envoy::Config::MetadataFilters::get().ENVOY_LB] =
        request_struct;

    auto match_criteria = filter_->metadataMatchCriteria();
    auto match = match_criteria->metadataMatchCriteria();

    EXPECT_EQ(match.size(), 3);
    auto it = match.begin();

    // Note: metadataMatchCriteria() keeps its entries sorted, so the order for checks
    // below matters.

    EXPECT_EQ((*it)->name(), "stage");
    EXPECT_EQ((*it)->value().value().string_value(), "devel");
    it++;

    EXPECT_EQ((*it)->name(), "version");
    EXPECT_EQ((*it)->value().value().string_value(), "v3.1");
    it++;

    EXPECT_EQ((*it)->name(), "xkey_in_request");
    EXPECT_EQ((*it)->value().value().string_value(), "1");

    EXPECT_EQ(match_criteria, filter_->metadataMatchCriteria());
  }

  void expectResponseTimerCreate() {
    response_timeout_ = new Envoy::Event::MockTimer(&dispatcher_);
    EXPECT_CALL(*response_timeout_, enableTimer(_, _));
    EXPECT_CALL(*response_timeout_, disableTimer());
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Envoy::Event::MockDispatcher> dispatcher_;
  const std::string cluster_name_ = "cluster_0";

  NiceMock<MockDecoderFilterCallback> mock_filter_callback_;
  NiceMock<MockCodecFactory> mock_codec_factory_;
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info_;

  NiceMock<Network::MockServerConnection> mock_downstream_connection_;

  NiceMock<MockGenericUpstreamFactory> mock_upstream_factory_;
  std::shared_ptr<MockGenericUpstream> mock_generic_upstream_ =
      std::make_shared<NiceMock<MockGenericUpstream>>();

  ClientCodecCallbacks* client_cb_{};

  NiceMock<MockRouteEntry> mock_route_entry_;

  std::shared_ptr<Router::RouterConfig> config_;

  std::shared_ptr<Router::RouterFilter> filter_;
  std::unique_ptr<FakeStreamCodecFactory::FakeRequest> request_;

  Envoy::Event::MockTimer* response_timeout_{};

  NiceMock<Tracing::MockConfig> tracing_config_;
  NiceMock<Tracing::MockSpan> active_span_;
  NiceMock<Tracing::MockSpan>* child_span_{};
  uint32_t creating_connection_{};
};

TEST_F(RouterFilterTest, DecodeHeaderFrameAndNoRouteEntry) {
  setup();

  EXPECT_CALL(mock_filter_callback_, routeEntry()).WillOnce(Return(nullptr));
  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(status.message(), "route_not_found");
      }));

  EXPECT_EQ(filter_->decodeHeaderFrame(*request_), HeaderFilterStatus::StopIteration);
  cleanUp();

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, NoUpstreamCluster) {
  setup();

  EXPECT_CALL(mock_filter_callback_, routeEntry()).WillOnce(Return(&mock_route_entry_));

  const std::string cluster_name = "cluster_1";
  EXPECT_CALL(mock_route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name));

  // No upstream cluster.
  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(status.message(), "cluster_not_found");
      }));

  filter_->decodeHeaderFrame(*request_);
  cleanUp();

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamClusterMaintainMode) {
  setup();

  EXPECT_CALL(mock_filter_callback_, routeEntry()).WillOnce(Return(&mock_route_entry_));

  const std::string cluster_name = "cluster_0";

  EXPECT_CALL(mock_route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name));

  factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
      {cluster_name});

  // Maintain mode.
  EXPECT_CALL(*factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .cluster_.info_,
              maintenanceMode())
      .WillOnce(Return(true));
  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(status.message(), "cluster_maintain_mode");
      }));

  filter_->decodeHeaderFrame(*request_);
  cleanUp();

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamClusterNoHealthyUpstream) {
  setup();

  EXPECT_CALL(mock_filter_callback_, routeEntry()).WillOnce(Return(&mock_route_entry_));

  const std::string cluster_name = "cluster_0";

  EXPECT_CALL(mock_route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name));

  factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
      {cluster_name});

  // No valid upstream.
  EXPECT_CALL(mock_upstream_factory_, createGenericUpstream(_, _, _, _, _))
      .WillOnce(Return(nullptr));

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(status.message(), "no_healthy_upstream");
      }));

  filter_->decodeHeaderFrame(*request_);

  cleanUp();

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, KickOffNormalUpstreamRequest) {
  setup();
  kickOffNewUpstreamRequest();

  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));

  cleanUp();

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, KickOffNormalUpstreamRequestAndTimeout) {
  setup();

  mock_route_entry_.timeout_ = std::chrono::milliseconds(1000);
  expectResponseTimerCreate();

  kickOffNewUpstreamRequest();

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([this](Status status, absl::string_view, ResponseUpdateFunction) {
        // All pending requests will be cleaned up.
        EXPECT_EQ(0, filter_->upstreamRequestsSize());
        EXPECT_EQ(status.message(), "timeout");
      }));
  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));

  response_timeout_->invokeCallback();

  cleanUp();

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestResetBeforePoolCallback) {
  setup();
  kickOffNewUpstreamRequest(true);
  expectFinalizeUpstreamSpanWithError();

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([this](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(0, filter_->upstreamRequestsSize());
        EXPECT_EQ(status.message(), "local_reset");
      }));
  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));

  auto* upstream_request = notifyConnectionClose(Network::ConnectionEvent::LocalClose);
  EXPECT_EQ(0, filter_->upstreamRequestsSize());

  // Calling resetStream() again should do nothing.
  dynamic_cast<UpstreamRequest*>(upstream_request)->resetStream(StreamResetReason::LocalReset, {});

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolFailureConnctionOverflow) {
  setup();
  kickOffNewUpstreamRequest(true);
  expectFinalizeUpstreamSpanWithError();

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(status.message(), "overflow");
      }));
  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));

  notifyUpstreamFailure(ConnectionPool::PoolFailureReason::Overflow);

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolFailureConnctionTimeout) {
  setup();
  kickOffNewUpstreamRequest(true);
  expectFinalizeUpstreamSpanWithError();

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(status.message(), "connection_failure");
      }));
  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));

  notifyUpstreamFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  EXPECT_EQ(0, filter_->upstreamRequestsSize());

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolFailureConnctionTimeoutAndWithRetryNoBound) {
  setup();
  RetryPolicy retry_policy{2};
  EXPECT_CALL(mock_route_entry_, retryPolicy()).WillRepeatedly(ReturnRef(retry_policy));

  kickOffNewUpstreamRequest(true);
  expectFinalizeUpstreamSpanWithError();

  // Retry, expect new upstream request to be kicked off.
  expectNewUpstreamRequest();

  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));

  notifyUpstreamFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  // Retry.
  EXPECT_EQ(1, filter_->upstreamRequestsSize());

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(status.message(), "connection_failure");
      }));

  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));

  notifyUpstreamFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  EXPECT_EQ(0, filter_->upstreamRequestsSize());

  cleanUp();
  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolFailureConnctionTimeoutAndWithRetryWithBound) {
  setup({}, true);
  RetryPolicy retry_policy{2};
  EXPECT_CALL(mock_route_entry_, retryPolicy()).WillRepeatedly(ReturnRef(retry_policy));

  kickOffNewUpstreamRequest(true);
  expectFinalizeUpstreamSpanWithError();

  // No try if upstream connection is bound to downstream connection.
  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(status.message(), "connection_failure");
      }));

  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));

  notifyUpstreamFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  // No retry.
  EXPECT_EQ(0, filter_->upstreamRequestsSize());

  cleanUp();
  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyAndExpectNoResponse) {
  setup(FrameFlags(0, FrameFlags::FLAG_END_STREAM | FrameFlags::FLAG_ONE_WAY));
  kickOffNewUpstreamRequest(true);

  EXPECT_CALL(mock_filter_callback_, completeDirectly()).WillOnce(Invoke([this]() -> void {
    EXPECT_EQ(0, filter_->upstreamRequestsSize());
  }));

  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(false));

  EXPECT_CALL(mock_generic_upstream_->mock_client_codec_, encode(_, _));

  expectInjectContextToUpstreamRequest();
  expectFinalizeUpstreamSpanAny();

  notifyUpstreamSuccess();

  EXPECT_EQ(0, filter_->upstreamRequestsSize());

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyButConnectionErrorBeforeResponse) {
  setup();
  kickOffNewUpstreamRequest();

  EXPECT_CALL(mock_generic_upstream_->mock_client_codec_, encode(_, _));

  notifyUpstreamSuccess();

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([this](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(0, filter_->upstreamRequestsSize());
        EXPECT_EQ(status.message(), "local_reset");
      }));

  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));

  // Mock connection close event.
  auto upstream_request = notifyConnectionClose(Network::ConnectionEvent::LocalClose);
  // Calling onConnectionClose() again should do nothing.
  upstream_request->onConnectionClose(Network::ConnectionEvent::LocalClose);

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyButConnectionTerminationBeforeResponse) {
  setup();
  kickOffNewUpstreamRequest();

  EXPECT_CALL(mock_generic_upstream_->mock_client_codec_, encode(_, _));

  notifyUpstreamSuccess();

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([this](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(0, filter_->upstreamRequestsSize());
        EXPECT_EQ(status.message(), "connection_termination");
      }));

  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));

  // Mock connection close event.
  notifyConnectionClose(Network::ConnectionEvent::RemoteClose);

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyButStreamDestroyBeforeResponse) {
  setup();
  kickOffNewUpstreamRequest();

  EXPECT_CALL(mock_generic_upstream_->mock_client_codec_, encode(_, _));

  notifyUpstreamSuccess();

  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));

  filter_->onDestroy();
  // Do nothing for the second call.
  filter_->onDestroy();

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyAndResponse) {
  setup();
  kickOffNewUpstreamRequest(true);

  EXPECT_CALL(mock_generic_upstream_->mock_client_codec_, encode(_, _))
      .WillOnce(Invoke([this](const StreamFrame&, EncodingContext& ctx) -> EncodingResult {
        EXPECT_EQ(ctx.routeEntry().ptr(), &mock_route_entry_);
        return 0;
      }));

  expectInjectContextToUpstreamRequest();

  notifyUpstreamSuccess();

  EXPECT_CALL(mock_filter_callback_, onResponseHeaderFrame(_)).WillOnce(Invoke([this](ResponsePtr) {
    // When the response is sent to callback, the upstream request should be removed.
    EXPECT_EQ(0, filter_->upstreamRequestsSize());
  }));
  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(false));
  expectFinalizeUpstreamSpanAny();

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  notifyDecodingSuccess(std::move(response), {});

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyButDecodeFailureAfterResponse) {
  setup();
  kickOffNewUpstreamRequest(false);

  EXPECT_CALL(mock_generic_upstream_->mock_client_codec_, encode(_, _));
  notifyUpstreamSuccess();

  EXPECT_CALL(mock_filter_callback_, onResponseHeaderFrame(_)).WillOnce(Invoke([this](ResponsePtr) {
    // When the response is sent to callback, the upstream request should be removed.
    EXPECT_EQ(0, filter_->upstreamRequestsSize());
  }));
  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(false));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  auto upstream_request = notifyDecodingSuccess(std::move(response), {});

  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));
  upstream_request->onDecodingFailure("decode-failure");

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyAndResponseWithStartTime) {
  setup();
  kickOffNewUpstreamRequest(true);

  EXPECT_CALL(mock_generic_upstream_->mock_client_codec_, encode(_, _));

  expectInjectContextToUpstreamRequest();

  notifyUpstreamSuccess();

  expectFinalizeUpstreamSpanAny();
  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(false));

  EXPECT_CALL(mock_filter_callback_, onResponseHeaderFrame(_)).WillOnce(Invoke([this](ResponsePtr) {
    // When the response is sent to callback, the upstream request should be removed.
    EXPECT_EQ(0, filter_->upstreamRequestsSize());
  }));

  StartTime start_time;
  start_time.start_time =
      std::chrono::time_point<std::chrono::system_clock>(std::chrono::milliseconds(111111111));
  start_time.start_time_monotonic =
      std::chrono::time_point<std::chrono::steady_clock>(std::chrono::milliseconds(222222222));
  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  notifyDecodingSuccess(std::move(response), start_time);

  EXPECT_EQ(222222222LL, std::chrono::duration_cast<std::chrono::milliseconds>(
                             mock_stream_info_.upstream_info_->upstreamTiming()
                                 .first_upstream_rx_byte_received_->time_since_epoch())
                             .count());

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyAndResponseAndTimeout) {
  setup();

  mock_route_entry_.timeout_ = std::chrono::milliseconds(1000);
  expectResponseTimerCreate();

  kickOffNewUpstreamRequest(true);

  EXPECT_CALL(mock_generic_upstream_->mock_client_codec_, encode(_, _));

  expectInjectContextToUpstreamRequest();

  notifyUpstreamSuccess();

  expectFinalizeUpstreamSpanAny();
  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(false));

  EXPECT_CALL(mock_filter_callback_, onResponseHeaderFrame(_)).WillOnce(Invoke([this](ResponsePtr) {
    // When the response is sent to callback, the upstream request should be removed.
    EXPECT_EQ(0, filter_->upstreamRequestsSize());
  }));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  notifyDecodingSuccess(std::move(response));

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyButEncodingFailure) {
  // There are multiple frames in the request.
  setup(FrameFlags(0, FrameFlags::FLAG_EMPTY));
  kickOffNewUpstreamRequest(true);

  auto frame_1 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  frame_1->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);
  EXPECT_EQ(CommonFilterStatus::StopIteration, filter_->decodeCommonFrame(*frame_1));
  // This only store the frame and does nothing else because the pool is not ready yet.
  filter_->onRequestCommonFrame(std::move(frame_1));

  // End stream is set to true by default.
  auto frame_2 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  EXPECT_EQ(CommonFilterStatus::StopIteration, filter_->decodeCommonFrame(*frame_2));
  // This only store the frame and does nothing else because the pool is not ready yet.
  filter_->onRequestCommonFrame(std::move(frame_2));

  testing::InSequence s;
  expectInjectContextToUpstreamRequest();
  // Encode header frame success.
  EXPECT_CALL(mock_generic_upstream_->mock_client_codec_, encode(_, _))
      .WillOnce(Return(EncodingResult(0)));
  // Encode common frame failure.
  EXPECT_CALL(mock_generic_upstream_->mock_client_codec_, encode(_, _))
      .WillOnce(Return(absl::UnknownError("encode-failure")));

  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));
  expectFinalizeUpstreamSpanAny();

  // This will trigger frames to be encoded.
  notifyUpstreamSuccess();

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyAndResponseWithMultipleFrames) {
  // There are multiple frames in the request.
  setup(FrameFlags(0, FrameFlags::FLAG_EMPTY));
  kickOffNewUpstreamRequest(true);

  auto frame_1 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  frame_1->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);
  EXPECT_EQ(CommonFilterStatus::StopIteration, filter_->decodeCommonFrame(*frame_1));

  // This only store the frame and does nothing else because the pool is not ready yet.
  filter_->onRequestCommonFrame(std::move(frame_1));

  EXPECT_CALL(mock_generic_upstream_->mock_client_codec_, encode(_, _)).Times(2);

  expectInjectContextToUpstreamRequest();

  // This will trigger two frames to be sent.
  notifyUpstreamSuccess();

  EXPECT_CALL(mock_generic_upstream_->mock_client_codec_, encode(_, _));

  // End stream is set to true by default.
  auto frame_2 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  EXPECT_EQ(CommonFilterStatus::StopIteration, filter_->decodeCommonFrame(*frame_2));

  // This will trigger the last frame to be sent directly because connection is ready and other
  // frames are already sent.
  filter_->onRequestCommonFrame(std::move(frame_2));

  expectFinalizeUpstreamSpanAny();
  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(false));

  EXPECT_CALL(mock_filter_callback_, onResponseHeaderFrame(_));
  EXPECT_CALL(mock_filter_callback_, onResponseCommonFrame(_))
      .Times(2)
      .WillRepeatedly(Invoke([this](ResponseCommonFramePtr frame) {
        // When the entire response is sent to callback, the upstream request should be removed.
        if (frame->frameFlags().endStream()) {
          EXPECT_EQ(0, filter_->upstreamRequestsSize());
        } else {
          EXPECT_EQ(1, filter_->upstreamRequestsSize());
        }
      }));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);

  notifyDecodingSuccess(std::move(response));

  auto response_frame_1 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  response_frame_1->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);
  notifyDecodingSuccess(std::move(response_frame_1));

  // End stream is set to true by default.
  auto response_frame_2 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  notifyDecodingSuccess(std::move(response_frame_2));

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest,
       UpstreamRequestPoolReadyAndResponseWithMultipleFramesButWithRepeatedHeaders) {
  setup();
  kickOffNewUpstreamRequest(false);

  EXPECT_CALL(mock_generic_upstream_->mock_client_codec_, encode(_, _));
  notifyUpstreamSuccess();

  // Request complete and response starts.

  EXPECT_CALL(mock_filter_callback_, onResponseHeaderFrame(_));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);
  notifyDecodingSuccess(std::move(response));

  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));

  auto response_2 = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response_2->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);

  notifyDecodingSuccess(std::move(response_2));

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest,
       UpstreamRequestPoolReadyAndResponseWithMultipleFramesButCommonFrameBeforeHeader) {
  setup();
  kickOffNewUpstreamRequest(false);

  EXPECT_CALL(mock_generic_upstream_->mock_client_codec_, encode(_, _));
  notifyUpstreamSuccess();

  // Request complete and response starts.

  auto response = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  response->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);

  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));

  notifyDecodingSuccess(std::move(response));

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyAndResponseWithDrainCloseSetInResponse) {
  setup();
  kickOffNewUpstreamRequest();

  EXPECT_CALL(mock_generic_upstream_->mock_client_codec_, encode(_, _));

  notifyUpstreamSuccess();

  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));

  EXPECT_CALL(mock_filter_callback_, onResponseHeaderFrame(_)).WillOnce(Invoke([this](ResponsePtr) {
    // When the response is sent to callback, the upstream request should be removed.
    EXPECT_EQ(0, filter_->upstreamRequestsSize());
  }));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response->stream_frame_flags_ =
      FrameFlags(0, FrameFlags::FLAG_END_STREAM | FrameFlags::FLAG_DRAIN_CLOSE);
  notifyDecodingSuccess(std::move(response));

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyAndResponseDecodingFailure) {
  setup();
  kickOffNewUpstreamRequest();

  EXPECT_CALL(mock_generic_upstream_->mock_client_codec_, encode(_, _));

  notifyUpstreamSuccess();

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([this](Status status, absl::string_view data, ResponseUpdateFunction) {
        EXPECT_EQ(0, filter_->upstreamRequestsSize());
        EXPECT_TRUE(status.message() == "protocol_error");
        EXPECT_EQ(data, "decoding-failure");
      }));

  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));

  notifyDecodingFailure("decoding-failure");

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, UpstreamRequestPoolReadyAndRequestEncodingFailure) {
  setup();
  kickOffNewUpstreamRequest();

  EXPECT_CALL(mock_generic_upstream_->mock_client_codec_, encode(_, _))
      .WillOnce(Return(EncodingResult(absl::InvalidArgumentError("encoding-failure"))));

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([this](Status status, absl::string_view data, ResponseUpdateFunction) {
        EXPECT_EQ(0, filter_->upstreamRequestsSize());
        EXPECT_TRUE(status.message() == "protocol_error");
        EXPECT_EQ(data, "encoding-failure");
      }));

  EXPECT_CALL(*mock_generic_upstream_, removeUpstreamRequest(_));
  EXPECT_CALL(*mock_generic_upstream_, cleanUp(true));

  notifyUpstreamSuccess();

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(RouterFilterTest, LoadBalancerContextDownstreamConnection) {
  setup();
  EXPECT_CALL(mock_filter_callback_, connection());
  filter_->downstreamConnection();
}

TEST_F(RouterFilterTest, LoadBalancerContextNoMetadataMatchCriteria) {
  setup();

  // No metadata match criteria by default.
  EXPECT_EQ(nullptr, filter_->metadataMatchCriteria());
}

TEST_F(RouterFilterTest, LoadBalancerContextMetadataMatchCriteria) {
  setup();
  verifyMetadataMatchCriteria();
}

} // namespace
} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
