#include "test/extensions/transport_sockets/tls/cert_validator/spiffe/spiffe_validator_integration_test.h"

#include <memory>

#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/common/tls/context_manager_impl.h"
#include "source/common/tls/ssl_handshaker.h"

#include "test/integration/integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Ssl {

void SslSPIFFECertValidatorIntegrationTest::initialize() {
  config_helper_.addSslConfig(ConfigHelper::ServerSslOptions()
                                  .setRsaCert(true)
                                  .setTlsV13(true)
                                  .setRsaCertOcspStaple(false)
                                  .setCustomValidatorConfig(custom_validator_config_)
                                  .setSanMatchers(san_matchers_)
                                  .setAllowExpiredCertificate(allow_expired_cert_));
  config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* filter_chain =
        bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    RELEASE_ASSERT(
        filter_chain->mutable_transport_socket()->mutable_typed_config()->UnpackTo(&tls_context),
        "failed to unpack DownstreamTlsContext for SPIFFE validator test listener");
    if (require_client_certificate_.has_value()) {
      tls_context.mutable_require_client_certificate()->set_value(*require_client_certificate_);
    } else {
      tls_context.clear_require_client_certificate();
    }
    std::ignore =
        filter_chain->mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_context);
  });
  HttpIntegrationTest::initialize();

  context_manager_ = std::make_unique<Extensions::TransportSockets::Tls::ContextManagerImpl>(
      server_factory_context_);
  registerTestServerPorts({"http"});
}

void SslSPIFFECertValidatorIntegrationTest::TearDown() {
  HttpIntegrationTest::cleanupUpstreamAndDownstream();
  codec_client_.reset();
  context_manager_.reset();
}

Network::ClientConnectionPtr SslSPIFFECertValidatorIntegrationTest::makeSslClientConnection(
    const ClientSslTransportOptions& options, bool use_expired = false,
    std::optional<std::string> workload_trust_domain = {}) {
  ClientSslTransportOptions modified_options{options};
  modified_options.setTlsVersion(tls_version_);
  modified_options.use_expired_spiffe_cert_ = use_expired;
  modified_options.setCustomCertValidatorConfig(client_validator_config_);

  Network::Address::InstanceConstSharedPtr address = getSslAddress(version_, lookupPort("http"));
  auto client_transport_socket_factory_ptr = createClientSslTransportSocketFactory(
      modified_options, *context_manager_, *api_, &server_factory_context_.serverScope());
  Network::TransportSocketOptionsConstSharedPtr socket_options;
  if (workload_trust_domain) {
    StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
    filter_state.setData("envoy.tls.cert_validator.spiffe.workload_trust_domain",
                         std::make_shared<Router::StringAccessorImpl>(*workload_trust_domain),
                         StreamInfo::FilterState::LifeSpan::Connection,
                         StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
    socket_options = Network::TransportSocketOptionsUtility::fromFilterState(filter_state);
  }

  return dispatcher_->createClientConnection(
      address, Network::Address::InstanceConstSharedPtr(),
      client_transport_socket_factory_ptr->createTransportSocket(socket_options, nullptr), nullptr,
      nullptr);
}

void SslSPIFFECertValidatorIntegrationTest::checkVerifyErrorCouter(uint64_t value) {
  Stats::CounterSharedPtr counter =
      test_server_->counter(listenerStatPrefix("ssl.fail_verify_error"));
  EXPECT_EQ(value, counter->value());
  counter->reset();
}

void SslSPIFFECertValidatorIntegrationTest::addStringMatcher(
    const envoy::type::matcher::v3::StringMatcher& matcher) {
  san_matchers_.emplace_back();
  *san_matchers_.back().mutable_matcher() = matcher;
  san_matchers_.back().set_san_type(
      envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::DNS);
  san_matchers_.emplace_back();
  *san_matchers_.back().mutable_matcher() = matcher;
  san_matchers_.back().set_san_type(
      envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::URI);
  san_matchers_.emplace_back();
  *san_matchers_.back().mutable_matcher() = matcher;
  san_matchers_.back().set_san_type(
      envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::EMAIL);
  san_matchers_.emplace_back();
  *san_matchers_.back().mutable_matcher() = matcher;
  san_matchers_.back().set_san_type(
      envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::IP_ADDRESS);
}

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientVersions, SslSPIFFECertValidatorIntegrationTest,
    testing::Combine(
        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
        testing::Values(envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2,
                        envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3)),
    SslSPIFFECertValidatorIntegrationTest::ipClientVersionTestParamsToString);

