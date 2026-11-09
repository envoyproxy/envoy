#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {
namespace {

const std::string AWS_REQUEST_SIGNING_UPSTREAM_FILTER = R"EOF(
name: envoy.filters.http.aws_request_signing
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.aws_request_signing.v3.AwsRequestSigning
  service_name: execute-api
  region: us-east-1
  signing_algorithm: aws_sigv4
  credential_provider:
    custom_credential_provider_chain: true
    container_credential_provider: {}
)EOF";

class DynamicModulesBootstrapAwsSigningIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  DynamicModulesBootstrapAwsSigningIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    // Nothing is listening here, so the fetch fails, anonymous credentials are installed, and the
    // callout goes out unsigned. That doesn't matter. The deadlock is in clearing the pending flag,
    // not in the signature, and both the success and the failure path of the fetch reach it
    // through setCredentialsToAllThreads().
    TestEnvironment::setEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI",
                               "http://127.0.0.1:1/path/to/creds", 1);
  }

  ~DynamicModulesBootstrapAwsSigningIntegrationTest() override {
    // Undo environment changes.
    TestEnvironment::unsetEnvVar("AWS_CONTAINER_CREDENTIALS_FULL_URI");
    TestEnvironment::unsetEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH");
  }

  void initializeWithBootstrapExtension(const std::string& module_dir,
                                        const std::string& module_name = "test",
                                        const std::string& extension_name = "test",
                                        const std::string& extension_config = "test_config") {
    TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", module_dir, 1);
    const std::string yaml = fmt::format(R"EOF(
      name: envoy.bootstrap.dynamic_modules
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.bootstrap.dynamic_modules.v3.DynamicModuleBootstrapExtension
        dynamic_module_config:
          name: {}
        extension_name: {}
        extension_config:
          "@type": type.googleapis.com/google.protobuf.StringValue
          value: {}
    )EOF",
                                         module_name, extension_name, extension_config);

    config_helper_.addBootstrapExtension(yaml);
    HttpIntegrationTest::initialize();
  }

  std::string testDataDir(const std::string& subdir) {
    return TestEnvironment::runfilesPath("test/extensions/dynamic_modules/test_data/" + subdir);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DynamicModulesBootstrapAwsSigningIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Regression test for a deadlock between the bootstrap init target and AWS credential resolution.
// The module holds its init target open until an HTTP callout through cluster_0 completes, and
// cluster_0 carries an upstream aws_request_signing filter whose credentials chain has no
// synchronous provider, so signing has to wait on an async metadata fetch.
//
// That fetch resolves on the main thread before any worker thread exists. While the resulting
// "credentials are no longer pending" notification was driven from the all-threads-complete
// callback of runOnAllThreads(), it could never fire here: that callback waits for every
// registered worker dispatcher to run the update, worker dispatchers do not run until
// startWorkers(), and startWorkers() waits on the very init manager this module is holding open.
// The signing filter then held the callout until its deadline. Reaching the module's success log at
// all is the assertion; a regression instead fails on the harness giving up waiting for listeners,
// and the module budgets its retries so it cannot spin indefinitely behind that.
TEST_P(DynamicModulesBootstrapAwsSigningIntegrationTest, SignedCalloutGatingInitTarget) {
  // Nothing is servicing the fake upstream while the server is still initializing, so it has to
  // answer the callout on its own.
  autonomous_upstream_ = true;
  config_helper_.prependFilter(AWS_REQUEST_SIGNING_UPSTREAM_FILTER, /*downstream=*/false);
  // cluster_0 carries no protocol options in the base config, and upstream_protocol_options is a
  // required field once any are set. This fills it in without disturbing the filter chain above.
  setUpstreamProtocol(Http::CodecType::HTTP1);

  EXPECT_LOG_CONTAINS(
      "info", "Bootstrap signed callout test completed successfully!",
      initializeWithBootstrapExtension(testDataDir("rust"), "bootstrap_signed_callout_test"));
}

} // namespace
} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
