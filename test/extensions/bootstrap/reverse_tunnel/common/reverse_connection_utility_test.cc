#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor_extension.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/upstream_socket_manager.h"

#include "test/common/tls/mock_ssl_handshaker.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/ssl.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

using TransportSockets::Tls::MockSslHandshakerImpl;

class ReverseConnectionUtilityTest : public testing::Test {
protected:
  ReverseConnectionUtilityTest() = default;
};

// Test isPingMessage functionality
TEST_F(ReverseConnectionUtilityTest, IsPingMessageEmptyData) {
  // Test with empty data
  EXPECT_FALSE(ReverseConnectionUtility::isPingMessage(""));
  EXPECT_FALSE(ReverseConnectionUtility::isPingMessage(absl::string_view()));
}

TEST_F(ReverseConnectionUtilityTest, IsPingMessageExactMatch) {
  // Test with exact RPING match
  EXPECT_TRUE(ReverseConnectionUtility::isPingMessage("RPING"));
  EXPECT_TRUE(ReverseConnectionUtility::isPingMessage(absl::string_view("RPING")));
}

TEST_F(ReverseConnectionUtilityTest, IsPingMessageInvalidData) {
  // Test with non-RPING data. isPingMessage should return false for these cases.
  EXPECT_FALSE(ReverseConnectionUtility::isPingMessage("PING"));
  EXPECT_FALSE(ReverseConnectionUtility::isPingMessage("RPIN"));
  EXPECT_FALSE(ReverseConnectionUtility::isPingMessage("RPINGG"));
  EXPECT_FALSE(ReverseConnectionUtility::isPingMessage("Hello World"));
}

// Test RPING prefix classification.
using RpingPrefixMatch = ReverseConnectionUtility::RpingPrefixMatch;

TEST_F(ReverseConnectionUtilityTest, ClassifyRpingPrefixComplete) {
  // Exactly RPING, or RPING followed by trailing application bytes: both are Complete since only
  // the first PING_MESSAGE.size() bytes are considered.
  EXPECT_EQ(RpingPrefixMatch::Complete, ReverseConnectionUtility::classifyRpingPrefix("RPING"));
  EXPECT_EQ(RpingPrefixMatch::Complete,
            ReverseConnectionUtility::classifyRpingPrefix("RPINGhello"));
}

TEST_F(ReverseConnectionUtilityTest, ClassifyRpingPrefixPartial) {
  // Empty input and every proper prefix of RPING are viable partials.
  EXPECT_EQ(RpingPrefixMatch::PartialPrefix, ReverseConnectionUtility::classifyRpingPrefix(""));
  EXPECT_EQ(RpingPrefixMatch::PartialPrefix, ReverseConnectionUtility::classifyRpingPrefix("R"));
  EXPECT_EQ(RpingPrefixMatch::PartialPrefix, ReverseConnectionUtility::classifyRpingPrefix("RP"));
  EXPECT_EQ(RpingPrefixMatch::PartialPrefix, ReverseConnectionUtility::classifyRpingPrefix("RPI"));
  EXPECT_EQ(RpingPrefixMatch::PartialPrefix, ReverseConnectionUtility::classifyRpingPrefix("RPIN"));
}

TEST_F(ReverseConnectionUtilityTest, ClassifyRpingPrefixNotRping) {
  // A short non-prefix, and a full-length non-match, are both classified as not-RPING.
  EXPECT_EQ(RpingPrefixMatch::NotRping, ReverseConnectionUtility::classifyRpingPrefix("X"));
  EXPECT_EQ(RpingPrefixMatch::NotRping, ReverseConnectionUtility::classifyRpingPrefix("RPX"));
  EXPECT_EQ(RpingPrefixMatch::NotRping, ReverseConnectionUtility::classifyRpingPrefix("PING"));
  EXPECT_EQ(RpingPrefixMatch::NotRping,
            ReverseConnectionUtility::classifyRpingPrefix("Hello World"));
}