// clientcert.pem's san is "spiffe://lyft.com/frontend-team" so it should be accepted.
TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorAccepted) {
  auto typed_conf = new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
  )EOF"),
                            *typed_conf);

  custom_validator_config_ = typed_conf;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkVerifyErrorCouter(0);
}

TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorAcceptsTlsAndMtls) {
  auto typed_conf = new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  allow_optional_client_certificate: true
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
  )EOF"),
                            *typed_conf);

  custom_validator_config_ = typed_conf;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    ClientSslTransportOptions options;
    options.no_cert_ = true;
    return makeSslClientConnection(options);
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkVerifyErrorCouter(0);

  codec_client_->close();
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  codec_client_ = makeHttpConnection(makeSslClientConnection({}));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 1024, default_response_headers_, 512);
  checkSimpleRequestSuccess(1024, 512, response.get());
  checkVerifyErrorCouter(0);
}

// The listeners share certificates and ticket keys, but differ in client authentication policy.
TEST_P(SslSPIFFECertValidatorIntegrationTest, AnonymousSessionCannotResumeWithRequiredCertificate) {
  auto typed_conf = new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  allow_optional_client_certificate: true
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
)EOF"),
                            *typed_conf);
  custom_validator_config_ = typed_conf;
  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listeners = bootstrap.mutable_static_resources()->mutable_listeners();
    auto* optional_listener = listeners->Mutable(0);
    auto* transport_socket =
        optional_listener->mutable_filter_chains(0)->mutable_transport_socket();
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    ASSERT_TRUE(transport_socket->typed_config().UnpackTo(&tls_context));
    tls_context.mutable_session_ticket_keys()->add_keys()->set_filename(
        TestEnvironment::substitute("{{ test_rundir }}/test/common/tls/test_data/ticket_key_a"));
    std::ignore = transport_socket->mutable_typed_config()->PackFrom(tls_context);
    auto* required_listener = listeners->Add();
    *required_listener = *optional_listener;
    required_listener->set_name("required");
    required_listener->set_stat_prefix("required");
    tls_context.mutable_require_client_certificate()->set_value(true);
    std::ignore = required_listener->mutable_filter_chains(0)
                      ->mutable_transport_socket()
                      ->mutable_typed_config()
                      ->PackFrom(tls_context);
  });
  initialize();
  registerTestServerPorts({"http", "required"});

  ClientSslTransportOptions options;
  options.no_cert_ = true;
  options.setTlsVersion(tls_version_);
  auto client_factory = createClientSslTransportSocketFactory(
      options, *context_manager_, *api_, &server_factory_context_.serverScope());
  bssl::UniquePtr<SSL_SESSION> session;
  static const int session_index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);

  auto connect = [&](uint32_t port) -> Network::ClientConnectionPtr {
    auto conn = dispatcher_->createClientConnection(
        getSslAddress(version_, port), Network::Address::InstanceConstSharedPtr(),
        client_factory->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
    auto* handshaker = dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
        conn->ssl().get());
    SSL* ssl = handshaker->ssl();
    SSL_set_ex_data(ssl, session_index, &session);
    // TLS 1.3 tickets arrive after the handshake and must be captured through this callback.
    SSL_CTX_set_session_cache_mode(SSL_get_SSL_CTX(ssl), SSL_SESS_CACHE_CLIENT);
    SSL_CTX_sess_set_new_cb(SSL_get_SSL_CTX(ssl), [](SSL* ssl, SSL_SESSION* new_session) -> int {
      auto* session =
          static_cast<bssl::UniquePtr<SSL_SESSION>*>(SSL_get_ex_data(ssl, session_index));
      session->reset(new_session);
      return 1;
    });
    if (session) {
      // OpenSSL consumes TLS 1.3 sessions on use. Resume a copy so the captured ticket remains
      // available for the required listener after checking it on the optional listener.
      uint8_t* bytes = nullptr;
      size_t length = 0;
      EXPECT_EQ(1, SSL_SESSION_to_bytes(session.get(), &bytes, &length));
      bssl::UniquePtr<uint8_t> owned_bytes(bytes);
      bssl::UniquePtr<SSL_SESSION> resumed_session(
          SSL_SESSION_from_bytes(bytes, length, SSL_get_SSL_CTX(ssl)));
      EXPECT_NE(nullptr, resumed_session);
      EXPECT_EQ(1, SSL_set_session(ssl, resumed_session.get()));
    }
    return conn;
  };
  codec_client_ = makeHttpConnection(connect(lookupPort("http")));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 1024, default_response_headers_, 512);
  checkSimpleRequestSuccess(1024, 512, response.get());
  ASSERT_TRUE(session);
  ASSERT_TRUE(SSL_SESSION_is_resumable(session.get()));
  codec_client_->close();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  // Prove the ticket works on the original optional listener.
  codec_client_ = makeHttpConnection(connect(lookupPort("http")));
  response =
      sendRequestAndWaitForResponse(default_request_headers_, 1024, default_response_headers_, 512);
  checkSimpleRequestSuccess(1024, 512, response.get());
  auto* handshaker = dynamic_cast<const Extensions::TransportSockets::Tls::SslHandshakerImpl*>(
      codec_client_->connection()->ssl().get());
  ASSERT_TRUE(SSL_session_reused(handshaker->ssl()));
  ASSERT_TRUE(SSL_SESSION_is_resumable(session.get()));
  codec_client_->close();
  ASSERT_TRUE(codec_client_->waitForDisconnect());

  auto conn = connect(lookupPort("required"));
  if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2) {
    auto codec = makeRawHttpConnection(std::move(conn), std::nullopt);
    EXPECT_FALSE(codec->connected());
  } else {
    auto codec = makeHttpConnection(std::move(conn));
    ASSERT_TRUE(codec->waitForDisconnect());
    codec->close();
  }
  EXPECT_EQ(0, test_server_->counter("listener.required.ssl.session_reused")->value());
}

