#include <memory>
#include <string>

#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"
#include "envoy/router/string_accessor.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/stream_info/uint64_accessor.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_upstream_lifecycle.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/thread_local/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ReverseTunnelUpstreamLifecycleFilterTest : public testing::Test {
protected:
  ReverseTunnelUpstreamLifecycleFilterTest() : dispatcher_("worker_0") {
    stats_scope_ = Stats::ScopeSharedPtr(stats_store_.createScope("test_scope."));
    EXPECT_CALL(context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));

    config_.set_stat_prefix("test_prefix");
    EXPECT_CALL(dispatcher_, createTimer_(_))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockTimer>>());
    EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockFileEvent>>());

    socket_interface_ = std::make_unique<ReverseTunnelAcceptor>(context_);
    extension_ =
        std::make_unique<ReverseTunnelAcceptorExtension>(*socket_interface_, context_, config_);
  }

  void SetUp() override {
    auto* registered_upstream_interface =
        Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
    ASSERT_NE(registered_upstream_interface, nullptr);
    auto* registered_acceptor = dynamic_cast<ReverseTunnelAcceptor*>(
        const_cast<Network::SocketInterface*>(registered_upstream_interface));
    ASSERT_NE(registered_acceptor, nullptr);
    registered_acceptor->extension_ = extension_.get();

    thread_local_registry_ =
        std::make_shared<UpstreamSocketThreadLocal>(dispatcher_, extension_.get());
    socket_manager_ = thread_local_registry_->socketManager();
    ASSERT_NE(socket_manager_, nullptr);

    tls_slot_ = ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>::makeUnique(thread_local_);
    thread_local_.setDispatcher(&dispatcher_);
    tls_slot_->set([registry = thread_local_registry_](Event::Dispatcher&)
                       -> std::shared_ptr<UpstreamSocketThreadLocal> { return registry; });
    extension_->setTestOnlyTLSRegistry(std::move(tls_slot_));

    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(ReturnRef(callbacks_.connection_));
    EXPECT_CALL(callbacks_.connection_, streamInfo())
        .WillRepeatedly(ReturnRef(callbacks_.connection_.stream_info_));
    EXPECT_CALL(callbacks_.connection_, localCloseReason())
        .WillRepeatedly(Invoke(
            [this]() -> absl::string_view { return callbacks_.connection_.local_close_reason_; }));
  }

  Network::ConnectionSocketPtr createMockSocket(int fd = 123,
                                                const std::string& local_addr = "127.0.0.1:8080",
                                                const std::string& remote_addr = "127.0.0.1:9090") {
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();

    auto local_colon_pos = local_addr.find(':');
    auto local_address = Network::Utility::parseInternetAddressNoThrow(
        local_addr.substr(0, local_colon_pos), std::stoi(local_addr.substr(local_colon_pos + 1)));
    auto remote_colon_pos = remote_addr.find(':');
    auto remote_address = Network::Utility::parseInternetAddressNoThrow(
        remote_addr.substr(0, remote_colon_pos),
        std::stoi(remote_addr.substr(remote_colon_pos + 1)));

    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    auto* mock_io_handle_ptr = mock_io_handle.get();
    EXPECT_CALL(*mock_io_handle_ptr, fdDoNotUse()).WillRepeatedly(Return(fd));
    EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle_ptr));

    socket->io_handle_ = std::move(mock_io_handle);
    socket->connection_info_provider_->setLocalAddress(local_address);
    socket->connection_info_provider_->setRemoteAddress(remote_address);
    return socket;
  }

  std::string filterStateString(absl::string_view key) const {
    const auto* accessor =
        callbacks_.connection_.stream_info_.filter_state_->getDataReadOnly<Router::StringAccessor>(
            key);
    return accessor != nullptr ? std::string(accessor->asString()) : "";
  }

  uint64_t filterStateUint64(absl::string_view key) const {
    const auto* accessor = callbacks_.connection_.stream_info_.filter_state_
                               ->getDataReadOnly<StreamInfo::UInt64Accessor>(key);
    return accessor != nullptr ? accessor->value() : 0;
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Network::MockReadFilterCallbacks> callbacks_;

  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;

  std::unique_ptr<ReverseTunnelAcceptor> socket_interface_;
  std::unique_ptr<ReverseTunnelAcceptorExtension> extension_;
  std::shared_ptr<UpstreamSocketThreadLocal> thread_local_registry_;
  std::unique_ptr<ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>> tls_slot_;
  UpstreamSocketManager* socket_manager_{nullptr};
  Network::ConnectionSocketPtr handed_off_socket_;
};

