#include <memory>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"

#include "source/common/config/api_version.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/common/network/utility.h"
#include "source/common/tls/client_ssl_socket.h"
#include "source/common/tls/context_manager_impl.h"
#include "source/extensions/filters/listener/tls_inspector/tls_inspector.h"

#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/utility.h"
#include "test/mocks/secret/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class LargeBufferListenerFilter : public Network::ListenerFilter {
public:
  static constexpr int BUFFER_SIZE = 512;
  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks&) override {
    ENVOY_LOG_MISC(debug, "LargeBufferListenerFilter::onAccept");
    return Network::FilterStatus::StopIteration;
  }

  // this needs to be smaller than the client hello, but larger than tls inspector's initial read
  // buffer size.
  size_t maxReadBytes() const override { return BUFFER_SIZE; }

  Network::FilterStatus onData(Network::ListenerFilterBuffer& buffer) override {
    auto raw_slice = buffer.rawSlice();
    ENVOY_LOG_MISC(debug, "LargeBufferListenerFilter::onData: recv: {}", raw_slice.len_);
    return Network::FilterStatus::Continue;
  }
};

class LargeBufferListenerFilterConfigFactory
    : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message&,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext&) override {
    return [listener_filter_matcher](Network::ListenerFilterManager& filter_manager) -> void {
      filter_manager.addAcceptFilter(listener_filter_matcher,
                                     std::make_unique<LargeBufferListenerFilter>());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::Protobuf::Struct()};
  }

  std::string name() const override {
    // This fake original_dest should be used only in integration test!
    return "envoy.filters.listener.large_buffer";
  }
};
static Registry::RegisterFactory<LargeBufferListenerFilterConfigFactory,
                                 Server::Configuration::NamedListenerFilterConfigFactory>
    register_;

class TlsInspectorIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public BaseIntegrationTest {
public:
  TlsInspectorIntegrationTest()
      : BaseIntegrationTest(GetParam(), ConfigHelper::baseConfig() + R"EOF(
    filter_chains:
      filters:
       -  name: envoy.filters.network.echo
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.echo.v3.Echo
)EOF") {}

