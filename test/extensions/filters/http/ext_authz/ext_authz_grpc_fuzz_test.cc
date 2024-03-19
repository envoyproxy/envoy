#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"

#include "source/common/http/context_impl.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/filters/http/ext_authz/ext_authz.h"

#include "test/extensions/filters/common/ext_authz/mocks.h"
#include "test/extensions/filters/common/ext_authz/test_common.h"
#include "test/extensions/filters/http/common/fuzz/http_filter_fuzzer.h"
#include "test/extensions/filters/http/ext_authz/ext_authz_fuzz.pb.validate.h"
#include "test/extensions/filters/http/ext_authz/ext_authz_fuzz_lib.h"
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

DEFINE_PROTO_FUZZER(const envoy::extensions::filters::http::ext_authz::ExtAuthzTestCase& input) {
  static ReusableFuzzerUtil fuzzer_util;
  static ReusableGrpcClientFactory grpc_client_factory;
  auto grpc_client = grpc_client_factory.newGrpcClientImpl(input.result());
  absl::StatusOr<std::unique_ptr<Filter>> filter = fuzzer_util.setup(input, std::move(grpc_client));
  if (!filter.ok()) {
    return;
  }

  // TODO: Add response headers.
  static Envoy::Extensions::HttpFilters::HttpFilterFuzzer fuzzer;
  fuzzer.runData(static_cast<Envoy::Http::StreamDecoderFilter*>(filter->get()),
                 input.request_data());
}

} // namespace
} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