TEST_F(ReverseTunnelUpstreamLifecycleFilterTest,
       CopiesLifecycleIntoFilterStateAndLogsKeepaliveTimeoutOnce) {
  const int fd = 321;
  auto socket = createMockSocket(fd);
  socket_manager_->addConnectionSocket("node-c", "cluster-c", std::move(socket),
                                       std::chrono::seconds(30), false, "tenant-c");
  handed_off_socket_ = socket_manager_->getConnectionSocket("node-c");
  ASSERT_NE(handed_off_socket_, nullptr);

  EXPECT_CALL(callbacks_.connection_, getSocket()).WillRepeatedly(ReturnRef(handed_off_socket_));
  EXPECT_CALL(callbacks_.connection_, addConnectionCallbacks(_));

  ReverseTunnelUpstreamLifecycleFilter filter;
  filter.initializeReadFilterCallbacks(callbacks_);
  EXPECT_EQ(filter.onNewConnection(), Network::FilterStatus::Continue);

  EXPECT_EQ(filterStateString(kFilterStateNodeId), "node-c");
  EXPECT_EQ(filterStateString(kFilterStateClusterId), "cluster-c");
  EXPECT_EQ(filterStateString(kFilterStateTenantId), "tenant-c");
  EXPECT_EQ(filterStateUint64(kFilterStateFd), fd);

  auto access_log = std::make_shared<NiceMock<AccessLog::MockInstance>>();
  extension_->setTestOnlyAccessLogs({access_log});
  callbacks_.connection_.local_close_reason_ =
      std::string(StreamInfo::LocalCloseReasons::get().Http2PingTimeout);

  EXPECT_CALL(*access_log, log(_, _))
      .WillOnce(Invoke([&](const Formatter::Context& context, const StreamInfo::StreamInfo&) {
        EXPECT_EQ(context.accessLogType(), AccessLog::AccessLogType::UpstreamEnd);
      }));

  filter.onEvent(Network::ConnectionEvent::LocalClose);
  filter.onEvent(Network::ConnectionEvent::LocalClose);

  const auto* lifecycle = socket_manager_->getLifecycleInfo(fd);
  ASSERT_NE(lifecycle, nullptr);
  EXPECT_EQ(lifecycle->close_reason, StreamInfo::LocalCloseReasons::get().Http2PingTimeout);

  // Clear access logs before teardown so the destructor-emitted tunnel_closed
  // close log does not trigger an unexpected extra mock call.
  extension_->setTestOnlyAccessLogs({});
}

TEST_F(ReverseTunnelUpstreamLifecycleFilterTest,
       LocalCloseAfterSocketDeathReusesHttp2PingTimeoutForTunnelClosed) {
  const int fd = 654;
  auto socket = createMockSocket(fd);
  socket_manager_->addConnectionSocket("node-d", "cluster-d", std::move(socket),
                                       std::chrono::seconds(30), false, "tenant-d");
  handed_off_socket_ = socket_manager_->getConnectionSocket("node-d");
  ASSERT_NE(handed_off_socket_, nullptr);

  EXPECT_CALL(callbacks_.connection_, getSocket()).WillRepeatedly(ReturnRef(handed_off_socket_));
  EXPECT_CALL(callbacks_.connection_, addConnectionCallbacks(_));

  ReverseTunnelUpstreamLifecycleFilter filter;
  filter.initializeReadFilterCallbacks(callbacks_);
  EXPECT_EQ(filter.onNewConnection(), Network::FilterStatus::Continue);

  auto access_log = std::make_shared<NiceMock<AccessLog::MockInstance>>();
  extension_->setTestOnlyAccessLogs({access_log});
  callbacks_.connection_.local_close_reason_ =
      std::string(StreamInfo::LocalCloseReasons::get().Http2PingTimeout);

  testing::InSequence sequence;
  EXPECT_CALL(*access_log, log(_, _))
      .WillOnce(
          Invoke([&](const Formatter::Context& context, const StreamInfo::StreamInfo& stream_info) {
            EXPECT_EQ(context.accessLogType(), AccessLog::AccessLogType::UpstreamEnd);
            ASSERT_TRUE(stream_info.connectionTerminationDetails().has_value());
            EXPECT_EQ(stream_info.connectionTerminationDetails().value(),
                      StreamInfo::LocalCloseReasons::get().Http2PingTimeout);
          }));
  EXPECT_CALL(*access_log, log(_, _))
      .WillOnce(
          Invoke([&](const Formatter::Context& context, const StreamInfo::StreamInfo& stream_info) {
            EXPECT_EQ(context.accessLogType(), AccessLog::AccessLogType::UpstreamEnd);
            ASSERT_TRUE(stream_info.connectionTerminationDetails().has_value());
            EXPECT_EQ(stream_info.connectionTerminationDetails().value(),
                      StreamInfo::LocalCloseReasons::get().Http2PingTimeout);
          }));

  socket_manager_->markSocketDead(fd);
  filter.onEvent(Network::ConnectionEvent::LocalClose);

  EXPECT_EQ(socket_manager_->getLifecycleInfo(fd), nullptr);
}

