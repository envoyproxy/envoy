#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/config/v2_link_hacks.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "contrib/golang/filters/http/source/golang_filter.h"
#include "gtest/gtest.h"

namespace Envoy {
class GolangIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                              public HttpIntegrationTest {
public:
  GolangIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP1);
    addFakeUpstream(Http::CodecType::HTTP1);
  }

  std::string genSoPath(std::string name) {
    return TestEnvironment::substitute(
        "{{ test_rundir }}/contrib/golang/filters/http/test/test_data/" + name + "/filter.so");
  }

  void initializeConfig(const std::string& lib_id, const std::string& lib_path,
                        const std::string& plugin_name) {
    const auto yaml_fmt = R"EOF(
name: golang
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.golang.v3alpha.Config
  library_id: %s
  library_path: %s
  plugin_name: %s
  merge_policy: MERGE_VIRTUALHOST_ROUTER_FILTER
  plugin_config:
    "@type": type.googleapis.com/udpa.type.v1.TypedStruct
    type_url: typexx
    value:
      remove: x-test-header-0
      set: foo
)EOF";

    auto yaml_string = absl::StrFormat(yaml_fmt, lib_id, lib_path, plugin_name);
    config_helper_.prependFilter(yaml_string);
    config_helper_.skipPortUsageValidation();
  }

  const std::string ECHO{"echo"};
  const std::string PASSTHROUGH{"passthrough"};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, GolangIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(GolangIntegrationTest, Echo) {
  initializeConfig(ECHO, genSoPath(ECHO), ECHO);
  initialize();
  registerTestServerPorts({"http"});

  auto path = "/localreply";
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"}, {":path", path}, {":scheme", "http"}, {":authority", "test.com"}};

  auto encoder_decoder = codec_client_->startRequest(request_headers);
  Http::RequestEncoder& request_encoder = encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(request_encoder, "helloworld", true);

  ASSERT_TRUE(response->waitForEndStream());

  // check status for echo
  EXPECT_EQ("403", response->headers().getStatusValue());

  // check body for echo
  auto body = StringUtil::toUpper(absl::StrFormat("forbidden from go, path: %s\r\n", path));
  EXPECT_EQ(body, StringUtil::toUpper(response->body()));

  codec_client_->close();

  if (fake_upstream_connection_ != nullptr) {
    AssertionResult result = fake_upstream_connection_->close();
    RELEASE_ASSERT(result, result.message());
    result = fake_upstream_connection_->waitForDisconnect();
    RELEASE_ASSERT(result, result.message());
  }
}

TEST_P(GolangIntegrationTest, Passthrough) {
  initializeConfig(PASSTHROUGH, genSoPath(PASSTHROUGH), PASSTHROUGH);
  initialize();
  registerTestServerPorts({"http"});

  auto path = "/";
  auto good = "good";
  auto bye = "bye";
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "POST"}, {":path", path}, {":scheme", "http"}, {":authority", "test.com"}};

  auto encoder_decoder = codec_client_->startRequest(request_headers);
  Http::RequestEncoder& request_encoder = encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);
  codec_client_->sendData(request_encoder, "helloworld", true);

  waitForNextUpstreamRequest();
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
  upstream_request_->encodeHeaders(response_headers, false);
  Buffer::OwnedImpl response_data1(good);
  upstream_request_->encodeData(response_data1, false);
  Buffer::OwnedImpl response_data2(bye);
  upstream_request_->encodeData(response_data2, true);

  ASSERT_TRUE(response->waitForEndStream());

  // check status for pasthrough
  EXPECT_EQ("200", response->headers().getStatusValue());

  // check body for pasthrough
  auto body = StringUtil::toUpper(absl::StrFormat("%s%s", good, bye));
  EXPECT_EQ(body, StringUtil::toUpper(response->body()));

  codec_client_->close();

  if (fake_upstream_connection_ != nullptr) {
    AssertionResult result = fake_upstream_connection_->close();
    RELEASE_ASSERT(result, result.message());
    result = fake_upstream_connection_->waitForDisconnect();
    RELEASE_ASSERT(result, result.message());
  }
}
} // namespace Envoy
