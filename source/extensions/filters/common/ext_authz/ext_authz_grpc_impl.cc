#include "extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/service/auth/v2alpha/external_auth.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"

#include "common/common/assert.h"
#include "common/grpc/async_client_impl.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

GrpcClientImpl::GrpcClientImpl(Grpc::RawAsyncClientPtr&& async_client,
                               const absl::optional<std::chrono::milliseconds>& timeout,
                               envoy::config::core::v3::ApiVersion transport_api_version,
                               bool use_alpha)
    : async_client_(std::move(async_client)), timeout_(timeout),
      service_method_(Grpc::VersionedMethods("envoy.service.auth.v3.Authorization.Check",
                                             "envoy.service.auth.v2.Authorization.Check",
                                             "envoy.service.auth.v2alpha.Authorization.Check")
                          .getMethodDescriptorForVersion(transport_api_version, use_alpha)),
      transport_api_version_(transport_api_version) {}

GrpcClientImpl::~GrpcClientImpl() { ASSERT(!callbacks_); }

void GrpcClientImpl::cancel() {
  ASSERT(callbacks_ != nullptr);
  request_->cancel();
  callbacks_ = nullptr;
}

void GrpcClientImpl::check(RequestCallbacks& callbacks,
                           const envoy::service::auth::v3::CheckRequest& request,
                           Tracing::Span& parent_span, const StreamInfo::StreamInfo&) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;

  ENVOY_LOG(trace, "Sending CheckRequest: {}", request.DebugString());
  request_ = async_client_->send(service_method_, request, *this, parent_span,
                                 Http::AsyncClient::RequestOptions().setTimeout(timeout_),
                                 transport_api_version_);
}

void GrpcClientImpl::onSuccess(std::unique_ptr<envoy::service::auth::v3::CheckResponse>&& response,
                               Tracing::Span& span) {
  ENVOY_LOG(trace, "Received CheckResponse: {}", response->DebugString());
  ResponsePtr authz_response = std::make_unique<Response>(Response{});
  if (response->status().code() == Grpc::Status::WellKnownGrpcStatus::Ok) {
    span.setTag(TracingConstants::get().TraceStatus, TracingConstants::get().TraceOk);
    authz_response->status = CheckStatus::OK;
    if (response->has_ok_response()) {
      authz_response->dynamic_metadata = response->ok_response().dynamic_metadata();
      toAuthzResponseHeader(authz_response, response->ok_response().headers());
    }
  } else {
    span.setTag(TracingConstants::get().TraceStatus, TracingConstants::get().TraceUnauthz);
    authz_response->status = CheckStatus::Denied;
    if (response->has_denied_response()) {
      toAuthzResponseHeader(authz_response, response->denied_response().headers());
      authz_response->status_code =
          static_cast<Http::Code>(response->denied_response().status().code());
      authz_response->body = response->denied_response().body();
    } else {
      authz_response->status_code = Http::Code::Forbidden;
    }
  }

  callbacks_->onComplete(std::move(authz_response));
  callbacks_ = nullptr;
}

void GrpcClientImpl::onFailure(Grpc::Status::GrpcStatus status, const std::string&,
                               Tracing::Span&) {
  ENVOY_LOG(trace, "CheckRequest call failed with status: {}",
            Grpc::Utility::grpcStatusToString(status));
  ASSERT(status != Grpc::Status::WellKnownGrpcStatus::Ok);
  Response response{};
  response.status = CheckStatus::Error;
  response.status_code = Http::Code::Forbidden;
  callbacks_->onComplete(std::make_unique<Response>(response));
  callbacks_ = nullptr;
}

void GrpcClientImpl::toAuthzResponseHeader(
    ResponsePtr& response,
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption>& headers) {
  for (const auto& header : headers) {
    if (header.append().value()) {
      response->headers_to_append.emplace_back(Http::LowerCaseString(header.header().key()),
                                               header.header().value());
    } else {
      response->headers_to_set.emplace_back(Http::LowerCaseString(header.header().key()),
                                            header.header().value());
    }
  }
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
