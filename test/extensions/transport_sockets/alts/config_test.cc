#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "envoy/api/io_error.h"
#include "envoy/buffer/buffer.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/transport_sockets/alts/v3/alts.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stats/scope.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/singleton/manager_impl.h"
#include "source/extensions/transport_sockets/alts/alts_channel_pool.h"
#include "source/extensions/transport_sockets/alts/config.h"

#include "test/extensions/transport_sockets/alts/fake_async_handshaker.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/network/io_handle.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Server::Configuration::MockTransportSocketFactoryContext;
using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {
namespace {

TEST(UpstreamAltsConfigTest, CreateSocketFactory) {
  NiceMock<MockTransportSocketFactoryContext> factory_context;
  Singleton::ManagerImpl singleton_manager;
  EXPECT_CALL(factory_context.server_context_, singletonManager())
      .WillRepeatedly(ReturnRef(singleton_manager));
  UpstreamAltsTransportSocketConfigFactory factory;

  ProtobufTypes::MessagePtr config = factory.createEmptyConfigProto();

  std::string yaml = R"EOF(
  handshaker_service: 169.254.169.254:8080
  peer_service_accounts: ["server-sa"]
  )EOF";
  TestUtility::loadFromYaml(yaml, *config);

  auto socket_factory = factory.createTransportSocketFactory(*config, factory_context).value();

  EXPECT_NE(nullptr, socket_factory);
  EXPECT_TRUE(socket_factory->implementsSecureTransport());
}

TEST(DownstreamAltsConfigTest, CreateSocketFactory) {
  NiceMock<MockTransportSocketFactoryContext> factory_context;
  Singleton::ManagerImpl singleton_manager;
  EXPECT_CALL(factory_context.server_context_, singletonManager())
      .WillRepeatedly(ReturnRef(singleton_manager));
  DownstreamAltsTransportSocketConfigFactory factory;

  ProtobufTypes::MessagePtr config = factory.createEmptyConfigProto();

  std::string yaml = R"EOF(
  handshaker_service: 169.254.169.254:8080
  peer_service_accounts: ["server-sa"]
  )EOF";
  TestUtility::loadFromYaml(yaml, *config);

  auto socket_factory = factory.createTransportSocketFactory(*config, factory_context, {}).value();

  EXPECT_NE(nullptr, socket_factory);
  EXPECT_TRUE(socket_factory->implementsSecureTransport());
}

TEST(UpstreamAltsConfigTest, CreateSocketFactoryAndInstantiateSocketWithOptions) {
  NiceMock<MockTransportSocketFactoryContext> factory_context;
  Singleton::ManagerImpl singleton_manager;
  EXPECT_CALL(factory_context.server_context_, singletonManager())
      .WillRepeatedly(ReturnRef(singleton_manager));
  UpstreamAltsTransportSocketConfigFactory factory;

  ProtobufTypes::MessagePtr config = factory.createEmptyConfigProto();
  std::string yaml = R"EOF(
  handshaker_service: 169.254.169.254:8080
  peer_service_accounts: ["server-sa"]
  )EOF";
  TestUtility::loadFromYaml(yaml, *config);

  auto socket_factory = factory.createTransportSocketFactory(*config, factory_context).value();
  ASSERT_NE(nullptr, socket_factory);

  // Verify socket creation with serverNameOverride works cleanly through the config factory helper
  auto options = std::make_shared<Network::TransportSocketOptionsImpl>("custom.server.target");
  auto socket = socket_factory->createTransportSocket(options, nullptr);
  EXPECT_NE(nullptr, socket);
}

// Starts the handshake of an ALTS transport socket created by the config factory, which makes the
// factory create the socket's handshaker.
class AltsHandshakerFactoryTest : public testing::Test {
protected:
  AltsHandshakerFactoryTest() {
    ON_CALL(factory_context_.server_context_, singletonManager())
        .WillByDefault(ReturnRef(singleton_manager_));
    ON_CALL(callbacks_, ioHandle()).WillByDefault(ReturnRef(io_handle_));
  }

  ~AltsHandshakerFactoryTest() override {
    if (socket_ != nullptr) {
      socket_->closeSocket(Network::ConnectionEvent::LocalClose);
    }
  }

  Grpc::MockAsyncClientManager& asyncClientManager() {
    return factory_context_.server_context_.cluster_manager_.async_client_manager_;
  }

  // Makes the async gRPC client manager reject the handshaker service's config, as it does when
  // Envoy is built without Google gRPC. By default, the mock accepts it.
  void disableGoogleGrpc() {
    ON_CALL(asyncClientManager(), factoryForGrpcService(_, _, _))
        .WillByDefault([](const envoy::config::core::v3::GrpcService&, Stats::Scope&,
                          bool) -> absl::StatusOr<Grpc::AsyncClientFactoryPtr> {
          return absl::InvalidArgumentError("Google C++ gRPC client is not linked");
        });
  }

  // A downstream (server) socket starts its handshake when the client's first handshake message
  // arrives.
  void startDownstreamHandshake(absl::string_view handshaker_service) {
    downstream_socket_factory_ =
        DownstreamAltsTransportSocketConfigFactory()
            .createTransportSocketFactory(config(handshaker_service), factory_context_, {})
            .value();
    socket_ = downstream_socket_factory_->createDownstreamTransportSocket();
    socket_->setTransportSocketCallbacks(callbacks_);
    EXPECT_CALL(io_handle_, read(_, _))
        .WillOnce([](Buffer::Instance& buffer, std::optional<uint64_t>) {
          buffer.add(FakeHandshake::ClientInit);
          return Api::IoCallUint64Result(FakeHandshake::ClientInit.size(), Api::IoError::none());
        })
        .WillRepeatedly([](Buffer::Instance&, std::optional<uint64_t>) {
          return Api::IoCallUint64Result(0, Network::IoSocketError::getIoSocketEagainError());
        });
    Buffer::OwnedImpl read_buffer;
    socket_->doRead(read_buffer);
  }