void SslSPIFFECertValidatorIntegrationTest::testClientCertificateRequired(
    bool allow_optional_client_certificate) {
  auto typed_conf = new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(
      TestEnvironment::substitute(fmt::format(
          R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
{}  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{{{ test_rundir }}}}/test/config/integration/certs/cacert.pem"
  )EOF",
          allow_optional_client_certificate ? "  allow_optional_client_certificate: true\n" : "")),
      *typed_conf);

  custom_validator_config_ = typed_conf;
  initialize();
  ClientSslTransportOptions options;
  options.no_cert_ = true;
  auto conn = makeSslClientConnection(options);
  if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2) {
    auto codec = makeRawHttpConnection(std::move(conn), std::nullopt);
    EXPECT_FALSE(codec->connected());
  } else {
    auto codec = makeHttpConnection(std::move(conn));
    ASSERT_TRUE(codec->waitForDisconnect());
    codec->close();
  }
}

TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorRequiresClientCertificate) {
  require_client_certificate_ = true;
  testClientCertificateRequired(true);
}

TEST_P(SslSPIFFECertValidatorIntegrationTest,
       ServerRsaSPIFFEValidatorRequiresClientCertificateByDefault) {
  testClientCertificateRequired(false);
}

TEST_P(SslSPIFFECertValidatorIntegrationTest,
       ServerRsaSPIFFEValidatorRequiresClientCertificateWhenUnset) {
  require_client_certificate_ = std::nullopt;
  testClientCertificateRequired(false);
}