  ~TlsInspectorIntegrationTest() override = default;
  std::string appendMatcher(const std::string& listener_filter, bool disabled) {
    if (disabled) {
      return listener_filter +
             R"EOF(
filter_disabled:
  any_match: true
)EOF";
    } else {
      return listener_filter +
             R"EOF(
filter_disabled:
  not_match:
    any_match: true
)EOF";
    }
  }

  void initializeWithTlsInspector(bool ssl_client, const std::string& log_format,
                                  absl::optional<bool> listener_filter_disabled = absl::nullopt,
                                  bool enable_ja3_fingerprinting = false,
                                  bool enable_ja4_fingerprinting = false) {
    std::string tls_inspector_config =
        ConfigHelper::tlsInspectorFilter(enable_ja3_fingerprinting, enable_ja4_fingerprinting);
    if (listener_filter_disabled.has_value()) {
      tls_inspector_config = appendMatcher(tls_inspector_config, listener_filter_disabled.value());
    }
    initializeWithTlsInspector(ssl_client, log_format, tls_inspector_config);
  }

  void initializeWithTlsInspector(bool ssl_client, const std::string& log_format,
                                  const std::string& tls_inspector_config) {
    config_helper_.renameListener("echo");
    config_helper_.addListenerFilter(tls_inspector_config);

    config_helper_.addConfigModifier([ssl_client](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      if (ssl_client) {
        auto* filter_chain =
            bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
        auto* alpn = filter_chain->mutable_filter_chain_match()->add_application_protocols();
        *alpn = "envoyalpn";
      }
      auto* timeout = bootstrap.mutable_static_resources()
                          ->mutable_listeners(0)
                          ->mutable_listener_filters_timeout();
      timeout->MergeFrom(ProtobufUtil::TimeUtil::MillisecondsToDuration(1000));
      bootstrap.mutable_static_resources()
          ->mutable_listeners(0)
          ->set_continue_on_listener_filters_timeout(true);
    });
    if (ssl_client) {
      config_helper_.addSslConfig();
    }

    useListenerAccessLog(log_format);
    BaseIntegrationTest::initialize();

    context_manager_ = std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
        server_factory_context_);
  }

  void initializeWithTlsInspectorWithLargeBufferFilter() {
    config_helper_.renameListener("echo");
    // note that initial_read_buffer_size should be smaller than the
    // LargeBufferListenerFilter::BUFFER_SIZE for the test scenario to be effective.
    config_helper_.addListenerFilter(R"EOF(
name: "envoy.filters.listener.tls_inspector"
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.listener.tls_inspector.v3.TlsInspector
  initial_read_buffer_size: 256
)EOF");
    // filters are prepended, so this filter will be the first one.
    config_helper_.addListenerFilter(R"EOF(
name: "envoy.filters.listener.large_buffer"
typed_config:
  "@type": type.googleapis.com/google.protobuf.Struct
)EOF");

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* timeout = bootstrap.mutable_static_resources()
                          ->mutable_listeners(0)
                          ->mutable_listener_filters_timeout();
      timeout->MergeFrom(ProtobufUtil::TimeUtil::MillisecondsToDuration(1000));
      bootstrap.mutable_static_resources()
          ->mutable_listeners(0)
          ->set_continue_on_listener_filters_timeout(true);
    });
    BaseIntegrationTest::initialize();

    context_manager_ = std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
        server_factory_context_);
  }

  void setupConnections(bool listener_filter_disabled, bool expect_connection_open, bool ssl_client,
                        const std::string& log_format = "%RESPONSE_CODE_DETAILS%",
                        const Ssl::ClientSslTransportOptions& ssl_options = {},
                        const std::string& curves_list = "", bool enable_ja3_fingerprinting = false,
                        bool enable_ja4_fingerprinting = false) {
    initializeWithTlsInspector(ssl_client, log_format, listener_filter_disabled,
                               enable_ja3_fingerprinting, enable_ja4_fingerprinting);

    // Set up the SSL client.
    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("echo"));
    context_ = Ssl::createClientSslTransportSocketFactory(ssl_options, *context_manager_, *api_);
    Network::TransportSocketPtr transport_socket;
    if (ssl_client) {
      transport_socket =
          context_->createTransportSocket(std::make_shared<Network::TransportSocketOptionsImpl>(
                                              absl::string_view(""), std::vector<std::string>(),
                                              std::vector<std::string>{"envoyalpn"}),
                                          nullptr);

      if (!curves_list.empty()) {
        auto ssl_socket =
            dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
        ASSERT(ssl_socket != nullptr);
        SSL_set1_curves_list(ssl_socket->rawSslForTest(), curves_list.c_str());
      }
    } else {
      auto transport_socket_factory = std::make_unique<Network::RawBufferSocketFactory>();
      transport_socket = transport_socket_factory->createTransportSocket(nullptr, nullptr);
    }
    client_ =
        dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                            std::move(transport_socket), nullptr, nullptr);
    client_->addConnectionCallbacks(connect_callbacks_);
    client_->connect();
    while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }

    if (expect_connection_open) {
      ASSERT(connect_callbacks_.connected());
      ASSERT_FALSE(connect_callbacks_.closed());
    } else {
      ASSERT_FALSE(connect_callbacks_.connected());
      ASSERT(connect_callbacks_.closed());
    }
  }

  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Network::UpstreamTransportSocketFactoryPtr context_;
  ConnectionStatusCallbacks connect_callbacks_;
  testing::NiceMock<Secret::MockSecretManager> secret_manager_;
  Network::ClientConnectionPtr client_;
};

// Each listener filter is enabled by default.
TEST_P(TlsInspectorIntegrationTest, AllListenerFiltersAreEnabledByDefault) {
  setupConnections(/*listener_filter_disabled=*/false, /*expect_connection_open=*/true,
                   /*ssl_client=*/true);
  client_->close(Network::ConnectionCloseType::NoFlush);
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_), testing::Eq("-"));
}

// The tls_inspector is disabled. The ALPN won't be sniffed out and no filter chain is matched.
TEST_P(TlsInspectorIntegrationTest, DisabledTlsInspectorFailsFilterChainFind) {
  setupConnections(/*listener_filter_disabled=*/true, /*expect_connection_open=*/false,
                   /*ssl_client=*/true);
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_),
              testing::Eq(StreamInfo::ResponseCodeDetails::get().FilterChainNotFound));
}

// trigger the tls inspect filter timeout, and continue create new connection after timeout
TEST_P(TlsInspectorIntegrationTest, ContinueOnListenerTimeout) {
  setupConnections(/*listener_filter_disabled=*/false, /*expect_connection_open=*/true,
                   /*ssl_client=*/false);
  // The listener filter will not process the following data but will only wait for 1 second
  // to timeout and then fall over to another listener filter chain.
  Buffer::OwnedImpl buffer("fake data");
  client_->write(buffer, false);
  // The timeout is set as one seconds, advance 2 seconds to trigger the timeout.
  timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(2000));
  client_->close(Network::ConnectionCloseType::NoFlush);
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_), testing::Eq("-"));
}

