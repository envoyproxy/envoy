// Integration tests for the dynamic_modules cert validator. These tests run a real Envoy
// server with a TLS listener whose validator is provided by a dynamically loaded module, and
// drive a TLS handshake from a client cert. Unlike the unit test in this same directory,
// the validator's extern "C" callbacks (set_error_details, set_filter_state,
// get_filter_state) are reached through a dlopen'd .so calling into the host — i.e. through
// the dynamic linker rather than a direct C++ call. That dynamic-linker path is what
// exercises the strong definitions in config.cc end-to-end.

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_validator/dynamic_modules/v3/dynamic_modules.pb.h"

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

// Drives a TLS handshake against an Envoy listener whose downstream TLS context is
// configured with the dynamic_modules cert validator backed by the named module. The
// validators in the C/Go/Rust test_data ship a "test"-named validator that always returns
// Successful, so a successful end-to-end handshake is the assertion that the cert validator
// strong code path ran.
class DynamicModulesCertValidatorIntegrationTest
    : public testing::TestWithParam<CertValidatorIntegrationParam>,
      public HttpIntegrationTest {
public:
  DynamicModulesCertValidatorIntegrationTest()
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

  void initializeWithValidator(const std::string& module_name) {
    auto* validator_config = new envoy::config::core::v3::TypedExtensionConfig();
    TestUtility::loadFromYaml(fmt::format(R"EOF(
name: envoy.tls.cert_validator.dynamic_modules
typed_config:
  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_validator.dynamic_modules.v3.DynamicModuleCertValidatorConfig
  dynamic_module_config:
    name: {}
  validator_name: test
)EOF",
                                          module_name),
                              *validator_config);

    config_helper_.addSslConfig(ConfigHelper::ServerSslOptions()
                                    .setRsaCert(true)
                                    .setTlsV13(true)
                                    .setRsaCertOcspStaple(false)
                                    .setCustomValidatorConfig(validator_config));
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

namespace {
std::vector<CertValidatorIntegrationParam> getTestParams() {
  std::vector<CertValidatorIntegrationParam> params;
  // The C, Go, and Rust SDKs each ship a cert validator module that always accepts.
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

INSTANTIATE_TEST_SUITE_P(LanguagesAndIpVersions, DynamicModulesCertValidatorIntegrationTest,
                         testing::ValuesIn(getTestParams()), testParamName);

TEST_P(DynamicModulesCertValidatorIntegrationTest, ValidatorAccepts) {
  // The C fakes are named cert_validator_no_op; the Go/Rust ones are named cert_validator_test.
  const std::string module =
      (GetParam().language == "c") ? "cert_validator_no_op" : "cert_validator_test";
  initializeWithValidator(module);

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClient();
  };
  testRouterRequestAndResponseWithBody(1024, 512, false, false, &creator);
}

// Drives a TLS handshake against a listener using the C-only cert_validator_filter_state
// module. That module's do_verify_cert_chain calls set_filter_state and get_filter_state,
// only returning Successful if the round-trip succeeds — so a successful handshake means the
// strong filter-state callbacks ran through the dynamic linker resolution path.
class DynamicModulesCertValidatorFilterStateTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModulesCertValidatorFilterStateTest()
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

  void initializeWithFilterStateValidator() {
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

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModulesCertValidatorFilterStateTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DynamicModulesCertValidatorFilterStateTest, FilterStateCallbacksRoundTrip) {
  initializeWithFilterStateValidator();

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
