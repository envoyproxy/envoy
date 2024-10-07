#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/filters/http/ext_proc/http_client/http_client_impl.h"

#include "test/mocks/server/server_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

class ExtProcHttpClientTest : public testing::Test {
public:
  ~ExtProcHttpClientTest() override = default;

  const std::string default_http_config_ = R"EOF(
  http_service:
    http_service:
      http_uri:
        uri: "ext_proc_server_0:9000"
        cluster: "ext_proc_server_0"
        timeout:
          seconds: 500
  processing_mode:
    request_header_mode: "SEND"
    response_header_mode: "SKIP"
  )EOF";

  void SetUp() override {
    TestUtility::loadFromYaml(default_http_config_, config_);
    client_ = std::make_unique<ExtProcHttpClient>(config_, context_);
  }

protected:
  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor config_{};
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Upstream::MockClusterManager& cm_{context_.cluster_manager_};
  std::unique_ptr<ExtProcHttpClient> client_;
  testing::NiceMock<Http::MockAsyncClientRequest> async_request_{
      &cm_.thread_local_cluster_.async_client_};
};

TEST_F(ExtProcHttpClientTest, Basic) {
  SetUp();

  client_->cancel();
  client_->context();
  Tracing::MockSpan parent_span;
  client_->onBeforeFinalizeUpstreamSpan(parent_span, nullptr);
  Http::AsyncClient::FailureReason reason = Envoy::Http::AsyncClient::FailureReason::Reset;
  client_->onFailure(async_request_, reason);
  reason = Envoy::Http::AsyncClient::FailureReason::ExceedResponseBufferLimit;
  client_->onFailure(async_request_, reason);
}

TEST_F(ExtProcHttpClientTest, JsonDecodingErrorTest) {
  Http::ResponseHeaderMapPtr resp_headers(new Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Http::ResponseMessagePtr response_ok_with_bad_body(
      new Http::ResponseMessageImpl(std::move(resp_headers)));
  response_ok_with_bad_body->body().add("foo-bar");
  client_->onSuccess(async_request_, std::move(response_ok_with_bad_body));
}

TEST_F(ExtProcHttpClientTest, EmptyResponseBodyTest) {
  Http::ResponseHeaderMapPtr resp_headers(new Http::TestResponseHeaderMapImpl({
      {":status", "200"},
  }));
  Http::ResponseMessagePtr response_ok_with_empty_body(
      new Http::ResponseMessageImpl(std::move(resp_headers)));
  client_->onSuccess(async_request_, std::move(response_ok_with_empty_body));
}

TEST_F(ExtProcHttpClientTest, ResponseStatusNotOkTest) {
  Http::ResponseHeaderMapPtr resp_headers(new Http::TestResponseHeaderMapImpl({
      {":status", "403"},
  }));
  Http::ResponseMessagePtr response(new Http::ResponseMessageImpl(std::move(resp_headers)));
  client_->onSuccess(async_request_, std::move(response));
}

TEST_F(ExtProcHttpClientTest, WrongResponseStatusTest) {
  Http::ResponseHeaderMapPtr resp_headers(new Http::TestResponseHeaderMapImpl({
      {":status", "foo"},
  }));
  Http::ResponseMessagePtr response_foo(new Http::ResponseMessageImpl(std::move(resp_headers)));
  client_->onSuccess(async_request_, std::move(response_foo));
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