// Test createPingResponse functionality
TEST_F(ReverseConnectionUtilityTest, CreatePingResponse) {
  auto ping_buffer = ReverseConnectionUtility::createPingResponse();

  EXPECT_NE(ping_buffer, nullptr);
  EXPECT_EQ(ping_buffer->toString(), "RPING");
  EXPECT_EQ(ping_buffer->length(), 5);
}

// Test sendPingResponse with Connection
TEST_F(ReverseConnectionUtilityTest, SendPingResponseConnection) {
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();

  // Set up mock expectations
  EXPECT_CALL(*connection, write(_, false));
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  bool result = ReverseConnectionUtility::sendPingResponse(*connection);

  EXPECT_TRUE(result);
}

// Test sendPingResponse with IoHandle
TEST_F(ReverseConnectionUtilityTest, SendPingResponseIoHandleSuccess) {
  auto io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();

  EXPECT_CALL(*io_handle, write(_))
      .WillOnce(Return(Api::IoCallUint64Result{5, Api::IoError::none()}));

  Api::IoCallUint64Result result = ReverseConnectionUtility::sendPingResponse(*io_handle);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.return_value_, 5);
  EXPECT_EQ(result.err_, nullptr);
}

TEST_F(ReverseConnectionUtilityTest, SendPingResponseIoHandleFailure) {
  auto io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();

  // Set up mock expectations for failed write
  EXPECT_CALL(*io_handle, write(_))
      .WillOnce(Return(Api::IoCallUint64Result{0, Network::IoSocketError::create(ECONNRESET)}));

  Api::IoCallUint64Result result = ReverseConnectionUtility::sendPingResponse(*io_handle);

  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.return_value_, 0);
  EXPECT_NE(result.err_, nullptr);
}

// Test handlePingMessage functionality
TEST_F(ReverseConnectionUtilityTest, HandlePingMessageValidPing) {
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();

  // should call sendPingResponse and return true since it is a valid RPING message
  EXPECT_CALL(*connection, write(_, false));
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  bool result = ReverseConnectionUtility::handlePingMessage("RPING", *connection);

  EXPECT_TRUE(result);
}

TEST_F(ReverseConnectionUtilityTest, HandlePingMessageInvalidData) {
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();

  // Should not call sendPingResponse for invalid data
  EXPECT_CALL(*connection, write(_, _)).Times(0);
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  bool result = ReverseConnectionUtility::handlePingMessage("INVALID", *connection);

  EXPECT_FALSE(result);
}

TEST_F(ReverseConnectionUtilityTest, HandlePingMessageEmptyData) {
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();

  // Should not call sendPingResponse for empty data
  EXPECT_CALL(*connection, write(_, _)).Times(0);
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  bool result = ReverseConnectionUtility::handlePingMessage("", *connection);

  EXPECT_FALSE(result);
}

// Test extractPingFromHttpData functionality
TEST_F(ReverseConnectionUtilityTest, ExtractPingFromHttpDataValid) {
  // Test with RPING in HTTP response body
  EXPECT_TRUE(ReverseConnectionUtility::extractPingFromHttpData("HTTP/1.1 200 OK\r\n\r\nRPING"));
  EXPECT_TRUE(ReverseConnectionUtility::extractPingFromHttpData(
      "GET /ping HTTP/1.1\r\nHost: example.com\r\n\r\nRPING"));
  EXPECT_TRUE(ReverseConnectionUtility::extractPingFromHttpData(
      "POST /data HTTP/1.1\r\nContent-Length: 5\r\n\r\nRPING"));
}

TEST_F(ReverseConnectionUtilityTest, ExtractPingFromHttpDataInvalid) {
  // Test with no RPING in HTTP data
  EXPECT_FALSE(ReverseConnectionUtility::extractPingFromHttpData("HTTP/1.1 200 OK\r\n\r\nHello"));
  EXPECT_FALSE(ReverseConnectionUtility::extractPingFromHttpData(
      "GET /ping HTTP/1.1\r\nHost: example.com\r\n\r\nPING"));
  EXPECT_FALSE(ReverseConnectionUtility::extractPingFromHttpData(""));
}