TEST_F(ReverseTunnelUpstreamLifecycleFilterTest,
       RemoteCloseAfterSocketDeathLogsTunnelClosedWithRemoteCloseReason) {
  const int fd = 777;
  auto socket = createMockSocket(fd);
  socket_manager_->addConnectionSocket("node-e", "cluster-e", std::move(socket),
                                       std::chrono::seconds(30), false, "tenant-e");
  handed_off_socket_ = socket_manager_->getConnectionSocket("node-e");
  ASSERT_NE(handed_off_socket_, nullptr);

  EXPECT_CALL(callbacks_.connection_, getSocket()).WillRepeatedly(ReturnRef(handed_off_socket_));
  EXPECT_CALL(callbacks_.connection_, addConnectionCallbacks(_));

  ReverseTunnelUpstreamLifecycleFilter filter;
  filter.initializeReadFilterCallbacks(callbacks_);
  EXPECT_EQ(filter.onNewConnection(), Network::FilterStatus::Continue);

  auto access_log = std::make_shared<NiceMock<AccessLog::MockInstance>>();
  extension_->setTestOnlyAccessLogs({access_log});

  EXPECT_CALL(*access_log, log(_, _))
      .WillOnce(
          Invoke([&](const Formatter::Context& context, const StreamInfo::StreamInfo& stream_info) {
            const auto& metadata = stream_info.dynamicMetadata().filter_metadata().at(
                std::string(kAccessLogMetadataNamespace));
            EXPECT_EQ(context.accessLogType(), AccessLog::AccessLogType::UpstreamEnd);
            EXPECT_EQ(metadata.fields().at("event").string_value(),
                      std::string(kLifecycleEventTunnelClosed));
            EXPECT_EQ(metadata.fields().at("close_reason").string_value(),
                      std::string(kLifecycleCloseReasonRemoteClose));
            ASSERT_TRUE(stream_info.connectionTerminationDetails().has_value());
            EXPECT_EQ(stream_info.connectionTerminationDetails().value(),
                      std::string(kLifecycleCloseReasonRemoteClose));
          }));

  socket_manager_->markSocketDead(fd);
  filter.onEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(socket_manager_->getLifecycleInfo(fd), nullptr);
}

TEST_F(ReverseTunnelUpstreamLifecycleFilterTest,
       GenericLocalCloseAfterSocketDeathLogsTunnelClosedWithLocalCloseReason) {
  const int fd = 888;
  auto socket = createMockSocket(fd);
  socket_manager_->addConnectionSocket("node-f", "cluster-f", std::move(socket),
                                       std::chrono::seconds(30), false, "tenant-f");
  handed_off_socket_ = socket_manager_->getConnectionSocket("node-f");
  ASSERT_NE(handed_off_socket_, nullptr);

  EXPECT_CALL(callbacks_.connection_, getSocket()).WillRepeatedly(ReturnRef(handed_off_socket_));
  EXPECT_CALL(callbacks_.connection_, addConnectionCallbacks(_));

  ReverseTunnelUpstreamLifecycleFilter filter;
  filter.initializeReadFilterCallbacks(callbacks_);
  EXPECT_EQ(filter.onNewConnection(), Network::FilterStatus::Continue);

  auto access_log = std::make_shared<NiceMock<AccessLog::MockInstance>>();
  extension_->setTestOnlyAccessLogs({access_log});
  callbacks_.connection_.local_close_reason_.clear();

  EXPECT_CALL(*access_log, log(_, _))
      .WillOnce(
          Invoke([&](const Formatter::Context& context, const StreamInfo::StreamInfo& stream_info) {
            const auto& metadata = stream_info.dynamicMetadata().filter_metadata().at(
                std::string(kAccessLogMetadataNamespace));
            EXPECT_EQ(context.accessLogType(), AccessLog::AccessLogType::UpstreamEnd);
            EXPECT_EQ(metadata.fields().at("event").string_value(),
                      std::string(kLifecycleEventTunnelClosed));
            EXPECT_EQ(metadata.fields().at("close_reason").string_value(),
                      std::string(kLifecycleCloseReasonLocalClose));
            ASSERT_TRUE(stream_info.connectionTerminationDetails().has_value());
            EXPECT_EQ(stream_info.connectionTerminationDetails().value(),
                      std::string(kLifecycleCloseReasonLocalClose));
          }));

  socket_manager_->markSocketDead(fd);
  filter.onEvent(Network::ConnectionEvent::LocalClose);

  EXPECT_EQ(socket_manager_->getLifecycleInfo(fd), nullptr);
}