// Client certificate has expired but the config allows expired certificates, so this case should
// be accepted.
TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorExpiredButAccepted) {
  auto typed_conf = new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  )EOF"),
                            *typed_conf);
  custom_validator_config_ = typed_conf;
  allow_expired_cert_ = true;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    const bool use_expired_certificate = true;
    return makeSslClientConnection({}, use_expired_certificate);
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkVerifyErrorCouter(0);
}

// clientcert.pem has "spiffe://lyft.com/frontend-team" URI SAN, so this case should be accepted.
TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorSANMatch) {
  auto typed_conf = new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
  )EOF"),
                            *typed_conf);
  custom_validator_config_ = typed_conf;

  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_prefix("spiffe://lyft.com/");
  addStringMatcher(matcher);

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkVerifyErrorCouter(0);
}

// clientcert.pem has "spiffe://lyft.com/frontend-team" URI SAN, so this case should be rejected.
TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorSANNotMatch) {
  auto typed_conf = new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  allow_optional_client_certificate: true
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
  )EOF"),
                            *typed_conf);
  custom_validator_config_ = typed_conf;

  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_prefix("spiffe://example.com/");
  // The cert has "DNS.1 = lyft.com" but SPIFFE validator must ignore SAN types other than URI.
  matcher.set_prefix("www.lyft.com");
  addStringMatcher(matcher);
  initialize();
  auto conn = makeSslClientConnection({});
  if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2) {
    auto codec = makeRawHttpConnection(std::move(conn), std::nullopt);
    EXPECT_FALSE(codec->connected());
  } else {
    auto codec = makeHttpConnection(std::move(conn));
    ASSERT_TRUE(codec->waitForDisconnect());
    codec->close();
  }
  Stats::CounterSharedPtr counter =
      test_server_->counter(listenerStatPrefix("ssl.fail_verify_san"));
  EXPECT_EQ(1u, counter->value());
  counter->reset();
}

// Client certificate has expired and the config does NOT allow expired certificates, so this case
// should be rejected.
TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorExpiredAndRejected) {
  auto typed_conf = new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  allow_optional_client_certificate: true
  trust_domains:
    - name: example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/ca_cert.pem"
  )EOF"),
                            *typed_conf);
  custom_validator_config_ = typed_conf;
  // Explicitly specify "false" just in case and for clarity.
  allow_expired_cert_ = false;
  initialize();
  auto conn = makeSslClientConnection({});
  if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2) {
    auto codec = makeRawHttpConnection(std::move(conn), std::nullopt);
    EXPECT_FALSE(codec->connected());
  } else {
    auto codec = makeHttpConnection(std::move(conn));
    ASSERT_TRUE(codec->waitForDisconnect());
    codec->close();
  }
  checkVerifyErrorCouter(1);
}

// clientcert.pem's san is "spiffe://lyft.com/frontend-team" so it should be rejected.
TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorRejected1) {
  auto typed_conf = new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  allow_optional_client_certificate: true
  trust_domains:
    - name: example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
  )EOF"),
                            *typed_conf);
  custom_validator_config_ = typed_conf;
  initialize();
  auto conn = makeSslClientConnection({});
  if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2) {
    auto codec = makeRawHttpConnection(std::move(conn), std::nullopt);
    EXPECT_FALSE(codec->connected());
  } else {
    auto codec = makeHttpConnection(std::move(conn));
    ASSERT_TRUE(codec->waitForDisconnect());
    codec->close();
  }
  checkVerifyErrorCouter(1);
}