// Test ReverseConnectionMessageHandlerFactory functionality
TEST_F(ReverseConnectionUtilityTest, CreatePingHandler) {
  auto handler = ReverseConnectionMessageHandlerFactory::createPingHandler();

  EXPECT_NE(handler, nullptr);
  EXPECT_EQ(handler->getPingCount(), 0);
}

// Test PingMessageHandler functionality
TEST_F(ReverseConnectionUtilityTest, PingMessageHandlerProcessValidPing) {
  auto handler = ReverseConnectionMessageHandlerFactory::createPingHandler();
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();

  // Set up mock expectations
  EXPECT_CALL(*connection, write(_, false));
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  bool result = handler->processPingMessage("RPING", *connection);

  EXPECT_TRUE(result);
  EXPECT_EQ(handler->getPingCount(), 1);
}

TEST_F(ReverseConnectionUtilityTest, PingMessageHandlerProcessInvalidPing) {
  auto handler = ReverseConnectionMessageHandlerFactory::createPingHandler();
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();

  // Set up mock expectations - should not call write for invalid data
  EXPECT_CALL(*connection, write(_, _)).Times(0);
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  bool result = handler->processPingMessage("INVALID", *connection);

  EXPECT_FALSE(result);
  EXPECT_EQ(handler->getPingCount(), 0);
}

TEST_F(ReverseConnectionUtilityTest, PingMessageHandlerProcessMultiplePings) {
  auto handler = ReverseConnectionMessageHandlerFactory::createPingHandler();
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();

  // Set up mock expectations for multiple writes
  EXPECT_CALL(*connection, write(_, false)).Times(3);
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  // Process multiple valid pings
  EXPECT_TRUE(handler->processPingMessage("RPING", *connection));
  EXPECT_TRUE(handler->processPingMessage("RPING", *connection));
  EXPECT_TRUE(handler->processPingMessage("RPING", *connection));

  EXPECT_EQ(handler->getPingCount(), 3);
}

TEST_F(ReverseConnectionUtilityTest, PingMessageHandlerProcessEmptyPing) {
  auto handler = ReverseConnectionMessageHandlerFactory::createPingHandler();
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();

  // Set up mock expectations - should not call write for empty data
  EXPECT_CALL(*connection, write(_, _)).Times(0);
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  bool result = handler->processPingMessage("", *connection);

  EXPECT_FALSE(result);
  EXPECT_EQ(handler->getPingCount(), 0);
}

TEST_F(ReverseConnectionUtilityTest, PingMessageHandlerGetPingCount) {
  auto handler = ReverseConnectionMessageHandlerFactory::createPingHandler();

  // Initially should be 0
  EXPECT_EQ(handler->getPingCount(), 0);

  // After processing a ping, should be 1
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();
  EXPECT_CALL(*connection, write(_, false));
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  handler->processPingMessage("RPING", *connection);
  EXPECT_EQ(handler->getPingCount(), 1);
}

TEST_F(ReverseConnectionUtilityTest, SplitTenantScopedIdentifierWithDelimiter) {
  const std::string composite =
      ReverseConnectionUtility::buildTenantScopedIdentifier("tenant-alpha", "node-1");
  const auto result = ReverseConnectionUtility::splitTenantScopedIdentifier(composite);
  EXPECT_TRUE(result.hasTenant());
  EXPECT_EQ(result.tenant, "tenant-alpha");
  EXPECT_EQ(result.identifier, "node-1");
}

TEST_F(ReverseConnectionUtilityTest, SplitTenantScopedIdentifierWithoutDelimiter) {
  const absl::string_view composite = "node-plain";
  const auto result = ReverseConnectionUtility::splitTenantScopedIdentifier(composite);
  EXPECT_FALSE(result.hasTenant());
  EXPECT_TRUE(result.tenant.empty());
  EXPECT_EQ(result.identifier, "node-plain");
}