TEST_F(ReverseTunnelUpstreamLifecycleFilterTest, OnNewConnectionReturnsWhenSocketIsNull) {
  Network::ConnectionSocketPtr null_socket;
  EXPECT_CALL(callbacks_.connection_, getSocket()).WillRepeatedly(ReturnRef(null_socket));
  EXPECT_CALL(callbacks_.connection_, addConnectionCallbacks(_));

  ReverseTunnelUpstreamLifecycleFilter filter;
  filter.initializeReadFilterCallbacks(callbacks_);
  EXPECT_EQ(filter.onNewConnection(), Network::FilterStatus::Continue);

  EXPECT_EQ(filterStateString(kFilterStateNodeId), "");
}

TEST_F(ReverseTunnelUpstreamLifecycleFilterTest, OnNewConnectionReturnsWhenNoLifecycleInfo) {
  const int fd = 999;
  auto socket = createMockSocket(fd);
  EXPECT_CALL(callbacks_.connection_, getSocket()).WillRepeatedly(ReturnRef(socket));
  EXPECT_CALL(callbacks_.connection_, addConnectionCallbacks(_));

  ReverseTunnelUpstreamLifecycleFilter filter;
  filter.initializeReadFilterCallbacks(callbacks_);
  EXPECT_EQ(filter.onNewConnection(), Network::FilterStatus::Continue);

  EXPECT_EQ(filterStateString(kFilterStateNodeId), "");
}

TEST_F(ReverseTunnelUpstreamLifecycleFilterTest, OnEventIgnoredWhenLifecycleNotInitialized) {
  Network::ConnectionSocketPtr null_socket;
  EXPECT_CALL(callbacks_.connection_, getSocket()).WillRepeatedly(ReturnRef(null_socket));
  EXPECT_CALL(callbacks_.connection_, addConnectionCallbacks(_));

  ReverseTunnelUpstreamLifecycleFilter filter;
  filter.initializeReadFilterCallbacks(callbacks_);
  filter.onNewConnection();

  filter.onEvent(Network::ConnectionEvent::LocalClose);
  filter.onEvent(Network::ConnectionEvent::RemoteClose);
  filter.onEvent(Network::ConnectionEvent::Connected);
}

TEST_F(ReverseTunnelUpstreamLifecycleFilterTest, ConnectedEventIsIgnored) {
  const int fd = 555;
  auto socket = createMockSocket(fd);
  socket_manager_->addConnectionSocket("node-g", "cluster-g", std::move(socket),
                                       std::chrono::seconds(30), false, "tenant-g");
  handed_off_socket_ = socket_manager_->getConnectionSocket("node-g");
  ASSERT_NE(handed_off_socket_, nullptr);

  EXPECT_CALL(callbacks_.connection_, getSocket()).WillRepeatedly(ReturnRef(handed_off_socket_));
  EXPECT_CALL(callbacks_.connection_, addConnectionCallbacks(_));

  ReverseTunnelUpstreamLifecycleFilter filter;
  filter.initializeReadFilterCallbacks(callbacks_);
  EXPECT_EQ(filter.onNewConnection(), Network::FilterStatus::Continue);

  auto access_log = std::make_shared<NiceMock<AccessLog::MockInstance>>();
  extension_->setTestOnlyAccessLogs({access_log});

  EXPECT_CALL(*access_log, log(_, _)).Times(0);
  filter.onEvent(Network::ConnectionEvent::Connected);

  extension_->setTestOnlyAccessLogs({});
}

TEST_F(ReverseTunnelUpstreamLifecycleFilterTest, OnDataReturnsContinue) {
  ReverseTunnelUpstreamLifecycleFilter filter;
  Buffer::OwnedImpl buffer("data");
  EXPECT_EQ(filter.onData(buffer, false), Network::FilterStatus::Continue);
}

TEST_F(ReverseTunnelUpstreamLifecycleFilterTest, FactoryCreatesEmptyProto) {
  ReverseTunnelUpstreamLifecycleConfigFactory factory;
  auto proto = factory.createEmptyConfigProto();
  EXPECT_NE(proto, nullptr);
}

TEST_F(ReverseTunnelUpstreamLifecycleFilterTest, FactoryReportsCorrectName) {
  ReverseTunnelUpstreamLifecycleConfigFactory factory;
  EXPECT_EQ(factory.name(), "envoy.filters.upstream_network.reverse_tunnel_lifecycle");
}

TEST_F(ReverseTunnelUpstreamLifecycleFilterTest, FactoryIsNotTerminalFilter) {
  ReverseTunnelUpstreamLifecycleConfigFactory factory;
  Protobuf::Empty empty;
  EXPECT_FALSE(factory.isTerminalFilterByProto(empty, context_));
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
