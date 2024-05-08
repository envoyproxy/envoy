#include <memory>

#include "source/common/tracing/common_values.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "contrib/generic_proxy/filters/network/source/router/router.h"
#include "contrib/generic_proxy/filters/network/test/fake_codec.h"
#include "contrib/generic_proxy/filters/network/test/mocks/codec.h"
#include "contrib/generic_proxy/filters/network/test/mocks/filter.h"
#include "contrib/generic_proxy/filters/network/test/mocks/route.h"
#include "gtest/gtest.h"

using testing::ByMove;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Router {
namespace {

#define ONLY_RUN_TEST_WITH_PARAM(param)                                                            \
  if (GetParam() != param) {                                                                       \
    return;                                                                                        \
  }

struct TestParameters {
  bool operator!=(const TestParameters& other) const {
    return with_tracing != other.with_tracing || bind_upstream != other.bind_upstream;
  }

  bool with_tracing{};
  bool bind_upstream{};
};

class RouterFilterTest : public testing::TestWithParam<TestParameters> {
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

    auto parameter = GetParam();

    mock_downstream_connection_.stream_info_.filter_state_ =
        std::make_shared<StreamInfo::FilterStateImpl>(
            StreamInfo::FilterState::LifeSpan::Connection);

    envoy::extensions::filters::network::generic_proxy::router::v3::Router router_config;
    router_config.set_bind_upstream_connection(parameter.bind_upstream);
    config_ = std::make_shared<Router::RouterConfig>(router_config);
    with_tracing_ = parameter.with_tracing;
  }

  void setup(FrameFlags frame_flags = FrameFlags{}) {
    filter_ = std::make_shared<Router::RouterFilter>(config_, factory_context_);
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

  void expectCreateConnection() {
    creating_connection_++;
    // New connection and response decoder will be created for this upstream request.
    auto client_codec = std::make_unique<NiceMock<MockClientCodec>>();
    mock_client_codec_ = client_codec.get();
    EXPECT_CALL(mock_codec_factory_, createClientCodec())
        .WillOnce(Return(ByMove(std::move(client_codec))));
    EXPECT_CALL(*mock_client_codec_, setCodecCallbacks(_))
        .WillOnce(Invoke([this](ClientCodecCallbacks& cb) { client_cb_ = &cb; }));

    EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                    .tcp_conn_pool_,
                newConnection(_));
  }

  void expectCancelConnect() {
    if (creating_connection_ > 0) {
      creating_connection_--;

      // Only cancel the connection if it is owned by the upstream request. If the connection is
      // bound to the downstream connection, then this won't be called.
      if (!config_->bindUpstreamConnection()) {
        EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                        .tcp_conn_pool_.handles_.back(),
                    cancel(_));
      }
    }
  }

  void expectUpstreamConnectionClose() {
    EXPECT_CALL(mock_upstream_connection_, close(Network::ConnectionCloseType::FlushWrite));
  }

  void notifyPoolFailure(Tcp::ConnectionPool::PoolFailureReason reason) {
    if (creating_connection_ > 0) {
      creating_connection_--;

      if (config_->bindUpstreamConnection()) {
        EXPECT_TRUE(!boundUpstreamConnection()->waitingUpstreamRequestsForTest().empty());
        EXPECT_TRUE(boundUpstreamConnection()->waitingResponseRequestsForTest().empty());
        EXPECT_CALL(mock_downstream_connection_, close(Network::ConnectionCloseType::FlushWrite));
      }

      factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
          .poolFailure(reason);

      if (config_->bindUpstreamConnection()) {
        EXPECT_TRUE(boundUpstreamConnection()->waitingUpstreamRequestsForTest().empty());
        EXPECT_TRUE(boundUpstreamConnection()->waitingResponseRequestsForTest().empty());
      }
    }
  }

  void notifyPoolReady(bool encoding_success = true) {
    if (creating_connection_ > 0) {
      creating_connection_--;

      if (config_->bindUpstreamConnection()) {
        EXPECT_TRUE(!boundUpstreamConnection()->waitingUpstreamRequestsForTest().empty());
        EXPECT_TRUE(boundUpstreamConnection()->waitingResponseRequestsForTest().empty());
      }

      if (!encoding_success) {
        EXPECT_CALL(mock_upstream_connection_, write(_, _)).Times(0);
      } else {
        EXPECT_CALL(mock_upstream_connection_, write(_, _)).Times(testing::AtLeast(1));
      }

      factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
          .poolReady(mock_upstream_connection_);

      if (config_->bindUpstreamConnection()) {
        EXPECT_TRUE(boundUpstreamConnection()->waitingUpstreamRequestsForTest().empty());
      }
    }
  }

  void notifyConnectionClose(Network::ConnectionEvent event) {
    ASSERT(!filter_->upstreamRequestsForTest().empty());
    auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

    if (config_->bindUpstreamConnection()) {
      EXPECT_TRUE(boundUpstreamConnection()->waitingUpstreamRequestsForTest().empty());
      EXPECT_TRUE(!boundUpstreamConnection()->waitingResponseRequestsForTest().empty());
      EXPECT_CALL(mock_downstream_connection_, close(Network::ConnectionCloseType::FlushWrite));
    }

    upstream_request->generic_upstream_->onEvent(event);

    if (config_->bindUpstreamConnection()) {
      EXPECT_TRUE(boundUpstreamConnection()->waitingUpstreamRequestsForTest().empty());
      EXPECT_TRUE(boundUpstreamConnection()->waitingResponseRequestsForTest().empty());
    }
  }

  void notifyDecodingSuccess(ResponseHeaderFramePtr&& response,
                             absl::optional<StartTime> start_time = {}) {
    ASSERT(!filter_->upstreamRequestsForTest().empty());

    auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();
    auto stream_frame = std::make_shared<ResponseHeaderFramePtr>(std::move(response));

    EXPECT_CALL(*mock_client_codec_, decode(BufferStringEqual("test_1"), _))
        .WillOnce(Invoke(
            [this, resp = std::move(stream_frame), start_time](Buffer::Instance& buffer, bool) {
              buffer.drain(buffer.length());

              const bool end_stream = (*resp)->frameFlags().endStream();
              int pending_request_size = 0;
              if (config_->bindUpstreamConnection()) {
                pending_request_size =
                    boundUpstreamConnection()->waitingResponseRequestsForTest().size();
              }

              client_cb_->onDecodingSuccess(std::move(*resp), start_time);

              if (config_->bindUpstreamConnection()) {
                EXPECT_EQ(pending_request_size - (end_stream ? 1 : 0),
                          boundUpstreamConnection()->waitingResponseRequestsForTest().size());
              }
            }));

    Buffer::OwnedImpl test_buffer;
    test_buffer.add("test_1");

    upstream_request->generic_upstream_->onUpstreamData(test_buffer, false);
  }
  void notifyDecodingSuccess(ResponseCommonFramePtr&& response) {
    ASSERT(!filter_->upstreamRequestsForTest().empty());

    auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();
    auto stream_frame = std::make_shared<ResponseCommonFramePtr>(std::move(response));

    EXPECT_CALL(*mock_client_codec_, decode(BufferStringEqual("test_1"), _))
        .WillOnce(Invoke([this, resp = std::move(stream_frame)](Buffer::Instance& buffer, bool) {
          buffer.drain(buffer.length());

          const bool end_stream = (*resp)->frameFlags().endStream();
          int pending_request_size = 0;
          if (config_->bindUpstreamConnection()) {
            pending_request_size =
                boundUpstreamConnection()->waitingResponseRequestsForTest().size();
          }

          client_cb_->onDecodingSuccess(std::move(*resp));

          if (config_->bindUpstreamConnection()) {
            EXPECT_EQ(pending_request_size - (end_stream ? 1 : 0),
                      boundUpstreamConnection()->waitingResponseRequestsForTest().size());
          }
        }));

    Buffer::OwnedImpl test_buffer;
    test_buffer.add("test_1");

    upstream_request->generic_upstream_->onUpstreamData(test_buffer, false);
  }

  void notifyDecodingFailure(absl::string_view reason) {
    ASSERT(!filter_->upstreamRequestsForTest().empty());

    auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

    if (config_->bindUpstreamConnection()) {
      // If upstream connection binding is enabled, the downstream connection will be closed
      // when the upstream connection is closed.
      EXPECT_CALL(mock_downstream_connection_, close(Network::ConnectionCloseType::FlushWrite));
    }

    EXPECT_CALL(mock_upstream_connection_, close(Network::ConnectionCloseType::FlushWrite))
        .WillOnce(Invoke([upstream_request](Network::ConnectionCloseType) {
          // Mock clean up closing.
          upstream_request->generic_upstream_->onEvent(Network::ConnectionEvent::LocalClose);
        }));

    EXPECT_CALL(*mock_client_codec_, decode(BufferStringEqual("test_1"), _))
        .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) {
          buffer.drain(buffer.length());
          client_cb_->onDecodingFailure(reason);
        }));

    Buffer::OwnedImpl test_buffer;
    test_buffer.add("test_1");

    upstream_request->generic_upstream_->onUpstreamData(test_buffer, false);
  }

  void expectNewUpstreamRequest() {
    if (boundUpstreamConnection() == nullptr) {
      // Upstream binding is disabled or not set up yet, try to create a new connection.
      expectCreateConnection();
    }

    if (with_tracing_) {
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
  }

  /**
   * Kick off a new upstream request.
   */
  void kickOffNewUpstreamRequest() {
    expectNewUpstreamRequest();
    EXPECT_EQ(filter_->onStreamDecoded(*request_), FilterStatus::StopIteration);
    EXPECT_EQ(1, filter_->upstreamRequestsForTest().size());
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

    auto match = filter_->metadataMatchCriteria()->metadataMatchCriteria();

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
  NiceMock<StreamInfo::MockStreamInfo> mock_stream_info_;

  NiceMock<Network::MockServerConnection> mock_downstream_connection_;
  NiceMock<Network::MockClientConnection> mock_upstream_connection_;

  NiceMock<MockCodecFactory> mock_codec_factory_;

  NiceMock<MockClientCodec>* mock_client_codec_{};

  ClientCodecCallbacks* client_cb_{};

  NiceMock<MockRouteEntry> mock_route_entry_;

  std::shared_ptr<Router::RouterConfig> config_;

  std::shared_ptr<Router::RouterFilter> filter_;
  std::unique_ptr<FakeStreamCodecFactory::FakeRequest> request_;

  Envoy::Event::MockTimer* response_timeout_{};

  NiceMock<Tracing::MockConfig> tracing_config_;
  NiceMock<Tracing::MockSpan> active_span_;
  NiceMock<Tracing::MockSpan>* child_span_{};
  bool with_tracing_{};
  uint32_t creating_connection_{};
};

std::vector<TestParameters> getTestParameters() {
  std::vector<TestParameters> ret;

  ret.push_back({false, false});
  ret.push_back({true, true});

  return ret;
}

std::string testParameterToString(const testing::TestParamInfo<TestParameters>& params) {
  return fmt::format("with_tracing_{}_bind_upstream_{}",
                     params.param.with_tracing ? "true" : "false",
                     params.param.bind_upstream ? "true" : "false");
}

INSTANTIATE_TEST_SUITE_P(GenericRoute, RouterFilterTest, testing::ValuesIn(getTestParameters()),
                         testParameterToString);

TEST_P(RouterFilterTest, OnStreamDecodedAndNoRouteEntry) {
  setup();

  EXPECT_CALL(mock_filter_callback_, routeEntry()).WillOnce(Return(nullptr));
  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(status.message(), "route_not_found");
      }));

  EXPECT_EQ(filter_->onStreamDecoded(*request_), FilterStatus::StopIteration);
  cleanUp();

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, NoUpstreamCluster) {
  setup();

  EXPECT_CALL(mock_filter_callback_, routeEntry()).WillOnce(Return(&mock_route_entry_));

  const std::string cluster_name = "cluster_1";
  EXPECT_CALL(mock_route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name));

  // No upstream cluster.
  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(status.message(), "cluster_not_found");
      }));

  filter_->onStreamDecoded(*request_);
  cleanUp();

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, UpstreamClusterMaintainMode) {
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

  filter_->onStreamDecoded(*request_);
  cleanUp();

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, UpstreamClusterNoHealthyUpstream) {
  setup();

  EXPECT_CALL(mock_filter_callback_, routeEntry()).WillOnce(Return(&mock_route_entry_));

  const std::string cluster_name = "cluster_0";

  EXPECT_CALL(mock_route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name));

  factory_context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
      {cluster_name});

  // No conn pool.
  EXPECT_CALL(factory_context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _))
      .WillOnce(Return(absl::nullopt));

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(status.message(), "no_healthy_upstream");
      }));

  filter_->onStreamDecoded(*request_);

  cleanUp();

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, KickOffNormalUpstreamRequest) {
  setup();
  kickOffNewUpstreamRequest();

  cleanUp();

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, KickOffNormalUpstreamRequestAndTimeout) {
  setup();

  mock_route_entry_.timeout_ = std::chrono::milliseconds(1000);
  expectResponseTimerCreate();

  kickOffNewUpstreamRequest();

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([this](Status status, absl::string_view, ResponseUpdateFunction) {
        // All pending requests will be cleaned up.
        EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
        EXPECT_EQ(status.message(), "timeout");
      }));
  response_timeout_->invokeCallback();

  cleanUp();

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, UpstreamRequestResetBeforePoolCallback) {
  setup();
  kickOffNewUpstreamRequest();

  if (with_tracing_) {
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().UpstreamAddress, _));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().PeerAddress, _));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Error, "true"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ErrorReason, "local_reset"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Component, "proxy"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ResponseFlags, "-"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().UpstreamCluster, "fake_cluster"));
    EXPECT_CALL(*child_span_,
                setTag(Tracing::Tags::get().UpstreamClusterName, "observability_name"));

    EXPECT_CALL(*child_span_, finishSpan());
  }

  expectCancelConnect();

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([this](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
        EXPECT_EQ(status.message(), "local_reset");
      }));

  filter_->upstreamRequestsForTest().begin()->get()->resetStream(StreamResetReason::LocalReset, {});
  EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, UpstreamRequestPoolFailureConnctionOverflow) {
  setup();
  kickOffNewUpstreamRequest();

  if (with_tracing_) {
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().UpstreamAddress, _));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().PeerAddress, _));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Error, "true"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ErrorReason, "overflow"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Component, "proxy"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ResponseFlags, "-"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().UpstreamCluster, "fake_cluster"));
    EXPECT_CALL(*child_span_,
                setTag(Tracing::Tags::get().UpstreamClusterName, "observability_name"));
    EXPECT_CALL(*child_span_, finishSpan());
  }

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(status.message(), "overflow");
      }));

  notifyPoolFailure(ConnectionPool::PoolFailureReason::Overflow);

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, UpstreamRequestPoolFailureConnctionTimeout) {
  setup();
  kickOffNewUpstreamRequest();

  if (with_tracing_) {
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().UpstreamAddress, _));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().PeerAddress, _));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Error, "true"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ErrorReason, "connection_failure"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Component, "proxy"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ResponseFlags, "-"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().UpstreamCluster, "fake_cluster"));
    EXPECT_CALL(*child_span_,
                setTag(Tracing::Tags::get().UpstreamClusterName, "observability_name"));
    EXPECT_CALL(*child_span_, finishSpan());
  }

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(status.message(), "connection_failure");
      }));

  notifyPoolFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, UpstreamRequestPoolFailureConnctionTimeoutAndWithRetry) {
  setup();
  RetryPolicy retry_policy{2};
  EXPECT_CALL(mock_route_entry_, retryPolicy()).WillRepeatedly(ReturnRef(retry_policy));

  kickOffNewUpstreamRequest();

  if (with_tracing_) {
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().UpstreamAddress, _));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().PeerAddress, _));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Error, "true"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ErrorReason, "connection_failure"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().Component, "proxy"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().ResponseFlags, "-"));
    EXPECT_CALL(*child_span_, setTag(Tracing::Tags::get().UpstreamCluster, "fake_cluster"));
    EXPECT_CALL(*child_span_,
                setTag(Tracing::Tags::get().UpstreamClusterName, "observability_name"));
    EXPECT_CALL(*child_span_, finishSpan());
  }

  const bool bound_upstream_connection = GetParam().bind_upstream;
  if (bound_upstream_connection) {
    // No try if upstream connection is bound to downstream connection.
    EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
        .WillOnce(Invoke([](Status status, absl::string_view, ResponseUpdateFunction) {
          EXPECT_EQ(status.message(), "connection_failure");
        }));
  } else {
    // Retry, expect new upstream request to be kicked off.
    expectNewUpstreamRequest();
  }

  notifyPoolFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  if (bound_upstream_connection) {
    // No retry.
    EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
  } else {
    // Retry.
    EXPECT_EQ(1, filter_->upstreamRequestsForTest().size());

    EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
        .WillOnce(Invoke([](Status status, absl::string_view, ResponseUpdateFunction) {
          EXPECT_EQ(status.message(), "connection_failure");
        }));

    notifyPoolFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
    EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
  }

  cleanUp();
  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, UpstreamRequestPoolReadyAndExpectNoResponse) {
  setup(FrameFlags(StreamFlags(0, true, false, false), true));
  kickOffNewUpstreamRequest();

  EXPECT_CALL(mock_filter_callback_, completeDirectly()).WillOnce(Invoke([this]() -> void {
    EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
  }));

  EXPECT_CALL(*mock_client_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingCallbacks& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect no response.
        callback.onEncodingSuccess(buffer, true);
      }));

  if (with_tracing_) {
    // Request complete directly.
    EXPECT_CALL(*child_span_, injectContext(_, _));
    EXPECT_CALL(*child_span_, setTag(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*child_span_, finishSpan());
  }

  notifyPoolReady();
  EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, UpstreamRequestPoolReadyButConnectionErrorBeforeResponse) {
  setup();
  kickOffNewUpstreamRequest();

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  EXPECT_CALL(*mock_client_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingCallbacks& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect response.
        callback.onEncodingSuccess(buffer, true);
      }));

  notifyPoolReady();

  EXPECT_NE(nullptr, upstream_request->generic_upstream_->connection().ptr());

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([this](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
        EXPECT_EQ(status.message(), "local_reset");
      }));

  // Mock connection close event.
  notifyConnectionClose(Network::ConnectionEvent::LocalClose);

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, UpstreamRequestPoolReadyButConnectionTerminationBeforeResponse) {
  setup();
  kickOffNewUpstreamRequest();

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  EXPECT_CALL(*mock_client_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingCallbacks& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect response.
        callback.onEncodingSuccess(buffer, true);
      }));

  notifyPoolReady();

  EXPECT_NE(nullptr, upstream_request->generic_upstream_->connection().ptr());

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([this](Status status, absl::string_view, ResponseUpdateFunction) {
        EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
        EXPECT_EQ(status.message(), "connection_termination");
      }));

  // Mock connection close event.
  notifyConnectionClose(Network::ConnectionEvent::RemoteClose);

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, UpstreamRequestPoolReadyButStreamDestroyBeforeResponse) {
  setup();
  kickOffNewUpstreamRequest();

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  EXPECT_CALL(*mock_client_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingCallbacks& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect response.
        callback.onEncodingSuccess(buffer, true);
      }));

  notifyPoolReady();

  EXPECT_NE(nullptr, upstream_request->generic_upstream_->connection().ptr());

  expectUpstreamConnectionClose();

  filter_->onDestroy();
  // Do nothing for the second call.
  filter_->onDestroy();

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, UpstreamRequestPoolReadyAndResponse) {
  setup();
  kickOffNewUpstreamRequest();

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  EXPECT_CALL(*mock_client_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingCallbacks& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect response.
        callback.onEncodingSuccess(buffer, true);
      }));

  if (with_tracing_) {
    // Inject tracing context.
    EXPECT_CALL(*child_span_, injectContext(_, _));
  }

  notifyPoolReady();

  EXPECT_NE(nullptr, upstream_request->generic_upstream_->connection().ptr());

  if (with_tracing_) {
    EXPECT_CALL(*child_span_, setTag(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*child_span_, finishSpan());
  }

  EXPECT_CALL(mock_filter_callback_, onResponseStart(_)).WillOnce(Invoke([this](ResponsePtr) {
    // When the response is sent to callback, the upstream request should be removed.
    EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
  }));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  notifyDecodingSuccess(std::move(response));

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, UpstreamRequestPoolReadyAndResponseWithStartTime) {
  setup();
  kickOffNewUpstreamRequest();

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  EXPECT_CALL(*mock_client_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingCallbacks& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect response.
        callback.onEncodingSuccess(buffer, true);
      }));

  if (with_tracing_) {
    // Inject tracing context.
    EXPECT_CALL(*child_span_, injectContext(_, _));
  }

  notifyPoolReady();

  EXPECT_NE(nullptr, upstream_request->generic_upstream_->connection().ptr());

  if (with_tracing_) {
    EXPECT_CALL(*child_span_, setTag(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*child_span_, finishSpan());
  }

  EXPECT_CALL(mock_filter_callback_, onResponseStart(_)).WillOnce(Invoke([this](ResponsePtr) {
    // When the response is sent to callback, the upstream request should be removed.
    EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
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

TEST_P(RouterFilterTest, UpstreamRequestPoolReadyAndResponseAndTimeout) {
  setup();

  mock_route_entry_.timeout_ = std::chrono::milliseconds(1000);
  expectResponseTimerCreate();

  kickOffNewUpstreamRequest();

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  EXPECT_CALL(*mock_client_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingCallbacks& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect response.
        callback.onEncodingSuccess(buffer, true);
      }));

  if (with_tracing_) {
    // Inject tracing context.
    EXPECT_CALL(*child_span_, injectContext(_, _));
  }

  notifyPoolReady();

  EXPECT_NE(nullptr, upstream_request->generic_upstream_->connection().ptr());

  if (with_tracing_) {
    EXPECT_CALL(*child_span_, setTag(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*child_span_, finishSpan());
  }

  EXPECT_CALL(mock_filter_callback_, onResponseStart(_)).WillOnce(Invoke([this](ResponsePtr) {
    // When the response is sent to callback, the upstream request should be removed.
    EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
  }));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  notifyDecodingSuccess(std::move(response));

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, UpstreamRequestPoolReadyAndResponseAndMultipleRequest) {
  for (size_t i = 0; i < 5; i++) {
    setup(FrameFlags(StreamFlags(i)));

    // Expect immediate encoding.
    if (GetParam().bind_upstream && i > 0) {
      EXPECT_CALL(*mock_client_codec_, encode(_, _))
          .WillOnce(Invoke([&](const StreamFrame&, EncodingCallbacks& callback) -> void {
            Buffer::OwnedImpl buffer;
            buffer.add("hello");
            // Expect response.
            callback.onEncodingSuccess(buffer, true);
          }));
    }

    kickOffNewUpstreamRequest();

    // Expect encoding after pool ready.
    if (!GetParam().bind_upstream || i == 0) {
      EXPECT_CALL(*mock_client_codec_, encode(_, _))
          .WillOnce(Invoke([&](const StreamFrame&, EncodingCallbacks& callback) -> void {
            Buffer::OwnedImpl buffer;
            buffer.add("hello");
            // Expect response.
            callback.onEncodingSuccess(buffer, true);
          }));
    }

    auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

    notifyPoolReady();

    EXPECT_NE(nullptr, upstream_request->generic_upstream_->connection().ptr());

    EXPECT_CALL(mock_filter_callback_, onResponseStart(_)).WillOnce(Invoke([this](ResponsePtr) {
      // When the response is sent to callback, the upstream request should be removed.
      EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
    }));

    auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
    response->stream_frame_flags_ = FrameFlags(StreamFlags(i));
    notifyDecodingSuccess(std::move(response));

    cleanUp();
  }
  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, UpstreamRequestPoolReadyAndResponseWithMultipleFrames) {
  // There are multiple frames in the request.
  setup(FrameFlags(StreamFlags(0, false, false, true), /*end_stream*/ false));
  kickOffNewUpstreamRequest();

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  if (with_tracing_) {
    // Inject tracing context.
    EXPECT_CALL(*child_span_, injectContext(_, _));
  }

  auto frame_1 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  frame_1->stream_frame_flags_ = FrameFlags(StreamFlags(0, false, false, true), false);

  // This only store the frame and does nothing else because the pool is not ready yet.
  filter_->onRequestCommonFrame(std::move(frame_1));

  EXPECT_CALL(*mock_client_codec_, encode(_, _))
      .Times(2)
      .WillRepeatedly(Invoke([&](const StreamFrame&, EncodingCallbacks& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect response.
        callback.onEncodingSuccess(buffer, false);
      }));

  // This will trigger two frames to be sent.
  notifyPoolReady();
  EXPECT_NE(nullptr, upstream_request->generic_upstream_->connection().ptr());

  EXPECT_CALL(*mock_client_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingCallbacks& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect response.
        callback.onEncodingSuccess(buffer, true);
      }));

  // End stream is set to true by default.
  auto frame_2 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  // This will trigger the last frame to be sent directly because connection is ready and other
  // frames are already sent.
  filter_->onRequestCommonFrame(std::move(frame_2));

  if (with_tracing_) {
    EXPECT_CALL(*child_span_, setTag(_, _)).Times(testing::AnyNumber());
    EXPECT_CALL(*child_span_, finishSpan());
  }

  EXPECT_CALL(mock_filter_callback_, onResponseStart(_));
  EXPECT_CALL(mock_filter_callback_, onResponseFrame(_))
      .Times(2)
      .WillRepeatedly(Invoke([this](ResponseCommonFramePtr frame) {
        // When the entire response is sent to callback, the upstream request should be removed.
        if (frame->frameFlags().endStream()) {
          EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
        } else {
          EXPECT_EQ(1, filter_->upstreamRequestsForTest().size());
        }
      }));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response->stream_frame_flags_ = FrameFlags(StreamFlags(0, false, false, false), false);
  notifyDecodingSuccess(std::move(response));

  auto response_frame_1 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  response_frame_1->stream_frame_flags_ = FrameFlags(StreamFlags(0, false, false, false), false);
  notifyDecodingSuccess(std::move(response_frame_1));

  // End stream is set to true by default.
  auto response_frame_2 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  notifyDecodingSuccess(std::move(response_frame_2));

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, UpstreamRequestPoolReadyAndResponseWithDrainCloseSetInResponse) {
  setup();
  kickOffNewUpstreamRequest();

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  EXPECT_CALL(*mock_client_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingCallbacks& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect response.
        callback.onEncodingSuccess(buffer, true);
      }));

  if (with_tracing_) {
    // Inject tracing context.
    EXPECT_CALL(*child_span_, injectContext(_, _));
  }

  notifyPoolReady();

  EXPECT_NE(nullptr, upstream_request->generic_upstream_->connection().ptr());

  EXPECT_CALL(mock_filter_callback_, onResponseStart(_)).WillOnce(Invoke([this](ResponsePtr) {
    // When the response is sent to callback, the upstream request should be removed.
    EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
  }));

  EXPECT_CALL(mock_upstream_connection_, close(Network::ConnectionCloseType::FlushWrite));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response->stream_frame_flags_ = FrameFlags(StreamFlags(0, false, true, false), true);
  notifyDecodingSuccess(std::move(response));

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, UpstreamRequestPoolReadyAndResponseDecodingFailure) {
  setup();
  kickOffNewUpstreamRequest();

  auto upstream_request = filter_->upstreamRequestsForTest().begin()->get();

  EXPECT_CALL(*mock_client_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingCallbacks& callback) -> void {
        Buffer::OwnedImpl buffer;
        buffer.add("hello");
        // Expect response.
        callback.onEncodingSuccess(buffer, true);
      }));

  notifyPoolReady();

  EXPECT_NE(nullptr, upstream_request->generic_upstream_->connection().ptr());

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([this](Status status, absl::string_view data, ResponseUpdateFunction) {
        EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
        EXPECT_TRUE(status.message() == "protocol_error");
        EXPECT_EQ(data, "decoding-failure");
      }));

  notifyDecodingFailure("decoding-failure");

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, UpstreamRequestPoolReadyAndRequestEncodingFailure) {
  setup();
  kickOffNewUpstreamRequest();

  EXPECT_CALL(*mock_client_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingCallbacks& callback) -> void {
        callback.onEncodingFailure("encoding-failure");
      }));

  EXPECT_CALL(mock_filter_callback_, sendLocalReply(_, _, _))
      .WillOnce(Invoke([this](Status status, absl::string_view data, ResponseUpdateFunction) {
        EXPECT_EQ(0, filter_->upstreamRequestsForTest().size());
        EXPECT_TRUE(status.message() == "protocol_error");
        EXPECT_EQ(data, "encoding-failure");
      }));

  notifyPoolReady(false);

  // Mock downstream closing.
  mock_downstream_connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_P(RouterFilterTest, LoadBalancerContextDownstreamConnection) {
  setup();
  EXPECT_CALL(mock_filter_callback_, connection());
  filter_->downstreamConnection();
}

TEST_P(RouterFilterTest, LoadBalancerContextNoMetadataMatchCriteria) {
  setup();

  // No metadata match criteria by default.
  EXPECT_EQ(nullptr, filter_->metadataMatchCriteria());
}

TEST_P(RouterFilterTest, LoadBalancerContextMetadataMatchCriteria) {
  setup();
  verifyMetadataMatchCriteria();
}

} // namespace
} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
