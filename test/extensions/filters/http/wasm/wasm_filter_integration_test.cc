#include "source/extensions/common/wasm/wasm.h"

#include "test/extensions/common/wasm/wasm_runtime.h"
#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Wasm {
namespace {

class WasmFilterIntegrationTest
    : public HttpIntegrationTest,
      public testing::TestWithParam<std::tuple<std::string, std::string, bool, Http::CodecType>> {
public:
  WasmFilterIntegrationTest()
      : HttpIntegrationTest(std::get<3>(GetParam()), Network::Address::IpVersion::v4) {}

  void SetUp() override {
    setUpstreamProtocol(std::get<3>(GetParam()));
    if (std::get<3>(GetParam()) == Http::CodecType::HTTP2) {
      config_helper_.setClientCodec(envoy::extensions::filters::network::http_connection_manager::
                                        v3::HttpConnectionManager::HTTP2);
    } else {
      config_helper_.setClientCodec(envoy::extensions::filters::network::http_connection_manager::
                                        v3::HttpConnectionManager::HTTP1);
    }
    // Wasm filters are expensive to setup and sometime default is not enough,
    // It needs to increase timeout to avoid flaky tests
    setListenersBoundTimeout(10 * TestUtility::DefaultTimeout);
  }

  void TearDown() override { fake_upstream_connection_.reset(); }

  void setupWasmFilter(const std::string& config, const std::string& root_id = "") {
    bool downstream = std::get<2>(GetParam());
    const std::string yaml = TestEnvironment::substitute(absl::StrCat(
        R"EOF(
          name: envoy.filters.http.wasm
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.wasm.v3.Wasm
            config:
              name: "plugin_name"
              root_id: ")EOF",
        root_id, R"EOF("
              vm_config:
                vm_id: "vm_id"
                runtime: envoy.wasm.runtime.)EOF",
        std::get<0>(GetParam()), R"EOF(
                configuration:
                  "@type": type.googleapis.com/google.protobuf.StringValue
                  value: )EOF",
        config, R"EOF(
                code:
                  local:
                    filename: "{{ test_rundir }}/test/extensions/filters/http/wasm/test_data/test_cpp.wasm"
        )EOF"));
    config_helper_.prependFilter(yaml, downstream);
  }

  template <typename TMap> void assertCompareMaps(const TMap& m1, const TMap& m2) {
    m1.iterate([&m2](const Http::HeaderEntry& entry) -> Http::HeaderMap::Iterate {
      Http::LowerCaseString lower_key{std::string(entry.key().getStringView())};
      if (entry.value() == "") {
        EXPECT_TRUE(m2.get(lower_key).empty());
      } else {
        if (m2.get(lower_key).empty()) {
          ADD_FAILURE() << "Header " << lower_key.get() << " not found.";
        } else {
          EXPECT_EQ(entry.value().getStringView(), m2.get(lower_key)[0]->value().getStringView());
        }
      }
      return Http::HeaderMap::Iterate::Continue;
    });
  }

  void runTest(const Http::RequestHeaderMap& request_headers,
               const std::vector<std::string>& request_body,
               const Http::RequestHeaderMap& expected_request_headers,
               const std::string& expected_request_body,
               const Http::ResponseHeaderMap& upstream_response_headers,
               const std::vector<std::string>& upstream_response_body,
               const Http::ResponseHeaderMap& expected_response_headers,
               const std::string& expected_response_body) {

    codec_client_ = makeHttpConnection(lookupPort("http"));
    IntegrationStreamDecoderPtr response;
    if (request_body.empty()) {
      response = codec_client_->makeHeaderOnlyRequest(request_headers);
    } else {
      auto encoder_decoder = codec_client_->startRequest(request_headers);
      request_encoder_ = &encoder_decoder.first;
      response = std::move(encoder_decoder.second);
      const auto request_body_size = request_body.size();
      for (size_t n = 0; n < request_body_size; n++) {
        Buffer::OwnedImpl buffer(request_body[n]);
        codec_client_->sendData(*request_encoder_, buffer, n == request_body_size - 1);
      }
    }

    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

    assertCompareMaps(expected_request_headers, upstream_request_->headers());
    EXPECT_STREQ(expected_request_body.c_str(), upstream_request_->body().toString().c_str());

    if (upstream_response_body.empty()) {
      upstream_request_->encodeHeaders(upstream_response_headers, true /*end_stream*/);
    } else {
      upstream_request_->encodeHeaders(upstream_response_headers, false /*end_stream*/);
      const auto upstream_response_body_size = upstream_response_body.size();
      for (size_t n = 0; n < upstream_response_body_size; n++) {
        Buffer::OwnedImpl buffer(upstream_response_body[n]);
        upstream_request_->encodeData(buffer, n == upstream_response_body_size - 1);
      }
    }

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());

    assertCompareMaps(expected_response_headers, response->headers());
    EXPECT_STREQ(expected_response_body.c_str(), response->body().c_str());

