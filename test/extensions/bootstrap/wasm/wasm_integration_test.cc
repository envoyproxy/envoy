#include "extensions/common/wasm/wasm.h"

#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

using testing::Eq;

namespace Envoy {
namespace Extensions {
namespace Wasm {
namespace {

class WasmIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(GetParam().upstream_protocol);
  }

  void cleanup() {
    if (wasm_connection_ != nullptr) {
      ASSERT_TRUE(wasm_connection_->close());
      ASSERT_TRUE(wasm_connection_->waitForDisconnect());
    }
    cleanupUpstreamAndDownstream();
  }
  void initialize() override {
    auto httpwasm = TestEnvironment::substitute(
        "{{ test_rundir }}/test/extensions/bootstrap/wasm/test_data/http_cpp.wasm");
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* wasm = bootstrap.mutable_static_resources()->add_clusters();
      wasm->MergeFrom(bootstrap.static_resources().clusters()[0]);
      wasm->set_name("wasm_cluster");
    });

    config_helper_.addBootstrapExtension(fmt::format(R"EOF(
name: envoy.filters.http.wasm
typed_config:
  '@type': type.googleapis.com/envoy.extensions.wasm.v3.WasmService
  singleton: true
  config:
    name: "singleton"
    root_id: "singleton"
    configuration:
      '@type': type.googleapis.com/google.protobuf.StringValue
      value: ""
    vm_config:
      vm_id: "my_vm_id"
      runtime: "envoy.wasm.runtime.v8"
      code:
        local:
          filename: {}
  )EOF",
                                                     httpwasm));
    HttpIntegrationTest::initialize();
  }

  FakeHttpConnectionPtr wasm_connection_;
  FakeStreamPtr wasm_request_;
  IntegrationStreamDecoderPtr response_;
};

INSTANTIATE_TEST_SUITE_P(Protocols, WasmIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(WasmIntegrationTest, FilterMakesCallInConfigureTime) {
  initialize();
  ASSERT_TRUE(fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, wasm_connection_));

  // Expect the filter to send us an HTTP request
  ASSERT_TRUE(wasm_connection_->waitForNewStream(*dispatcher_, wasm_request_));

  ASSERT_TRUE(wasm_request_->waitForEndStream(*dispatcher_));

  // Respond back to the filter.
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
  };
  wasm_request_->encodeHeaders(response_headers, true);
  cleanup();
}

} // namespace
} // namespace Wasm
} // namespace Extensions
} // namespace Envoy
