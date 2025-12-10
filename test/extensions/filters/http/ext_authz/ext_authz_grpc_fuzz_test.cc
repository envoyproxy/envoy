#include <memory>

#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.validate.h"
#include "envoy/grpc/status.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "source/common/common/assert.h"
#include "source/extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"
#include "source/extensions/filters/http/ext_authz/ext_authz.h"

#include "test/extensions/filters/common/ext_authz/test_common.h"
#include "test/extensions/filters/http/common/fuzz/http_filter_fuzzer.h"
#include "test/extensions/filters/http/ext_authz/ext_authz_fuzz.pb.h"
#include "test/extensions/filters/http/ext_authz/ext_authz_fuzz.pb.validate.h"
#include "test/extensions/filters/http/ext_authz/ext_authz_fuzz_lib.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/grpc/mocks.h"

#include "gmock/gmock.h"

using Envoy::Extensions::Filters::Common::ExtAuthz::TestCommon;
using envoy::extensions::filters::http::ext_authz::ExtAuthzTestCaseGrpc;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {
namespace {

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

              ENVOY_LOG_MISC(trace, "Check Request attributes:\n{}",
                             check_request.attributes().DebugString());

              if (failure_reason_) {
                grpc_client_->onFailure(Envoy::Grpc::Status::WellKnownGrpcStatus::Unknown,
                                        *failure_reason_, mock_span_);
              } else {
                grpc_client_->onSuccess(std::move(response_), mock_span_);
              }
              return &grpc_async_request_;
            }));
  }

  std::unique_ptr<Filters::Common::ExtAuthz::GrpcClientImpl>
  newGrpcClientImpl(const ExtAuthzTestCaseGrpc& input) {
    if (input.has_failure_reason()) {
      failure_reason_ = input.failure_reason();
      response_ = nullptr;
      ENVOY_LOG_MISC(trace, "Failure reason: {}", *failure_reason_);
    } else {
      failure_reason_ = std::nullopt;
      response_ = std::make_unique<envoy::service::auth::v3::CheckResponse>(input.response());
      ENVOY_LOG_MISC(trace, "Check Response:\n{}", response_->DebugString());
    }

    grpc_client_ = new Filters::Common::ExtAuthz::GrpcClientImpl(internal_grpc_mock_client_,
                                                                 std::chrono::milliseconds(1000));
    return std::unique_ptr<Filters::Common::ExtAuthz::GrpcClientImpl>{grpc_client_};
  }

private:
  std::shared_ptr<NiceMock<Grpc::MockAsyncClient>> internal_grpc_mock_client_;
  Envoy::Tracing::MockSpan mock_span_;
  NiceMock<Grpc::MockAsyncRequest> grpc_async_request_;

  // Set by calling newGrpcClientImpl. Only one of response_ or failure_reason_ will be set.
  std::unique_ptr<envoy::service::auth::v3::CheckResponse> response_;
  absl::optional<std::string> failure_reason_;
  Filters::Common::ExtAuthz::GrpcClientImpl* grpc_client_;
};

DEFINE_PROTO_FUZZER(ExtAuthzTestCaseGrpc& input) {
  static ReusableFuzzerUtil fuzzer_util;
  static ReusableGrpcClientFactory grpc_client_factory;

  try {
    TestUtility::validate(input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during validation: {}", e.what());
    return;
  }

  auto grpc_client = grpc_client_factory.newGrpcClientImpl(input);
  // Force this to be true so that the filter can handle invalid mutations gracefully.
  input.mutable_base()->mutable_config()->set_validate_mutations(true);
  absl::StatusOr<std::unique_ptr<Filter>> filter =
      fuzzer_util.setup(input.base(), std::move(grpc_client));
  if (!filter.ok()) {
    return;
  }

  static Envoy::Extensions::HttpFilters::HttpFilterFuzzer fuzzer;
  fuzzer.runData(static_cast<Envoy::Http::StreamDecoderFilter*>(filter->get()),
                 input.base().request_data());
  fuzzer.runData(static_cast<Envoy::Http::StreamEncoderFilter*>(filter->get()),
                 input.base().response_data());
  fuzzer.reset();
}

} // namespace
} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
