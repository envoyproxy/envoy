#include "test/common/integration/xds_test_server.h"

#include <utility>

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"

#include "source/common/event/libevent.h"
#include "source/extensions/config_subscription/grpc/grpc_collection_subscription_factory.h"
#include "source/extensions/config_subscription/grpc/grpc_mux_impl.h"
#include "source/extensions/config_subscription/grpc/grpc_subscription_factory.h"
#include "source/extensions/config_subscription/grpc/new_grpc_mux_impl.h"
#include "source/extensions/transport_sockets/tls/context_config_impl.h"
#include "source/extensions/transport_sockets/tls/ssl_socket.h"

#include "test/integration/fake_upstream.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

namespace Envoy {

XdsTestServer::XdsTestServer()
    : api_(Api::createApiForTest(stats_store_, time_system_)),
      version_(Network::Address::IpVersion::v4),
      mock_buffer_factory_(new NiceMock<MockBufferFactory>), upstream_config_(time_system_) {
  std::string runfiles_error;
  runfiles_ = std::unique_ptr<bazel::tools::cpp::runfiles::Runfiles>{
      bazel::tools::cpp::runfiles::Runfiles::Create("", &runfiles_error)};
  RELEASE_ASSERT(TestEnvironment::getOptionalEnvVar("NORUNFILES").has_value() ||
                     runfiles_ != nullptr,
                 runfiles_error);
  TestEnvironment::setRunfiles(runfiles_.get());

  if (!Envoy::Event::Libevent::Global::initialized()) {
    // Required by the Dispatcher.
    Envoy::Event::Libevent::Global::initialize();
  }
  dispatcher_ =
      api_->allocateDispatcher("test_thread", Buffer::WatermarkFactoryPtr{mock_buffer_factory_});

  ON_CALL(*mock_buffer_factory_, createBuffer_(_, _, _))
      .WillByDefault(Invoke([](std::function<void()> below_low, std::function<void()> above_high,
                               std::function<void()> above_overflow) -> Buffer::Instance* {
        return new Buffer::WatermarkBuffer(std::move(below_low), std::move(above_high),
                                           std::move(above_overflow));
      }));
  ON_CALL(factory_context_.server_context_, api()).WillByDefault(testing::ReturnRef(*api_));
  ON_CALL(factory_context_, statsScope())
      .WillByDefault(testing::ReturnRef(*stats_store_.rootScope()));
  Logger::Context logging_state(spdlog::level::level_enum::err,
                                "[%Y-%m-%d %T.%e][%t][%l][%n] [%g:%#] %v", lock_, false, false);
  upstream_config_.upstream_protocol_ = Http::CodecType::HTTP2;
  Config::forceRegisterAdsConfigSubscriptionFactory();
  Config::forceRegisterGrpcConfigSubscriptionFactory();
  Config::forceRegisterDeltaGrpcConfigSubscriptionFactory();
  Config::forceRegisterDeltaGrpcCollectionConfigSubscriptionFactory();
  Config::forceRegisterAggregatedGrpcCollectionConfigSubscriptionFactory();
  Config::forceRegisterAdsCollectionConfigSubscriptionFactory();
  Config::forceRegisterGrpcMuxFactory();
  Config::forceRegisterNewGrpcMuxFactory();

  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  auto* common_tls_context = tls_context.mutable_common_tls_context();
  common_tls_context->add_alpn_protocols(Http::Utility::AlpnNames::get().Http2);
  auto* tls_cert = common_tls_context->add_tls_certificates();
  tls_cert->mutable_certificate_chain()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcert.pem"));
  tls_cert->mutable_private_key()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamkey.pem"));
  auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
      tls_context, factory_context_);
  auto context = std::make_unique<Extensions::TransportSockets::Tls::ServerSslSocketFactory>(
      std::move(cfg), context_manager_, *stats_store_.rootScope(), std::vector<std::string>{});
  xds_upstream_ = std::make_unique<FakeUpstream>(std::move(context), 0, version_, upstream_config_);
}

std::string XdsTestServer::getHost() const {
  return Network::Test::getLoopbackAddressUrlString(version_);
}

int XdsTestServer::getPort() const {
  ASSERT(xds_upstream_);
  return xds_upstream_->localAddress()->ip()->port();
}

void XdsTestServer::start() {
  AssertionResult result = xds_upstream_->waitForHttpConnection(*dispatcher_, xds_connection_);
  RELEASE_ASSERT(result, result.message());
  result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
  RELEASE_ASSERT(result, result.message());
  xds_stream_->startGrpcStream();
}

void XdsTestServer::send(const envoy::service::discovery::v3::DiscoveryResponse& response) {
  ASSERT(xds_stream_);
  xds_stream_->sendGrpcMessage(response);
}

void XdsTestServer::shutdown() {
  if (xds_connection_ != nullptr) {
    AssertionResult result = xds_connection_->close();
    RELEASE_ASSERT(result, result.message());
    result = xds_connection_->waitForDisconnect();
    RELEASE_ASSERT(result, result.message());
    xds_connection_.reset();
  }
}

} // namespace Envoy