TEST_P(TlsInspectorIntegrationTest, TlsInspectorMetadataPopulatedInAccessLog) {
  initializeWithTlsInspector(
      /*ssl_client=*/false,
      /*log_format=*/"%DYNAMIC_METADATA(envoy.filters.listener.tls_inspector:failure_reason)%",
      false, false, false);
  Network::Address::InstanceConstSharedPtr address =
      Ssl::getSslAddress(version_, lookupPort("echo"));
  context_ =
      Ssl::createClientSslTransportSocketFactory(/*ssl_options=*/{}, *context_manager_, *api_);
  auto transport_socket_factory = std::make_unique<Network::RawBufferSocketFactory>();
  Network::TransportSocketPtr transport_socket =
      transport_socket_factory->createTransportSocket(nullptr, nullptr);
  client_ = dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                                std::move(transport_socket), nullptr, nullptr);
  std::shared_ptr<WaitForPayloadReader> payload_reader =
      std::make_shared<WaitForPayloadReader>(*dispatcher_);
  client_->addReadFilter(payload_reader);
  client_->addConnectionCallbacks(connect_callbacks_);
  client_->connect();
  Buffer::OwnedImpl buffer("fake data");
  client_->write(buffer, false);
  while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  // The timeout is set as one seconds, advance 2 seconds to trigger the timeout.
  timeSystem().advanceTimeWaitImpl(std::chrono::milliseconds(2000));
  client_->close(Network::ConnectionCloseType::NoFlush);
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_), testing::Eq("ClientHelloNotDetected"));
}

// The `JA3` fingerprint is correct in the access log.
TEST_P(TlsInspectorIntegrationTest, JA3FingerprintIsSet) {
  // These TLS options will create a client hello message with
  // `JA3` fingerprint:
  //   `771,49199,23-65281-10-11-35-16-13,23,0`
  // MD5 hash:
  //   `71d1f47d1125ac53c3c6a4863c087cfe`
  Ssl::ClientSslTransportOptions ssl_options;
  ssl_options.setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"});
  ssl_options.setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  setupConnections(/*listener_filter_disabled=*/false, /*expect_connection_open=*/true,
                   /*ssl_client=*/true, /*log_format=*/"%TLS_JA3_FINGERPRINT%",
                   /*ssl_options=*/ssl_options, /*curves_list=*/"P-256",
                   /*enable_`ja3`_fingerprinting=*/true);
  client_->close(Network::ConnectionCloseType::NoFlush);

  EXPECT_THAT(waitForAccessLog(listener_access_log_name_),
              testing::Eq("71d1f47d1125ac53c3c6a4863c087cfe"));

  test_server_->waitUntilHistogramHasSamples("tls_inspector.bytes_processed");
  auto bytes_processed_histogram = test_server_->histogram("tls_inspector.bytes_processed");
  EXPECT_EQ(
      TestUtility::readSampleCount(test_server_->server().dispatcher(), *bytes_processed_histogram),
      1);
  EXPECT_EQ(static_cast<int>(TestUtility::readSampleSum(test_server_->server().dispatcher(),
                                                        *bytes_processed_histogram)),
            115);
}

// The `JA4` fingerprint is correct in the access log.
TEST_P(TlsInspectorIntegrationTest, JA4FingerprintIsSet) {
  // These TLS options will create a client hello message with
  // `JA4` fingerprint:
  //   `t12i0107en_f06271c2b022_0f3b2bcde21d`
  Ssl::ClientSslTransportOptions ssl_options;
  ssl_options.setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"});
  ssl_options.setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  setupConnections(/*listener_filter_disabled=*/false, /*expect_connection_open=*/true,
                   /*ssl_client=*/true, /*log_format=*/"%TLS_JA4_FINGERPRINT%",
                   /*ssl_options=*/ssl_options, /*curves_list=*/"P-256",
                   /*enable_`ja3`_fingerprinting=*/false, /*enable_`ja4`_fingerprinting=*/true);
  client_->close(Network::ConnectionCloseType::NoFlush);

  EXPECT_THAT(waitForAccessLog(listener_access_log_name_),
              testing::Eq("t12i0107en_f06271c2b022_0f3b2bcde21d"));

  test_server_->waitUntilHistogramHasSamples("tls_inspector.bytes_processed");
  auto bytes_processed_histogram = test_server_->histogram("tls_inspector.bytes_processed");
  EXPECT_EQ(
      TestUtility::readSampleCount(test_server_->server().dispatcher(), *bytes_processed_histogram),
      1);
  EXPECT_EQ(static_cast<int>(TestUtility::readSampleSum(test_server_->server().dispatcher(),
                                                        *bytes_processed_histogram)),
            115);
}

