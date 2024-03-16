#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"

#include "source/common/http/context_impl.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/filters/http/ext_authz/ext_authz.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/common/ext_authz/test_common.h"
#include "test/extensions/filters/http/common/fuzz/http_filter_fuzzer.h"
#include "test/extensions/filters/http/ext_authz/ext_authz_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "gmock/gmock.h"

using Envoy::Extensions::Filters::Common::ExtAuthz::TestCommon;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {
namespace {

std::unique_ptr<envoy::service::auth::v3::CheckResponse>
makeGrpcCheckResponse(const Grpc::Status::WellKnownGrpcStatus status) {
  auto response = std::make_unique<envoy::service::auth::v3::CheckResponse>();
  response->mutable_status()->set_code(status);
  // TODO: We only add the response status.
  // Add fuzzed inputs for headers_to_(set/append/add), body, status_code to the Response.
  return response;
}

Grpc::Status::WellKnownGrpcStatus resultCaseToGrpcStatus(
    const envoy::extensions::filters::http::ext_authz::ExtAuthzTestCase::AuthResult result) {
  Grpc::Status::WellKnownGrpcStatus check_status;
  switch (result) {
  case envoy::extensions::filters::http::ext_authz::ExtAuthzTestCase::OK: {
    check_status = Grpc::Status::WellKnownGrpcStatus::Ok;
    break;
  }
  case envoy::extensions::filters::http::ext_authz::ExtAuthzTestCase::ERROR: {
    check_status = Grpc::Status::WellKnownGrpcStatus::Internal;
    break;
  }
  case envoy::extensions::filters::http::ext_authz::ExtAuthzTestCase::DENIED: {
    check_status = Grpc::Status::WellKnownGrpcStatus::PermissionDenied;
    break;
  }
  default: {
    // Unhandled status.
    PANIC("not implemented");
  }
  }
  return check_status;
}

std::string resultCaseToHttpStatus(
    const envoy::extensions::filters::http::ext_authz::ExtAuthzTestCase::AuthResult result) {
  std::string check_status;
  switch (result) {
  case envoy::extensions::filters::http::ext_authz::ExtAuthzTestCase::OK: {
    check_status = "200";
    break;
  }
  case envoy::extensions::filters::http::ext_authz::ExtAuthzTestCase::ERROR: {
    check_status = "500";
    break;
  }
  case envoy::extensions::filters::http::ext_authz::ExtAuthzTestCase::DENIED: {
    check_status = "403";
    break;
  }
  default: {
    // Unhandled status.
    PANIC("not implemented");
  }
  }
  return check_status;
}

class StatelessFuzzerMocks {
public:
  StatelessFuzzerMocks()
      : addr_(std::make_shared<Network::Address::PipeInstance>("/test/test.sock")) {
    connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
    connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  }

  // Only add mocks here that are stateless. I.e. if you need to call ON_CALL on a mock each fuzzer
  // run, do not add the mock here, because it will leak memory.
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Network::Address::InstanceConstSharedPtr addr_;
  NiceMock<Envoy::Network::MockConnection> connection_;
};

class ReusableGrpcClientFactory {
public:
  ReusableGrpcClientFactory()
      : internal_grpc_mock_client_(std::make_shared<NiceMock<Grpc::MockAsyncClient>>()) {
    ON_CALL(*internal_grpc_mock_client_, sendRaw(_, _, _, _, _, _))
        .WillByDefault(
            Invoke([&](absl::string_view, absl::string_view, Buffer::InstancePtr&& serialized_req,
                       Grpc::RawAsyncRequestCallbacks&, Tracing::Span&,
                       const Http::AsyncClient::RequestOptions&) -> Grpc::AsyncRequest* {
              envoy::service::auth::v3::CheckRequest check_request;
              EXPECT_TRUE(check_request.ParseFromString(serialized_req->toString()))
                  << "Could not parse serialized check request";

              // TODO: Query the request header map in HttpFilterFuzzer to test
              // headers_to_(add/remove/append).
              // TODO: Test check request attributes against config
              // and filter metadata.
              ENVOY_LOG_MISC(trace, "Check Request attributes {}",
                             check_request.attributes().DebugString());

              if (status_ == Grpc::Status::WellKnownGrpcStatus::Ok) {
                grpc_client_->onSuccess(makeGrpcCheckResponse(status_), mock_span_);
              } else {
                grpc_client_->onFailure(status_, "Fuzz input status was not ok!", mock_span_);
              }
              return &grpc_async_request_;
            }));
  }

  std::unique_ptr<Filters::Common::ExtAuthz::GrpcClientImpl> newGrpcClientImpl(
      const envoy::extensions::filters::http::ext_authz::ExtAuthzTestCase::AuthResult result) {
    status_ = resultCaseToGrpcStatus(result);
    grpc_client_ = new Filters::Common::ExtAuthz::GrpcClientImpl(internal_grpc_mock_client_,
                                                                 std::chrono::milliseconds(1000));
    return std::unique_ptr<Filters::Common::ExtAuthz::GrpcClientImpl>{grpc_client_};
  }

private:
  std::shared_ptr<NiceMock<Grpc::MockAsyncClient>> internal_grpc_mock_client_;
  Envoy::Tracing::MockSpan mock_span_;
  NiceMock<Grpc::MockAsyncRequest> grpc_async_request_;

  // Set but calling newGrpcClientImpl
  Grpc::Status::WellKnownGrpcStatus status_;
  Filters::Common::ExtAuthz::GrpcClientImpl* grpc_client_;
};

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

  std::unique_ptr<Filters::Common::ExtAuthz::RawHttpClientImpl> newRawHttpClientImpl(
      const envoy::extensions::filters::http::ext_authz::ExtAuthzTestCase::AuthResult result) {
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

DEFINE_PROTO_FUZZER(const envoy::extensions::filters::http::ext_authz::ExtAuthzTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during validation: {}", e.what());
    return;
  }

  static StatelessFuzzerMocks mocks;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store;
  static ScopedInjectableLoader<Regex::Engine> engine(std::make_unique<Regex::GoogleReEngine>());

  // Prepare filter.
  const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz proto_config = input.config();
  FilterConfigSharedPtr config;

  try {
    config = std::make_shared<FilterConfig>(proto_config, *stats_store.rootScope(),
                                            "ext_authz_prefix", mocks.factory_context_);

  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during filter config validation: {}", e.what());
    return;
  }

  // Set metadata context.
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  ON_CALL(decoder_callbacks, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{mocks.connection_}));
  envoy::config::core::v3::Metadata metadata = input.filter_metadata();
  ON_CALL(decoder_callbacks.stream_info_, dynamicMetadata())
      .WillByDefault(testing::ReturnRef(metadata));

  static ReusableGrpcClientFactory grpc_client_factory;
  auto grpc_client = grpc_client_factory.newGrpcClientImpl(input.result());
  auto filter = std::make_unique<Filter>(config, std::move(grpc_client));
  filter->setDecoderFilterCallbacks(decoder_callbacks);
  filter->setEncoderFilterCallbacks(mocks.encoder_callbacks_);

  // TODO: Add response headers.
  static Envoy::Extensions::HttpFilters::HttpFilterFuzzer fuzzer;
  fuzzer.runData(static_cast<Envoy::Http::StreamDecoderFilter*>(filter.get()),
                 input.request_data());
  fuzzer.reset();

  static ReusableHttpClientFactory http_client_factory;
  auto http_client = http_client_factory.newRawHttpClientImpl(input.result());
  filter = std::make_unique<Filter>(config, std::move(http_client));
  filter->setDecoderFilterCallbacks(decoder_callbacks);
  filter->setEncoderFilterCallbacks(mocks.encoder_callbacks_);

  // TODO: Add response headers.
  fuzzer.runData(static_cast<Envoy::Http::StreamDecoderFilter*>(filter.get()),
                 input.request_data());
  fuzzer.reset();
}

} // namespace
} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
