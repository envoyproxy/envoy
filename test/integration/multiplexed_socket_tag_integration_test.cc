#include "test/integration/http_protocol_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

class MultiplexedIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void simultaneousRequest(int32_t request1_bytes, int32_t request2_bytes);

  void initialize() override {
    config_helper_.addFilter("{ name: header-to-socket-tag-filter }");
    HttpProtocolIntegrationTest::initialize();
  }

};

INSTANTIATE_TEST_SUITE_P(IpVersions, MultiplexedIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP2, Http::CodecType::HTTP3},
                             {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(MultiplexedIntegrationTest, TwoRequestsSameUpstream) {
  testRouterRequestAndResponseWithBody(1024, 512, false, false);

  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 1024, default_response_headers_, 512,
                                    0, TestUtility::DefaultTimeout);
  checkSimpleRequestSuccess(1024, 512, response.get());
}

TEST_P(MultiplexedIntegrationTest, TwoRequestsDifferentTagsDifferentUpstream) {
  testRouterRequestAndResponseWithBody(1024, 512, false, false);
  default_request_headers_.addCopy("socket-tag", std::string(4096, 'a'));

  // Nulling out fake_upstream_connection_ causes sendRequestAndWaitForResponse
  // to wait for a new upstream connection.
  auto old_connection = std::move(fake_upstream_connection_);
  fake_upstream_connection_ = nullptr;
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 1024, default_response_headers_, 512,
                                    0, TestUtility::DefaultTimeout);
  checkSimpleRequestSuccess(1024, 512, response.get());
}

} // namespace Envoy