TEST_P(TlsInspectorIntegrationTest, RequestedBufferSizeCanGrow) {
  const std::string small_initial_buffer_tls_inspector_config = R"EOF(
    name: "envoy.filters.listener.tls_inspector"
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.tls_inspector.v3.TlsInspector
      initial_read_buffer_size: 256
  )EOF";
  initializeWithTlsInspector(true, "%RESPONSE_CODE_DETAILS%",
                             small_initial_buffer_tls_inspector_config);

  Network::Address::InstanceConstSharedPtr address =
      Ssl::getSslAddress(version_, lookupPort("echo"));

  Ssl::ClientSslTransportOptions ssl_options;
  ssl_options.setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"});
  ssl_options.setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  const std::string really_long_sni(absl::StrCat(std::string(240, 'a'), ".foo.com"));
  ssl_options.setSni(really_long_sni);
  context_ = Ssl::createClientSslTransportSocketFactory(ssl_options, *context_manager_, *api_);
  Network::TransportSocketPtr transport_socket = context_->createTransportSocket(
      std::make_shared<Network::TransportSocketOptionsImpl>(
          absl::string_view(""), std::vector<std::string>(), std::vector<std::string>{"envoyalpn"}),
      nullptr);

  client_ = dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                                std::move(transport_socket), nullptr, nullptr);
  client_->addConnectionCallbacks(connect_callbacks_);
  client_->connect();

  while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  ASSERT(connect_callbacks_.connected());
  client_->close(Network::ConnectionCloseType::NoFlush);

  test_server_->waitUntilHistogramHasSamples("tls_inspector.bytes_processed");
  auto bytes_processed_histogram = test_server_->histogram("tls_inspector.bytes_processed");
  EXPECT_EQ(
      TestUtility::readSampleCount(test_server_->server().dispatcher(), *bytes_processed_histogram),
      1);
  EXPECT_EQ(static_cast<int>(TestUtility::readSampleSum(test_server_->server().dispatcher(),
                                                        *bytes_processed_histogram)),
            515);
}

TEST_P(TlsInspectorIntegrationTest, RequestedBufferSizeCanStartBig) {
  initializeWithTlsInspectorWithLargeBufferFilter();

  Network::Address::InstanceConstSharedPtr address =
      Ssl::getSslAddress(version_, lookupPort("echo"));

  Ssl::ClientSslTransportOptions ssl_options;
  ssl_options.setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"});
  ssl_options.setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  const std::string really_long_sni(absl::StrCat(std::string(240, 'a'), ".foo.com"));
  ssl_options.setSni(really_long_sni);
  context_ = Ssl::createClientSslTransportSocketFactory(ssl_options, *context_manager_, *api_);
  Network::TransportSocketPtr transport_socket = context_->createTransportSocket(
      std::make_shared<Network::TransportSocketOptionsImpl>(
          absl::string_view(""), std::vector<std::string>(), std::vector<std::string>{}),
      nullptr);

  client_ = dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                                std::move(transport_socket), nullptr, nullptr);
  client_->addConnectionCallbacks(connect_callbacks_);
  client_->connect();

  while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  client_->close(Network::ConnectionCloseType::NoFlush);

  test_server_->waitUntilHistogramHasSamples("tls_inspector.bytes_processed");
  auto bytes_processed_histogram = test_server_->histogram("tls_inspector.bytes_processed");
  EXPECT_EQ(
      TestUtility::readSampleCount(test_server_->server().dispatcher(), *bytes_processed_histogram),
      1);
  auto bytes_processed = static_cast<int>(
      TestUtility::readSampleSum(test_server_->server().dispatcher(), *bytes_processed_histogram));
  EXPECT_EQ(bytes_processed, 515);
  // Double check that the test is effective by ensuring that the
  // LargeBufferListenerFilter::BUFFER_SIZE is smaller than the client hello.
  EXPECT_GT(bytes_processed, LargeBufferListenerFilter::BUFFER_SIZE);
}