// clientcert.pem's san is "spiffe://lyft.com/frontend-team" but the corresponding trust bundle
// does not match with the client cert. So this should also be rejected.
TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorRejected2) {
  auto typed_conf = new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  allow_optional_client_certificate: true
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/common/tls/test_data/fake_ca_cert.pem"
    - name: example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
  )EOF"),
                            *typed_conf);
  custom_validator_config_ = typed_conf;
  initialize();
  auto conn = makeSslClientConnection({});
  if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2) {
    auto codec = makeRawHttpConnection(std::move(conn), std::nullopt);
    EXPECT_FALSE(codec->connected());
  } else {
    auto codec = makeHttpConnection(std::move(conn));
    ASSERT_TRUE(codec->waitForDisconnect());
    codec->close();
  }
  checkVerifyErrorCouter(1);
}

TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorAcceptedWorkloadTrustDomain) {
  auto typed_conf = new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
      workload_trust_domain: mydomain.org
  )EOF"),
                            *typed_conf);
  custom_validator_config_ = typed_conf;
  config_helper_.addListenerFilter(R"EOF(
name: set_workload_trust_domain
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.listener.set_filter_state.v3.Config
  on_accept:
  - object_key: envoy.tls.cert_validator.spiffe.workload_trust_domain
    factory_key: envoy.string
    format_string:
      text_format_source:
        inline_string: mydomain.org
)EOF");
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
  checkVerifyErrorCouter(0);
}

TEST_P(SslSPIFFECertValidatorIntegrationTest, ServerRsaSPIFFEValidatorRejectedWorkloadTrustDomain) {
  auto typed_conf = new envoy::config::core::v3::TypedExtensionConfig();
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  allow_optional_client_certificate: true
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
      workload_trust_domain: mydomain.org
  )EOF"),
                            *typed_conf);
  custom_validator_config_ = typed_conf;
  initialize();
  auto conn = makeSslClientConnection({});
  if (tls_version_ == envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_2) {
    auto codec = makeRawHttpConnection(std::move(conn), std::nullopt);
    EXPECT_FALSE(codec->connected());
  } else {
    auto codec = makeHttpConnection(std::move(conn));
    ASSERT_TRUE(codec->waitForDisconnect());
    codec->close();
  }
  checkVerifyErrorCouter(1);
}

TEST_P(SslSPIFFECertValidatorIntegrationTest, ClientSPIFFEValidatorAccepted) {
  envoy::config::core::v3::TypedExtensionConfig typed_conf;
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
  )EOF"),
                            typed_conf);
  client_validator_config_ = &typed_conf;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({});
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

TEST_P(SslSPIFFECertValidatorIntegrationTest, ClientSPIFFEValidatorRejected) {
  envoy::config::core::v3::TypedExtensionConfig typed_conf;
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: example.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
  )EOF"),
                            typed_conf);
  client_validator_config_ = &typed_conf;
  initialize();
  auto conn = makeSslClientConnection({});
  auto codec = makeRawHttpConnection(std::move(conn), std::nullopt);
  EXPECT_FALSE(codec->connected());
}

TEST_P(SslSPIFFECertValidatorIntegrationTest, ClientSPIFFEValidatorAcceptedWorkloadTrustDomain) {
  envoy::config::core::v3::TypedExtensionConfig typed_conf;
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
      workload_trust_domain: mydomain.org
  )EOF"),
                            typed_conf);

  client_validator_config_ = &typed_conf;
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection({}, false, "mydomain.org");
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

TEST_P(SslSPIFFECertValidatorIntegrationTest, ClientSPIFFEValidatorRejectedWorkloadTrustDomain) {
  envoy::config::core::v3::TypedExtensionConfig typed_conf;
  TestUtility::loadFromYaml(TestEnvironment::substitute(R"EOF(
name: envoy.tls.cert_validator.spiffe
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.SPIFFECertValidatorConfig
  trust_domains:
    - name: lyft.com
      trust_bundle:
        filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
      workload_trust_domain: mydomain.org
  )EOF"),
                            typed_conf);

  client_validator_config_ = &typed_conf;
  initialize();
  auto conn = makeSslClientConnection({});
  auto codec = makeRawHttpConnection(std::move(conn), std::nullopt);
  EXPECT_FALSE(codec->connected());
}

} // namespace Ssl
} // namespace Envoy
