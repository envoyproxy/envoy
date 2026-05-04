// Integration tests for the dynamic_modules cert validator. These tests run a real Envoy
// server with a TLS listener whose validator is provided by a dynamically loaded module, and
// drive a TLS handshake from a client cert. Unlike the unit test in this same directory, the
// validator's extern "C" callbacks (set_error_details, set_filter_state, get_filter_state)
// are reached through a dlopen'd .so calling into the host — i.e. through the dynamic linker
// rather than a direct C++ call. That dynamic-linker path is what exercises the strong
// definitions in config.cc end-to-end.
//
// Two test classes:
//
//   * DynamicModulesCertValidatorClientIntegrationTest mounts the validator on the *client*
//     side (validating the server's certificate). The server presents its certificate as
//     part of the TLS handshake, so the client validator's do_verify_cert_chain runs without
//     extra plumbing. This is the simpler wiring and covers the doVerifyCertChain code path.
//
//   * DynamicModulesCertValidatorServerIntegrationTest mounts the validator on the *server*
//     side. This covers addClientValidationContext (which builds the server's TLS
//     CertificateRequest CA list from the configured trusted_ca) and exercises the three
//     extern "C" filter-state / error-details callbacks, which the cert_validator_filter_state
//     module invokes inside do_verify_cert_chain.

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_validator/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls.pb.h"

#include "test/integration/http_integration.h"
#include "test/integration/ssl_utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace DynamicModules {
namespace {

struct CertValidatorIntegrationParam {
  std::string language;
  Network::Address::IpVersion ip_version;
};

// =============================================================================
// Client-side validator: validates the server's certificate. The C/Go/Rust modules
// always return Successful, so the handshake completes if our doVerifyCertChain
// strong code path ran end-to-end.
// =============================================================================

class DynamicModulesCertValidatorClientIntegrationTest
    : public testing::TestWithParam<CertValidatorIntegrationParam>,
      public HttpIntegrationTest {
public:
  DynamicModulesCertValidatorClientIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam().ip_version) {}

  void SetUp() override {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute("{{ test_rundir }}/test/extensions/dynamic_modules/test_data/" +
                                    GetParam().language),
        1);
    TestEnvironment::setEnvVar("GODEBUG", "cgocheck=0", 1);
  }

  void TearDown() override {
    HttpIntegrationTest::cleanupUpstreamAndDownstream();
    codec_client_.reset();
  }

  void initialize() override {
    config_helper_.addSslConfig(
        ConfigHelper::ServerSslOptions().setRsaCert(true).setTlsV13(true).setRsaCertOcspStaple(
            false));
    HttpIntegrationTest::initialize();
  }

  Network::ClientConnectionPtr makeSslClient() {
    validator_config_storage_ = std::make_unique<envoy::config::core::v3::TypedExtensionConfig>();
    const std::string module_name =
        (GetParam().language == "c") ? "cert_validator_no_op" : "cert_validator_test";
    TestUtility::loadFromYaml(fmt::format(R"EOF(
name: envoy.tls.cert_validator.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_validator.dynamic_modules.v3.DynamicModuleCertValidatorConfig
  dynamic_module_config:
    name: {}
  validator_name: test
)EOF",
                                          module_name),
                              *validator_config_storage_);

    Ssl::ClientSslTransportOptions options;
    options.setCustomCertValidatorConfig(validator_config_storage_.get());

    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("http"));
    auto factory = Ssl::createClientSslTransportSocketFactory(options, context_manager_, *api_);
    return dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                               factory->createTransportSocket(nullptr, nullptr),
                                               nullptr, nullptr);
  }

  std::unique_ptr<envoy::config::core::v3::TypedExtensionConfig> validator_config_storage_;
};

namespace {
std::vector<CertValidatorIntegrationParam> getTestParams() {
  std::vector<CertValidatorIntegrationParam> params;
  for (const auto& language : {"c", "go", "rust"}) {
    for (const auto ip : TestEnvironment::getIpVersionsForTest()) {
      params.push_back({language, ip});
    }
  }
  return params;
}

std::string testParamName(const testing::TestParamInfo<CertValidatorIntegrationParam>& info) {
  return info.param.language + "_" +
         (info.param.ip_version == Network::Address::IpVersion::v4 ? "IPv4" : "IPv6");
}
} // namespace

INSTANTIATE_TEST_SUITE_P(LanguagesAndIpVersions, DynamicModulesCertValidatorClientIntegrationTest,
                         testing::ValuesIn(getTestParams()), testParamName);

TEST_P(DynamicModulesCertValidatorClientIntegrationTest, ValidatorAccepts) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClient();
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

// =============================================================================
// Server-side validator: validates the client's certificate. This exercises
// addClientValidationContext (which builds the CA list from trusted_ca) and the
// three extern "C" callbacks invoked from inside the filter_state module's
// do_verify_cert_chain. The validator returns Successful on the round-trip.
// =============================================================================

class DynamicModulesCertValidatorServerIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModulesCertValidatorServerIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void SetUp() override {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
                               TestEnvironment::substitute(
                                   "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/c"),
                               1);
  }

  void TearDown() override {
    HttpIntegrationTest::cleanupUpstreamAndDownstream();
    codec_client_.reset();
  }

  void initialize() override {
    auto* validator_config = new envoy::config::core::v3::TypedExtensionConfig();
    TestUtility::loadFromYaml(R"EOF(
name: envoy.tls.cert_validator.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_validator.dynamic_modules.v3.DynamicModuleCertValidatorConfig
  dynamic_module_config:
    name: cert_validator_filter_state
  validator_name: test
)EOF",
                              *validator_config);

    config_helper_.addSslConfig(ConfigHelper::ServerSslOptions()
                                    .setRsaCert(true)
                                    .setTlsV13(true)
                                    .setRsaCertOcspStaple(false)
                                    .setCustomValidatorConfig(validator_config));

    // ConfigHelper::initializeTls treats custom_validator_config and trusted_ca as
    // either-or — it skips trusted_ca when a custom validator is set. We add it back via
    // a config modifier so the dynamic_modules validator's addClientValidationContext can
    // populate the server's TLS CertificateRequest CA list. Without that list, BoringSSL
    // clients refuse to send a certificate. Chain validation is still delegated to the
    // dynamic module via the custom verify callback; the CA list is only an advertisement.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* filter_chain =
          bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
      auto* transport_socket = filter_chain->mutable_transport_socket();
      envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
      transport_socket->mutable_typed_config()->UnpackTo(&tls_context);
      tls_context.mutable_common_tls_context()
          ->mutable_validation_context()
          ->mutable_trusted_ca()
          ->set_filename(TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
      transport_socket->mutable_typed_config()->PackFrom(tls_context);
    });

    HttpIntegrationTest::initialize();
  }

  Network::ClientConnectionPtr makeSslClient() {
    Network::Address::InstanceConstSharedPtr address =
        Ssl::getSslAddress(version_, lookupPort("http"));
    auto factory = Ssl::createClientSslTransportSocketFactory(Ssl::ClientSslTransportOptions(),
                                                              context_manager_, *api_);
    return dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                               factory->createTransportSocket(nullptr, nullptr),
                                               nullptr, nullptr);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModulesCertValidatorServerIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModulesCertValidatorServerIntegrationTest, FilterStateCallbacksRoundTrip) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClient();
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

} // namespace
} // namespace DynamicModules
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
