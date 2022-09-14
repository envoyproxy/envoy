#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"

#include "source/extensions/filters/http/custom_response/config.h"
#include "source/extensions/filters/http/custom_response/custom_response_filter.h"
#include "source/extensions/filters/http/custom_response/factory.h"

#include "test/common/http/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {
namespace {

class CustomResponseFilterTest : public testing::Test {
public:
  void setupMockObjects() {
    EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(_))
        .WillRepeatedly(Return(&thread_local_cluster_));
    EXPECT_CALL(thread_local_cluster_.async_client_, send_(_, _, _))
        .WillRepeatedly(Invoke([&](Envoy::Http::RequestMessagePtr& message,
                                   Envoy::Http::AsyncClient::Callbacks& callback,
                                   const Envoy::Http::AsyncClient::RequestOptions& options)
                                   -> Http::AsyncClient::Request* {
          message_.swap(message);
          client_callback_ = &callback;
          options_ = options;
          return &client_request_;
        }));
  }

  void setupFilterAndCallback() {
    filter_ = std::make_unique<CustomResponseFilter>(config_, context_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  void createConfig(const absl::string_view config_str = kDefaultConfig) {
    envoy::extensions::filters::http::custom_response::v3::CustomResponse filter_config;
    TestUtility::loadFromYaml(std::string(config_str), filter_config);
    Stats::StatNameManagedStorage prefix("stats", context_.scope().symbolTable());
    config_ = std::make_shared<FilterConfig>(filter_config, prefix.statName(), context_);
  }

  void setServerName(const std::string& server_name) {
    encoder_callbacks_.stream_info_.downstream_connection_info_provider_->setRequestedServerName(
        server_name);
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster_;
  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info_;
  NiceMock<Envoy::Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Envoy::Http::MockAsyncClientRequest> client_request_{
      &thread_local_cluster_.async_client_};
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;

  // Mocks for http request.
  Envoy::Http::AsyncClient::Callbacks* client_callback_;
  Envoy::Http::RequestMessagePtr message_;
  Envoy::Http::AsyncClient::RequestOptions options_;

  std::unique_ptr<CustomResponseFilter> filter_;
  std::shared_ptr<FilterConfig> config_;
  Http::TestResponseHeaderMapImpl default_headers_{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
};

TEST_F(CustomResponseFilterTest, LocalData) {
  setupMockObjects();
  createConfig();
  setupFilterAndCallback();

  setServerName("server1.example.foo");
  Http::TestResponseHeaderMapImpl headers{{":status", "499"}};
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, _));
  EXPECT_EQ(filter_->encodeHeaders(headers, true), Http::FilterHeadersStatus::StopIteration);
}

TEST_F(CustomResponseFilterTest, DISABLED_RemoteData) {
  setupMockObjects();
  createConfig();
  setupFilterAndCallback();

  setServerName("server1.example.foo");
  Http::TestResponseHeaderMapImpl headers{{":status", "503"}};
  EXPECT_EQ(filter_->encodeHeaders(headers, true), Http::FilterHeadersStatus::StopIteration);
}

} // namespace
} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