// This test verifies that `JA4` fingerprinting works with a malformed ClientHello that
// should still be valid enough to extract a `JA4` hash
TEST_P(TlsInspectorIntegrationTest, JA4FingerprintWithMalformedClientHello) {
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.mutable_enable_ja4_fingerprinting()->set_value(true);
  config_helper_.renameListener("echo");
  config_helper_.addListenerFilter(ConfigHelper::tlsInspectorFilter(false, true));

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* timeout = bootstrap.mutable_static_resources()
                        ->mutable_listeners(0)
                        ->mutable_listener_filters_timeout();
    timeout->MergeFrom(ProtobufUtil::TimeUtil::MillisecondsToDuration(1000));
    bootstrap.mutable_static_resources()
        ->mutable_listeners(0)
        ->set_continue_on_listener_filters_timeout(true);
  });

  useListenerAccessLog("%TLS_JA4_FINGERPRINT%");
  BaseIntegrationTest::initialize();

  context_manager_ = std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
      server_factory_context_);

  // Set up SSL options with minimal ciphers
  Ssl::ClientSslTransportOptions ssl_options;
  ssl_options.setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"});
  ssl_options.setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  ssl_options.setSni("example.com");

  context_ = Ssl::createClientSslTransportSocketFactory(ssl_options, *context_manager_, *api_);
  Network::Address::InstanceConstSharedPtr address =
      Ssl::getSslAddress(version_, lookupPort("echo"));

  // Force ALPN to contain odd characters
  auto transport_socket = context_->createTransportSocket(
      std::make_shared<Network::TransportSocketOptionsImpl>(
          absl::string_view(""), std::vector<std::string>(), std::vector<std::string>{"h@2"}),
      nullptr);

  // Inject a custom SSL object to test error handling
  auto ssl_socket =
      dynamic_cast<Extensions::TransportSockets::Tls::SslSocket*>(transport_socket.get());
  ASSERT(ssl_socket != nullptr);
  // Set a curve that's not typically used to test specific code paths
  SSL_set1_curves_list(ssl_socket->rawSslForTest(), "secp224r1");

  // Connect to the server
  client_ = dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                                std::move(transport_socket), nullptr, nullptr);
  client_->addConnectionCallbacks(connect_callbacks_);
  client_->connect();

  while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  client_->close(Network::ConnectionCloseType::NoFlush);

  // The connection should be successful and we should get a valid `JA4` fingerprint in the logs
  EXPECT_THAT(waitForAccessLog(listener_access_log_name_),
              testing::Not(testing::Eq("t00d000000_000000000000_000000000000")));
}

// This test verifies `JA4` fingerprinting behavior with non-standard ALPN protocols
TEST_P(TlsInspectorIntegrationTest, JA4FingerprintWithSpecialALPN) {
  envoy::extensions::filters::listener::tls_inspector::v3::TlsInspector proto_config;
  proto_config.mutable_enable_ja4_fingerprinting()->set_value(true);
  config_helper_.renameListener("echo");
  config_helper_.addListenerFilter(ConfigHelper::tlsInspectorFilter(false, true));

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* timeout = bootstrap.mutable_static_resources()
                        ->mutable_listeners(0)
                        ->mutable_listener_filters_timeout();
    timeout->MergeFrom(ProtobufUtil::TimeUtil::MillisecondsToDuration(1000));
    bootstrap.mutable_static_resources()
        ->mutable_listeners(0)
        ->set_continue_on_listener_filters_timeout(true);
  });

  useListenerAccessLog("%TLS_JA4_FINGERPRINT%");
  BaseIntegrationTest::initialize();

  context_manager_ = std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
      server_factory_context_);

  // Set up SSL options with minimal ciphers
  Ssl::ClientSslTransportOptions ssl_options;
  ssl_options.setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"});
  ssl_options.setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  ssl_options.setSni("example.com");

  context_ = Ssl::createClientSslTransportSocketFactory(ssl_options, *context_manager_, *api_);
  Network::Address::InstanceConstSharedPtr address =
      Ssl::getSslAddress(version_, lookupPort("echo"));

  // Use non-alphanumeric ALPN protocols to test special character handling
  auto transport_socket =
      context_->createTransportSocket(std::make_shared<Network::TransportSocketOptionsImpl>(
                                          absl::string_view(""), std::vector<std::string>(),
                                          std::vector<std::string>{"*test*", "!special!"}),
                                      nullptr);

  client_ = dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                                std::move(transport_socket), nullptr, nullptr);
  client_->addConnectionCallbacks(connect_callbacks_);
  client_->connect();

  while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  client_->close(Network::ConnectionCloseType::NoFlush);

  // The connection should be successful, and we should get a valid `JA4` fingerprint
  // with hex-encoded special characters in the logs
  std::string log_content = waitForAccessLog(listener_access_log_name_);
  EXPECT_THAT(log_content, testing::Not(testing::Eq("t00d000000_000000000000_000000000000")));
}

