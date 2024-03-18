#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"

#include "source/common/http/context_impl.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/filters/http/ext_authz/ext_authz.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/http/common/fuzz/http_filter_fuzzer.h"
#include "test/extensions/filters/http/ext_authz/ext_authz_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/server_factory_context.h"

#include "gmock/gmock.h"

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
  NiceMock<Grpc::MockAsyncRequest> async_request_;
  Envoy::Tracing::MockSpan mock_span_;
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

  auto internal_mock_client = std::make_shared<NiceMock<Grpc::MockAsyncClient>>();
  auto grpc_client = new Filters::Common::ExtAuthz::GrpcClientImpl(internal_mock_client,
                                                                   std::chrono::milliseconds(1000));
  auto filter = std::make_unique<Filter>(config, Filters::Common::ExtAuthz::ClientPtr{grpc_client});

  // Set metadata context.
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks;
  ON_CALL(decoder_callbacks, connection())
      .WillByDefault(Return(OptRef<const Network::Connection>{mocks.connection_}));
  envoy::config::core::v3::Metadata metadata = input.filter_metadata();
  ON_CALL(decoder_callbacks.stream_info_, dynamicMetadata())
      .WillByDefault(testing::ReturnRef(metadata));

  filter->setDecoderFilterCallbacks(decoder_callbacks);
  filter->setEncoderFilterCallbacks(mocks.encoder_callbacks_);

  // Set check result default action.
  ON_CALL(*internal_mock_client, sendRaw(_, _, _, _, _, _))
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

            const Grpc::Status::WellKnownGrpcStatus status = resultCaseToGrpcStatus(input.result());
            if (status == Grpc::Status::WellKnownGrpcStatus::Ok) {
              grpc_client->onSuccess(makeGrpcCheckResponse(status), mocks.mock_span_);
            } else {
              grpc_client->onFailure(status, "Fuzz input status was not ok!", mocks.mock_span_);
            }
            return &mocks.async_request_;
          }));

  // TODO: Add response headers.
  Envoy::Extensions::HttpFilters::HttpFilterFuzzer fuzzer;
  fuzzer.runData(static_cast<Envoy::Http::StreamDecoderFilter*>(filter.get()),
                 input.request_data());
}

} // namespace
} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