  // An upstream (client) socket starts its handshake as soon as it connects.
  void startUpstreamHandshake(absl::string_view handshaker_service) {
    upstream_socket_factory_ =
        UpstreamAltsTransportSocketConfigFactory()
            .createTransportSocketFactory(config(handshaker_service), factory_context_)
            .value();
    socket_ = upstream_socket_factory_->createTransportSocket(nullptr, nullptr);
    socket_->setTransportSocketCallbacks(callbacks_);
    socket_->onConnected();
  }

  // The blocking call to the handshaker service fails right away when its address is invalid, and
  // the connection is closed.
  void expectBlockingHandshakeFailure() {
    EXPECT_CALL(callbacks_.connection_,
                close(Network::ConnectionCloseType::NoFlush, "tsi_handshake_failed"));
  }

  NiceMock<MockTransportSocketFactoryContext> factory_context_;
  Singleton::ManagerImpl singleton_manager_;
  NiceMock<Network::MockIoHandle> io_handle_;
  NiceMock<Network::MockTransportSocketCallbacks> callbacks_;
  Network::DownstreamTransportSocketFactoryPtr downstream_socket_factory_;
  Network::UpstreamTransportSocketFactoryPtr upstream_socket_factory_;
  Network::TransportSocketPtr socket_;

private:
  static envoy::extensions::transport_sockets::alts::v3::Alts
  config(absl::string_view handshaker_service) {
    envoy::extensions::transport_sockets::alts::v3::Alts config;
    config.set_handshaker_service(handshaker_service);
    return config;
  }
};

TEST_F(AltsHandshakerFactoryTest, DownstreamHandshakeUsesAsyncGrpcClient) {
  auto handshaker_client = std::make_shared<FakeAsyncHandshakerClient>();
  envoy::config::core::v3::GrpcService grpc_service;
  EXPECT_CALL(asyncClientManager(), getOrCreateRawAsyncClientWithHashKey(_, _, true))
      .WillOnce([&](const Grpc::GrpcServiceConfigWithHashKey& config, Stats::Scope&,
                    bool) -> absl::StatusOr<Grpc::RawAsyncClientSharedPtr> {
        grpc_service = config.config();
        return handshaker_client;
      });
  startDownstreamHandshake("169.254.169.254:8080");

  // The handshake started on a stream of the async client, and is waiting for its response.
  ASSERT_EQ(handshaker_client->streams().size(), 1);
  ASSERT_EQ(handshaker_client->lastStream().requests().size(), 1);
  EXPECT_EQ(handshaker_client->lastStream().requests()[0].server_start().in_bytes(),
            FakeHandshake::ClientInit);

  // The async client connects to the handshaker service like the blocking path's channels do.
  const auto& google_grpc = grpc_service.google_grpc();
  EXPECT_EQ(google_grpc.target_uri(), "169.254.169.254:8080");
  EXPECT_EQ(google_grpc.stat_prefix(), "alts_handshaker");
  EXPECT_FALSE(google_grpc.has_channel_credentials());
  const auto& channel_args = google_grpc.channel_args().args();
  EXPECT_EQ(channel_args.size(), AltsChannelPool::channelArgs().size());
  for (const auto& [key, value] : AltsChannelPool::channelArgs()) {
    ASSERT_TRUE(channel_args.contains(key)) << key;
    EXPECT_EQ(channel_args.at(key).int_value(), value) << key;
  }
}

TEST_F(AltsHandshakerFactoryTest, DownstreamHandshakeBlocksWithoutGoogleGrpc) {
  disableGoogleGrpc();
  EXPECT_CALL(asyncClientManager(), getOrCreateRawAsyncClientWithHashKey(_, _, _)).Times(0);
  expectBlockingHandshakeFailure();
  startDownstreamHandshake(" ");
}

TEST_F(AltsHandshakerFactoryTest, DownstreamHandshakeBlocksWhenAsyncGrpcClientIsUnavailable) {
  EXPECT_CALL(asyncClientManager(), getOrCreateRawAsyncClientWithHashKey(_, _, true))
      .WillOnce(Return(absl::InternalError("no client")));
  expectBlockingHandshakeFailure();
  startDownstreamHandshake(" ");
}

TEST_F(AltsHandshakerFactoryTest, DownstreamHandshakeBlocksWhenRuntimeGuardIsDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.alts_async_handshaker", "false"}});
  EXPECT_CALL(asyncClientManager(), getOrCreateRawAsyncClientWithHashKey(_, _, _)).Times(0);
  expectBlockingHandshakeFailure();
  startDownstreamHandshake(" ");
}

TEST_F(AltsHandshakerFactoryTest, UpstreamHandshakeBlocks) {
  EXPECT_CALL(asyncClientManager(), factoryForGrpcService(_, _, _)).Times(0);
  EXPECT_CALL(asyncClientManager(), getOrCreateRawAsyncClientWithHashKey(_, _, _)).Times(0);
  expectBlockingHandshakeFailure();
  startUpstreamHandshake(" ");
}

} // namespace
} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
