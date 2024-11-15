#include <memory>
#include <string>

#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "source/common/common/assert.h"
#include "source/extensions/filters/common/ext_authz/ext_authz_http_impl.h"

#include "test/extensions/filters/common/ext_authz/test_common.h"
#include "test/extensions/filters/http/common/fuzz/http_filter_fuzzer.h"
#include "test/extensions/filters/http/ext_authz/ext_authz_fuzz.pb.h"
#include "test/extensions/filters/http/ext_authz/ext_authz_fuzz.pb.validate.h"
#include "test/extensions/filters/http/ext_authz/ext_authz_fuzz_lib.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "gmock/gmock.h"

using Envoy::Extensions::Filters::Common::ExtAuthz::TestCommon;
using envoy::extensions::filters::http::ext_authz::ExtAuthzTestCaseHttp;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {
namespace {

std::string resultCaseToHttpStatus(const ExtAuthzTestCaseHttp::AuthResult result) {
  switch (result) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case ExtAuthzTestCaseHttp::OK:
    return "200";
  case ExtAuthzTestCaseHttp::ERROR:
    return "500";
  case ExtAuthzTestCaseHttp::DENIED:
    return "403";
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

class ReusableHttpClientFactory {
public:
  ReusableHttpClientFactory() : http_sync_request_(&internal_http_mock_client_) {
    cm_.initializeThreadLocalClusters({"ext_authz"});
    ON_CALL(cm_.thread_local_cluster_, httpAsyncClient())
        .WillByDefault(ReturnRef(internal_http_mock_client_));

    ON_CALL(internal_http_mock_client_, send_(_, _, _))
        .WillByDefault(Invoke([&](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks&,
                                  const Envoy::Http::AsyncClient::RequestOptions)
                                  -> Http::AsyncClient::Request* {
          if (status_ == "200") {
            const auto headers = TestCommon::makeHeaderValueOption({{":status", status_, false}});
            http_client_->onSuccess(http_sync_request_, TestCommon::makeMessageResponse(headers));
          } else {
            http_client_->onFailure(http_sync_request_, Http::AsyncClient::FailureReason::Reset);
          }
          return &http_sync_request_;
        }));
  }

  std::unique_ptr<Filters::Common::ExtAuthz::RawHttpClientImpl>
  newRawHttpClientImpl(const ExtAuthzTestCaseHttp::AuthResult result) {
    http_client_ = new Filters::Common::ExtAuthz::RawHttpClientImpl(cm_, createConfig());
    status_ = resultCaseToHttpStatus(result);
    return std::unique_ptr<Filters::Common::ExtAuthz::RawHttpClientImpl>(http_client_);
  }

private:
  Filters::Common::ExtAuthz::ClientConfigSharedPtr createConfig() {
    envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config{};
    const std::string default_yaml = R"EOF(
      http_service:
        server_uri:
          uri: "ext_authz:9000"
          cluster: "ext_authz"
          timeout: 0.25s
        authorization_request:
          headers_to_add:
          - key: "x-authz-header1"
            value: "value"
          - key: "x-authz-header2"
            value: "value"
        authorization_response:
          allowed_upstream_headers:
            patterns:
            - exact: Bar
              ignore_case: true
            - prefix: "X-"
              ignore_case: true
          allowed_upstream_headers_to_append:
            patterns:
            - exact: Alice
              ignore_case: true
            - prefix: "Append-"
              ignore_case: true
          allowed_client_headers:
            patterns:
            - exact: Foo
              ignore_case: true
            - prefix: "X-"
              ignore_case: true
          allowed_client_headers_on_success:
            patterns:
            - prefix: "X-Downstream-"
              ignore_case: true
      )EOF";
    TestUtility::loadFromYaml(default_yaml, proto_config);

    return std::make_shared<Filters::Common::ExtAuthz::ClientConfig>(proto_config, 250, "/bar",
                                                                     factory_context_);
  }

  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Http::MockAsyncClient> internal_http_mock_client_;
  NiceMock<Http::MockAsyncClientRequest> http_sync_request_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;

  // Set by calling newRawHttpClientImpl
  std::string status_;
  Filters::Common::ExtAuthz::RawHttpClientImpl* http_client_;
};

DEFINE_PROTO_FUZZER(const ExtAuthzTestCaseHttp& input) {
  static ReusableFuzzerUtil fuzzer_util;
  static ReusableHttpClientFactory http_client_factory;

  try {
    TestUtility::validate(input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during validation: {}", e.what());
    return;
  }

  auto http_client = http_client_factory.newRawHttpClientImpl(input.result());
  absl::StatusOr<std::unique_ptr<Filter>> filter =
      fuzzer_util.setup(input.base(), std::move(http_client));
  if (!filter.ok()) {
    return;
  }

  // TODO: Add response headers.
  static Envoy::Extensions::HttpFilters::HttpFilterFuzzer fuzzer;
  fuzzer.runData(static_cast<Envoy::Http::StreamDecoderFilter*>(filter->get()),
                 input.base().request_data());
}

} // namespace
} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