TEST_F(ReverseConnectionUtilityTest, SplitTenantScopedIdentifierEmptyValue) {
  const auto result = ReverseConnectionUtility::splitTenantScopedIdentifier("");
  EXPECT_FALSE(result.hasTenant());
  EXPECT_TRUE(result.tenant.empty());
  EXPECT_TRUE(result.identifier.empty());
}

TEST_F(ReverseConnectionUtilityTest, BuildTenantScopedIdentifierWithTenant) {
  const std::string composite =
      ReverseConnectionUtility::buildTenantScopedIdentifier("tenant-alpha", "node-1");
  EXPECT_EQ(composite, absl::StrCat("tenant-alpha",
                                    ReverseConnectionUtility::TENANT_SCOPE_DELIMITER, "node-1"));
}

TEST_F(ReverseConnectionUtilityTest, BuildTenantScopedIdentifierWithoutTenant) {
  const std::string composite = ReverseConnectionUtility::buildTenantScopedIdentifier("", "node-1");
  EXPECT_EQ(composite, "node-1");
}

TEST_F(ReverseConnectionUtilityTest, ClusterScopedIdentifierRoundTripWithTenant) {
  const std::string node_id = ReverseConnectionUtility::buildTenantScopedIdentifier("t", "node-1");
  const std::string cluster_id =
      ReverseConnectionUtility::buildTenantScopedIdentifier("t", "cluster-1");

  const std::string composite =
      ReverseConnectionUtility::buildClusterScopedIdentifier(node_id, cluster_id);
  EXPECT_EQ(composite, "t:cluster-1:node-1");

  const auto [recovered_node, recovered_cluster] =
      ReverseConnectionUtility::splitClusterScopedIdentifier(composite);
  EXPECT_EQ(recovered_node, node_id);
  EXPECT_EQ(recovered_cluster, cluster_id);
}

TEST_F(ReverseConnectionUtilityTest, ClusterScopedIdentifierRoundTripWithoutTenant) {
  const std::string composite =
      ReverseConnectionUtility::buildClusterScopedIdentifier("node-1", "cluster-1");
  EXPECT_EQ(composite, "cluster-1:node-1");

  const auto [recovered_node, recovered_cluster] =
      ReverseConnectionUtility::splitClusterScopedIdentifier(composite);
  EXPECT_EQ(recovered_node, "node-1");
  EXPECT_EQ(recovered_cluster, "cluster-1");
}

TEST_F(ReverseConnectionUtilityTest, ClusterScopedIdentifierEmptyCluster) {
  const std::string composite =
      ReverseConnectionUtility::buildClusterScopedIdentifier("node-1", "");
  EXPECT_EQ(composite, "node-1");

  const auto [recovered_node, recovered_cluster] =
      ReverseConnectionUtility::splitClusterScopedIdentifier(composite);
  EXPECT_EQ(recovered_node, "node-1");
  EXPECT_EQ(recovered_cluster, "");
}

TEST_F(ReverseConnectionUtilityTest, ApplySslQuietCloseWithoutSsl) {
  NiceMock<Network::MockConnection> connection;
  EXPECT_CALL(connection, ssl()).WillOnce(Return(nullptr));

  ReverseConnectionUtility::applySslQuietClose(connection);
}

TEST_F(ReverseConnectionUtilityTest, ApplySslQuietCloseOnValidSslHandshaker) {
  NiceMock<Network::MockConnection> connection;

  bssl::UniquePtr<SSL_CTX> ctx(SSL_CTX_new(TLS_method()));
  ASSERT_NE(ctx, nullptr);
  SSL* ssl = SSL_new(ctx.get());
  ASSERT_NE(ssl, nullptr);
  auto mock_ssl_handshaker = std::make_shared<MockSslHandshakerImpl>(ssl);

  EXPECT_CALL(connection, ssl()).WillOnce(Return(mock_ssl_handshaker));
  EXPECT_EQ(0, SSL_get_quiet_shutdown(ssl));

  ReverseConnectionUtility::applySslQuietClose(connection);

  EXPECT_EQ(1, SSL_get_quiet_shutdown(ssl));
}