// This test verifies `JA4` fingerprinting with minimal extensions
TEST_P(TlsInspectorIntegrationTest, JA4FingerprintWithMinimalExtensions) {
  config_helper_.renameListener("echo");
  config_helper_.addListenerFilter(ConfigHelper::tlsInspectorFilter(false, true));

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* timeout = bootstrap.mutable_static_resources()
                        ->mutable_listeners(0)
                        ->mutable_listener_filters_timeout();
    timeout->MergeFrom(ProtobufUtil::TimeUtil::MillisecondsToDuration(1000));
    bootstrap.mutable_static_resources()
        ->mutable_listeners(0)
        ->set_continue_on_listener_filters_timeout(true);
  });

  useListenerAccessLog("%TLS_JA4_FINGERPRINT%");
  BaseIntegrationTest::initialize();

  context_manager_ = std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
      server_factory_context_);

  // Configure with minimal extensions
  Ssl::ClientSslTransportOptions ssl_options;
  ssl_options.setCipherSuites({"ECDHE-RSA-AES128-GCM-SHA256"});
  ssl_options.setTlsVersion(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2);
  // No SNI set

  context_ = Ssl::createClientSslTransportSocketFactory(ssl_options, *context_manager_, *api_);
  Network::Address::InstanceConstSharedPtr address =
      Ssl::getSslAddress(version_, lookupPort("echo"));

  auto transport_socket = context_->createTransportSocket(nullptr, nullptr);

  client_ = dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                                std::move(transport_socket), nullptr, nullptr);
  client_->addConnectionCallbacks(connect_callbacks_);
  client_->connect();

  while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  client_->close(Network::ConnectionCloseType::NoFlush);

  // The connection should be successful, and we should get a `JA4` fingerprint that indicates
  // no SNI (i character) in the logs
  std::string log_content = waitForAccessLog(listener_access_log_name_);
  EXPECT_THAT(log_content, testing::HasSubstr("i"));
}

// Test that SNI is captured and available in access logs even when the TLS connection
// fails.
TEST_P(TlsInspectorIntegrationTest, SniCapturedOnFilterChainNotFound) {
  const std::string test_sni = "test.example.com";
  initializeWithTlsInspector(/*ssl_client=*/true,
                             /*log_format=*/"%REQUESTED_SERVER_NAME%|%RESPONSE_CODE_DETAILS%",
                             /*listener_filter_disabled=*/absl::nullopt);

  // Set up the SSL client with an SNI that won't match any filter chain.
  Network::Address::InstanceConstSharedPtr address =
      Ssl::getSslAddress(version_, lookupPort("echo"));

  Ssl::ClientSslTransportOptions ssl_options;
  ssl_options.setSni(test_sni);
  context_ = Ssl::createClientSslTransportSocketFactory(ssl_options, *context_manager_, *api_);

  // Use ALPN that doesn't match the filter chain.
  Network::TransportSocketPtr transport_socket = context_->createTransportSocket(
      std::make_shared<Network::TransportSocketOptionsImpl>(
          absl::string_view(""), std::vector<std::string>(), std::vector<std::string>{"nomatch"}),
      nullptr);

  client_ = dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                                std::move(transport_socket), nullptr, nullptr);
  client_->addConnectionCallbacks(connect_callbacks_);
  client_->connect();

  while (!connect_callbacks_.connected() && !connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Connection should fail due to filter chain not found.
  ASSERT_FALSE(connect_callbacks_.connected());
  ASSERT(connect_callbacks_.closed());

  // Verify that even though the connection failed, the SNI was captured and is in the access log.
  std::string log_content = waitForAccessLog(listener_access_log_name_);
  EXPECT_THAT(log_content, testing::HasSubstr(test_sni));
  EXPECT_THAT(log_content,
              testing::HasSubstr(StreamInfo::ResponseCodeDetails::get().FilterChainNotFound));
}

INSTANTIATE_TEST_SUITE_P(IpVersions, TlsInspectorIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);
} // namespace
} // namespace Envoy
