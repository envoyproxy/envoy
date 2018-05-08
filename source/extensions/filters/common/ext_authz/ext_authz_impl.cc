#include "extensions/filters/common/ext_authz/ext_authz_impl.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/ssl/connection.h"

#include "common/common/assert.h"
#include "common/grpc/async_client_impl.h"
#include "common/http/headers.h"
#include "common/protobuf/protobuf.h"
#include "extensions/filters/common/auth/attribute_context.h"

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
  CheckStatus status = CheckStatus::OK;
  ASSERT(response->status().code() != Grpc::Status::GrpcStatus::Unknown);
  if (response->status().code() != Grpc::Status::GrpcStatus::Ok) {
    status = CheckStatus::Denied;
    span.setTag(Constants::get().TraceStatus, Constants::get().TraceUnauthz);
  } else {
    span.setTag(Constants::get().TraceStatus, Constants::get().TraceOk);
  }

  callbacks_->onComplete(status);
  callbacks_ = nullptr;
}

void GrpcClientImpl::onFailure(Grpc::Status::GrpcStatus status, const std::string&,
                               Tracing::Span&) {
  ASSERT(status != Grpc::Status::GrpcStatus::Ok);
  callbacks_->onComplete(CheckStatus::Error);
  callbacks_ = nullptr;
}

void CheckRequestUtils::createHttpCheck(const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
                                        const Envoy::Http::HeaderMap& headers,
                                        envoy::service::auth::v2alpha::CheckRequest& request) {

  auto attrs = request.mutable_attributes();

  Envoy::Http::StreamDecoderFilterCallbacks* cb =
      const_cast<Envoy::Http::StreamDecoderFilterCallbacks*>(callbacks);

  auto hdr = headers.EnvoyDownstreamServiceCluster();
  std::string service = hdr ? std::string(hdr->value().getStringView()) : "";

  Auth::AttributeContextUtils::setSourcePeer(*attrs, *cb->connection(), service);
  Auth::AttributeContextUtils::setDestinationPeer(*attrs, *cb->connection());
  Auth::AttributeContextUtils::setHttpRequest(*attrs, callbacks, headers);
}

void CheckRequestUtils::createTcpCheck(const Network::ReadFilterCallbacks* callbacks,
                                       envoy::service::auth::v2alpha::CheckRequest& request) {

  auto attrs = request.mutable_attributes();

  Network::ReadFilterCallbacks* cb = const_cast<Network::ReadFilterCallbacks*>(callbacks);
  Auth::AttributeContextUtils::setSourcePeer(*attrs, cb->connection(), "");
  Auth::AttributeContextUtils::setDestinationPeer(*attrs, cb->connection());
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