// addJitter adds an upward, bounded jitter using the supplied random generator.
TEST_F(ReverseConnectionUtilityTest, AddJitterUpwardDeterministic) {
  NiceMock<Random::MockRandomGenerator> random;
  // 15% of 10000 = 1500 jitter window; 10000 + (700 % 1500) = 10700.
  EXPECT_CALL(random, random()).WillRepeatedly(Return(700));
  EXPECT_EQ(ReverseConnectionUtility::addJitter(10000, 15, random), 10700);
}

// A 0% jitter (or sub-1% interval) returns the interval unchanged.
TEST_F(ReverseConnectionUtilityTest, AddJitterZeroPercentIsNoOp) {
  NiceMock<Random::MockRandomGenerator> random;
  EXPECT_EQ(ReverseConnectionUtility::addJitter(10000, 0, random), 10000);
}

// The jittered interval never drops below the base nor reaches base*(1 + jitter%).
TEST_F(ReverseConnectionUtilityTest, AddJitterStaysWithinUpperBound) {
  NiceMock<Random::MockRandomGenerator> random;
  EXPECT_CALL(random, random()).WillRepeatedly(Return(123456789ULL));
  const uint64_t result = ReverseConnectionUtility::addJitter(10000, 15, random);
  EXPECT_GE(result, 10000);
  EXPECT_LT(result, 11500);
}

TEST_F(ReverseConnectionUtilityTest, DiffMsReturnsCorrectDuration) {
  MonotonicTime start(std::chrono::milliseconds(1000));
  MonotonicTime end(std::chrono::milliseconds(1500));

  EXPECT_EQ(ReverseConnectionUtility::diffMs(start, end), 500);
}

TEST_F(ReverseConnectionUtilityTest, DiffMsZeroDuration) {
  MonotonicTime t(std::chrono::milliseconds(42));

  EXPECT_EQ(ReverseConnectionUtility::diffMs(t, t), 0);
}

class GetThreadLocalSocketManagerTest : public testing::Test {
protected:
  GetThreadLocalSocketManagerTest() : dispatcher_("worker_0") {
    stats_scope_ = stats_store_.createScope("test_scope.");
    EXPECT_CALL(context_, threadLocal()).WillRepeatedly(ReturnRef(thread_local_));
    EXPECT_CALL(context_, scope()).WillRepeatedly(ReturnRef(*stats_scope_));
    config_.set_stat_prefix("test_prefix");
    EXPECT_CALL(dispatcher_, createTimer_(_))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockTimer>>());
    EXPECT_CALL(dispatcher_, createFileEvent_(_, _, _, _))
        .WillRepeatedly(testing::ReturnNew<NiceMock<Event::MockFileEvent>>());
  }

  void SetUp() override {
    extension_ = std::make_unique<ReverseTunnelAcceptorExtension>(acceptor_, context_, config_);
    saved_extension_ = acceptor_.extension_;
    acceptor_.extension_ = extension_.get();
  }

  void TearDown() override {
    acceptor_.extension_ = saved_extension_;
    thread_local_registry_.reset();
    extension_.reset();
  }

  void wireTls() {
    thread_local_registry_ =
        std::make_shared<UpstreamSocketThreadLocal>(dispatcher_, extension_.get());
    socket_manager_ = thread_local_registry_->socketManager();
    thread_local_.setDispatcher(&dispatcher_);
    tls_slot_ = ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>::makeUnique(thread_local_);
    tls_slot_->set([registry = thread_local_registry_](Event::Dispatcher&)
                       -> std::shared_ptr<UpstreamSocketThreadLocal> { return registry; });
    extension_->setTestOnlyTLSRegistry(std::move(tls_slot_));
  }

  ReverseTunnelAcceptor& resolveAcceptor() {
    auto* socket_if =
        Network::socketInterface("envoy.bootstrap.reverse_tunnel.upstream_socket_interface");
    RELEASE_ASSERT(socket_if, "");
    auto* acceptor =
        dynamic_cast<ReverseTunnelAcceptor*>(const_cast<Network::SocketInterface*>(socket_if));
    RELEASE_ASSERT(acceptor, "");
    return *acceptor;
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Stats::TestUtil::TestStore stats_store_;
  Stats::ScopeSharedPtr stats_scope_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  envoy::extensions::bootstrap::reverse_tunnel::upstream_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface config_;
  std::unique_ptr<ReverseTunnelAcceptorExtension> extension_;
  ReverseTunnelAcceptor& acceptor_{resolveAcceptor()};
  ReverseTunnelAcceptorExtension* saved_extension_{nullptr};
  std::shared_ptr<UpstreamSocketThreadLocal> thread_local_registry_;
  std::unique_ptr<ThreadLocal::TypedSlot<UpstreamSocketThreadLocal>> tls_slot_;
  UpstreamSocketManager* socket_manager_{nullptr};
};

