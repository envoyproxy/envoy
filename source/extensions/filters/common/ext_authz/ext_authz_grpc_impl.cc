#include "extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"

#include "envoy/access_log/access_log.h"
#include "envoy/ssl/connection.h"

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

GrpcClientImpl::GrpcClientImpl(Grpc::AsyncClientPtr&& async_client,
                               const absl::optional<std::chrono::milliseconds>& timeout)
    : service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          // TODO(dio): Define the following service method name as a constant value.
          "envoy.service.auth.v2alpha.Authorization.Check")),
      async_client_(std::move(async_client)), timeout_(timeout) {}

GrpcClientImpl::~GrpcClientImpl() { ASSERT(!callbacks_); }

void GrpcClientImpl::cancel() {
  ASSERT(callbacks_ != nullptr);
  request_->cancel();
  callbacks_ = nullptr;
}

void GrpcClientImpl::check(RequestCallbacks& callbacks,
                           const envoy::service::auth::v2alpha::CheckRequest& request,
                           Tracing::Span& parent_span) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;

  request_ = async_client_->send(service_method_, request, *this, parent_span, timeout_);
}

void GrpcClientImpl::onSuccess(
    std::unique_ptr<envoy::service::auth::v2alpha::CheckResponse>&& response, Tracing::Span& span) {
  ASSERT(response->status().code() != Grpc::Status::GrpcStatus::Unknown);
  ResponsePtr authz_response = std::make_unique<Response>(Response{});

  if (response->status().code() == Grpc::Status::GrpcStatus::Ok) {
    span.setTag(Constants::get().TraceStatus, Constants::get().TraceOk);
    authz_response->status = CheckStatus::OK;
    if (response->has_ok_response()) {
      for (const auto& header : response->ok_response().headers()) {
        authz_response->headers.emplace_back(
            std::make_pair(Http::LowerCaseString(header.first), header.second));
      }
    }
  } else {
    span.setTag(Constants::get().TraceStatus, Constants::get().TraceUnauthz);
    authz_response->status = CheckStatus::Denied;
    if (response->has_denied_response()) {
      for (const auto& header : response->denied_response().headers()) {
        authz_response->headers.emplace_back(
            std::make_pair(Http::LowerCaseString(header.first), header.second));
      }
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
  ASSERT(status != Grpc::Status::GrpcStatus::Ok);
  ResponsePtr authz_response = std::make_unique<Response>(Response{});
  authz_response->status = CheckStatus::Error;
  callbacks_->onComplete(std::move(authz_response));
  callbacks_ = nullptr;
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
