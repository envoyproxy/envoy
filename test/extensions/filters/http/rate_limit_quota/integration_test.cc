#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"

#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/filter.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/rate_limit_quota/test_utils.h"
#include "test/integration/http_integration.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {
namespace {

// These tests exercise the rate limit quota filter through Envoy's integration test
// environment by configuring an instance of the Envoy server and driving it
// through the mock network stack.
class RateLimitQuotaIntegrationTest
    : public HttpIntegrationTest,
      public Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing {
protected:
  RateLimitQuotaIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();

    // Create separate side stream for rate limit quota server
    for (int i = 0; i < 2; ++i) {
      grpc_upstreams_.push_back(&addFakeUpstream(Http::CodecType::HTTP2));
    }
  }

  void initializeConfig(bool valid_rlqs_server = true) {
    config_helper_.addConfigModifier(
        [this, valid_rlqs_server](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          // Ensure "HTTP2 with no prior knowledge." Necessary for gRPC and for headers
          ConfigHelper::setHttp2(
              *(bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0)));

          // Clusters for ExtProc gRPC servers, starting by copying an existing cluster
          for (size_t i = 0; i < grpc_upstreams_.size(); ++i) {
            auto* server_cluster = bootstrap.mutable_static_resources()->add_clusters();
            server_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
            std::string cluster_name = absl::StrCat("rlqs_server_", i);
            server_cluster->set_name(cluster_name);
            server_cluster->mutable_load_assignment()->set_cluster_name(cluster_name);
          }

          if (valid_rlqs_server) {
            // Load configuration of the server from YAML and use a helper to add a grpc_service
            // stanza pointing to the cluster that we just made
            setGrpcService(*proto_config_.mutable_rlqs_server(), "rlqs_server_0",
                           grpc_upstreams_[0]->localAddress());
          } else {
            // Set up the gRPC service with wrong cluster name and address.
            setGrpcService(*proto_config_.mutable_rlqs_server(), "rlqs_wrong_server",
                           std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234));
            // setGrpcService(*proto_config_.mutable_rlqs_server(), "rlqs_wrong_server",
            //                grpc_upstreams_[0]->localAddress());
          }

          // Set the domain name.
          proto_config_.set_domain("cloud_12345_67890_rlqs");

          xds::type::matcher::v3::Matcher matcher;
          TestUtility::loadFromYaml(std::string(ValidMatcherConfig), matcher);
          proto_config_.mutable_bucket_matchers()->MergeFrom(matcher);

          // Construct a configuration proto for our filter and then re-write it
          // to JSON so that we can add it to the overall config
          envoy::config::listener::v3::Filter rate_limit_quota_filter;
          rate_limit_quota_filter.set_name("envoy.filters.http.rate_limit_quota");
          rate_limit_quota_filter.mutable_typed_config()->PackFrom(proto_config_);
          config_helper_.prependFilter(
              MessageUtil::getJsonStringFromMessageOrError(rate_limit_quota_filter));

          // Parameterize with defer processing to prevent bit rot as filter made
          // assumptions of data flow, prior relying on eager processing.
          config_helper_.addRuntimeOverride(Runtime::defer_processing_backedup_streams,
                                            deferredProcessing() ? "true" : "false");
        });
    setUpstreamProtocol(Http::CodecType::HTTP2);
    setDownstreamProtocol(Http::CodecType::HTTP2);
  }

  // Send downstream client request.
  IntegrationStreamDecoderPtr
  sendClientRequest(const absl::flat_hash_map<std::string, std::string>* custom_headers = nullptr) {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    if (custom_headers != nullptr) {
      for (auto const& pair : *custom_headers) {
        headers.addCopy(pair.first, pair.second);
      }
    }
    return codec_client_->makeHeaderOnlyRequest(headers);
  }

  void TearDown() override {
    if (rlqs_connection_) {
      ASSERT_TRUE(rlqs_connection_->close());
      ASSERT_TRUE(rlqs_connection_->waitForDisconnect());
    }
    cleanupUpstreamAndDownstream();
  }

  envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig
      proto_config_{};
  std::vector<FakeUpstream*> grpc_upstreams_;
  FakeHttpConnectionPtr rlqs_connection_;
  FakeStreamPtr rlqs_stream_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDeferredProcessing, RateLimitQuotaIntegrationTest,
    GRPC_CLIENT_INTEGRATION_DEFERRED_PROCESSING_PARAMS,
    Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing::protocolTestParamsToString);

// TODO(tyxia) google-gRPC start pointer is not null.
TEST_P(RateLimitQuotaIntegrationTest, StarFailed) {
  // SKIP_IF_GRPC_CLIENT(Grpc::ClientType::GoogleGrpc);
  initializeConfig(/*valid_rlqs_server=*/false);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  // TODO(tyxia) Here is the key to fix envoy_grpc memory issue.
  auto response = sendClientRequest(&custom_headers);
  EXPECT_FALSE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_,
                                                         std::chrono::milliseconds(25000)));
}

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