TEST_F(GetThreadLocalSocketManagerTest, ReturnsNullWithoutTls) {
  EXPECT_EQ(nullptr, ReverseConnectionUtility::getThreadLocalSocketManager());
}

TEST_F(GetThreadLocalSocketManagerTest, ReturnsNullWithoutExtension) {
  acceptor_.extension_ = nullptr;
  EXPECT_EQ(nullptr, ReverseConnectionUtility::getThreadLocalSocketManager());
}

TEST_F(GetThreadLocalSocketManagerTest, ReturnsManagerWhenWired) {
  wireTls();
  EXPECT_EQ(socket_manager_, ReverseConnectionUtility::getThreadLocalSocketManager());
}

namespace {

constexpr absl::string_view kUpstreamSocketInterfaceName =
    "envoy.bootstrap.reverse_tunnel.upstream_socket_interface";

// Bootstrap factory that is not a SocketInterface, so Network::socketInterface() returns nullptr.
class NonSocketInterfaceFactory : public Server::Configuration::BootstrapExtensionFactory {
public:
  std::string name() const override { return std::string(kUpstreamSocketInterfaceName); }
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message&,
                           Server::Configuration::ServerFactoryContext&) override {
    return nullptr;
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }
};

// SocketInterface that is not ReverseTunnelAcceptor, so the inner dynamic_cast fails.
class NonAcceptorSocketInterface : public Network::SocketInterfaceBase {
public:
  std::string name() const override { return std::string(kUpstreamSocketInterfaceName); }
  Network::IoHandlePtr socket(Network::Socket::Type, Network::Address::Type,
                              Network::Address::IpVersion, bool,
                              const Network::SocketCreationOptions&) const override {
    return nullptr;
  }
  Network::IoHandlePtr socket(Network::Socket::Type, const Network::Address::InstanceConstSharedPtr,
                              const Network::SocketCreationOptions&) const override {
    return nullptr;
  }
  bool ipFamilySupported(int) override { return false; }
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message&,
                           Server::Configuration::ServerFactoryContext&) override {
    return nullptr;
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }
};

} // namespace

TEST(GetThreadLocalSocketManager, ReturnsNullWhenInterfaceMissing) {
  NonSocketInterfaceFactory factory;
  Registry::InjectFactory<Server::Configuration::BootstrapExtensionFactory> inject(factory);
  EXPECT_EQ(nullptr, ReverseConnectionUtility::getThreadLocalSocketManager());
}

TEST(GetThreadLocalSocketManager, ReturnsNullWhenFactoryIsNotAcceptor) {
  NonAcceptorSocketInterface factory;
  Registry::InjectFactory<Server::Configuration::BootstrapExtensionFactory> inject(factory);
  EXPECT_EQ(nullptr, ReverseConnectionUtility::getThreadLocalSocketManager());
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