    // cleanup
    codec_client_->close();
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }
};

INSTANTIATE_TEST_SUITE_P(Runtimes, WasmFilterIntegrationTest,
                         Common::Wasm::dual_filter_with_codecs_sandbox_runtime_and_cpp_values,
                         Common::Wasm::wasmDualFilterWithCodecsTestParamsToString);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(WasmFilterIntegrationTest);

TEST_P(WasmFilterIntegrationTest, HeadersManipulation) {
  setupWasmFilter("headers");
  HttpIntegrationTest::initialize();

  Http::TestRequestHeaderMapImpl request_headers{{":scheme", "http"},
                                                 {":method", "GET"},
                                                 {":path", "/resize?type=jpg"},
                                                 {":authority", "host"},
                                                 {"server", "client-id"}};
  Http::TestRequestHeaderMapImpl expected_request_headers{{":method", "GET"},
                                                          {":path", "/resize?type=jpg"},
                                                          {":authority", "host"},
                                                          {"newheader", "newheadervalue"},
                                                          {"server", "envoy-wasm"}};

  Http::TestResponseHeaderMapImpl upstream_response_headers{{":status", "200"},
                                                            {"content-type", "application/json"}};

  Http::TestResponseHeaderMapImpl expected_response_headers{
      {":status", "200"}, {"content-type", "application/json"}, {"test-status", "OK"}};

  auto request_body = std::vector<std::string>{};
  auto upstream_response_body = std::vector<std::string>{};
  runTest(request_headers, request_body, expected_request_headers, "", upstream_response_headers,
          upstream_response_body, expected_response_headers, "");
}

TEST_P(WasmFilterIntegrationTest, BodyManipulation) {
  setupWasmFilter("", "body");
  HttpIntegrationTest::initialize();

  Http::TestRequestHeaderMapImpl request_headers{{":scheme", "http"},
                                                 {":method", "GET"},
                                                 {":path", "/"},
                                                 {":authority", "host"},
                                                 {"x-test-operation", "ReplaceBufferedBody"}};

  Http::TestRequestHeaderMapImpl expected_request_headers{{":path", "/"}};

  Http::TestResponseHeaderMapImpl upstream_response_headers{
      {":status", "200"}, {"x-test-operation", "ReplaceBufferedBody"}};

  Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"}};

  auto request_body = std::vector<std::string>{{"request_body"}};
  auto upstream_response_body = std::vector<std::string>{{"upstream_body"}};
  runTest(request_headers, request_body, expected_request_headers, "replace",
          upstream_response_headers, upstream_response_body, expected_response_headers, "replace");
}

TEST_P(WasmFilterIntegrationTest, BodyBufferedManipulation) {
  setupWasmFilter("", "body");
  HttpIntegrationTest::initialize();

  Http::TestRequestHeaderMapImpl request_headers{{":scheme", "http"},
                                                 {":method", "GET"},
                                                 {":path", "/"},
                                                 {":authority", "host"},
                                                 {"x-test-operation", "SetEndOfBodies"}};

  Http::TestRequestHeaderMapImpl expected_request_headers{{":path", "/"}};

  Http::TestResponseHeaderMapImpl upstream_response_headers{{":status", "200"},
                                                            {"x-test-operation", "SetEndOfBodies"}};

  Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"}};

  auto request_body = std::vector<std::string>{{"request_"}, {"body"}};
  auto upstream_response_body = std::vector<std::string>{{"upstream_"}, {"body"}};
  runTest(request_headers, request_body, expected_request_headers, "request_body.end",
          upstream_response_headers, upstream_response_body, expected_response_headers,
          "upstream_body.end");
}

TEST_P(WasmFilterIntegrationTest, BodyBufferedMultipleChunksManipulation) {
  setupWasmFilter("", "body");
  HttpIntegrationTest::initialize();

  Http::TestRequestHeaderMapImpl request_headers{{":scheme", "http"},
                                                 {":method", "GET"},
                                                 {":path", "/"},
                                                 {":authority", "host"},
                                                 {"x-test-operation", "SetEndOfBodies"}};

  Http::TestRequestHeaderMapImpl expected_request_headers{{":path", "/"}};

  Http::TestResponseHeaderMapImpl upstream_response_headers{{":status", "200"},
                                                            {"x-test-operation", "SetEndOfBodies"}};

  Http::TestResponseHeaderMapImpl expected_response_headers{{":status", "200"}};

  auto request_body = std::vector<std::string>{{"request_"}, {"very_"}, {"long_"}, {"body"}};
  auto upstream_response_body =
      std::vector<std::string>{{"upstream_"}, {"very_"}, {"long_"}, {"body"}};
  runTest(request_headers, request_body, expected_request_headers, "request_very_long_body.end",
          upstream_response_headers, upstream_response_body, expected_response_headers,
          "upstream_very_long_body.end");
}

} // namespace
} // namespace Wasm
} // namespace Extensions
} // namespace Envoy
