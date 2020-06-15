#include "extensions/filters/common/ratelimit/ratelimit_impl.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/common/ratelimit/v3/ratelimit.pb.h"
#include "envoy/service/ratelimit/v3/rls.pb.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/config/version_converter.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

namespace {
// The fully-qualified name template for the rate-limit service's ShouldRateLimit method.
constexpr char METHOD_NAME_TEMPLATE[] =
    "envoy.service.ratelimit.{}.RateLimitService.ShouldRateLimit";

} // namespace

GrpcClientImpl::GrpcClientImpl(Grpc::RawAsyncClientPtr&& async_client,
                               const absl::optional<std::chrono::milliseconds>& timeout,
                               envoy::config::core::v3::ApiVersion transport_api_version)
    : async_client_(std::move(async_client)), timeout_(timeout),
      service_method_(Config::VersionUtil::getMethodDescriptorForVersion(METHOD_NAME_TEMPLATE,
                                                                         transport_api_version)),
      transport_api_version_(transport_api_version) {}

GrpcClientImpl::~GrpcClientImpl() { ASSERT(!callbacks_); }

void GrpcClientImpl::cancel() {
  ASSERT(callbacks_ != nullptr);
  request_->cancel();
  callbacks_ = nullptr;
}

void GrpcClientImpl::createRequest(envoy::service::ratelimit::v3::RateLimitRequest& request,
                                   const std::string& domain,
                                   const std::vector<Envoy::RateLimit::Descriptor>& descriptors) {
  request.set_domain(domain);
  for (const Envoy::RateLimit::Descriptor& descriptor : descriptors) {
    envoy::extensions::common::ratelimit::v3::RateLimitDescriptor* new_descriptor =
        request.add_descriptors();
    for (const Envoy::RateLimit::DescriptorEntry& entry : descriptor.entries_) {
      envoy::extensions::common::ratelimit::v3::RateLimitDescriptor::Entry* new_entry =
          new_descriptor->add_entries();
      new_entry->set_key(entry.key_);
      new_entry->set_value(entry.value_);
    }
  }
}

void GrpcClientImpl::limit(RequestCallbacks& callbacks, const std::string& domain,
                           const std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                           Tracing::Span& parent_span) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;

  envoy::service::ratelimit::v3::RateLimitRequest request;
  createRequest(request, domain, descriptors);

  request_ = async_client_->send(service_method_, request, *this, parent_span,
                                 Http::AsyncClient::RequestOptions().setTimeout(timeout_),
                                 transport_api_version_);
}

void GrpcClientImpl::onSuccess(
    std::unique_ptr<envoy::service::ratelimit::v3::RateLimitResponse>&& response,
    Tracing::Span& span) {
  LimitStatus status = LimitStatus::OK;
  ASSERT(response->overall_code() != envoy::service::ratelimit::v3::RateLimitResponse::UNKNOWN);
  if (response->overall_code() == envoy::service::ratelimit::v3::RateLimitResponse::OVER_LIMIT) {
    status = LimitStatus::OverLimit;
    span.setTag(Constants::get().TraceStatus, Constants::get().TraceOverLimit);
  } else {
    span.setTag(Constants::get().TraceStatus, Constants::get().TraceOk);
  }

  Http::ResponseHeaderMapPtr response_headers_to_add;
  Http::RequestHeaderMapPtr request_headers_to_add;
  if (!response->response_headers_to_add().empty()) {
    response_headers_to_add = std::make_unique<Http::ResponseHeaderMapImpl>();
    for (const auto& h : response->response_headers_to_add()) {
      response_headers_to_add->addCopy(Http::LowerCaseString(h.key()), h.value());
    }
  }

  if (!response->request_headers_to_add().empty()) {
    request_headers_to_add = std::make_unique<Http::RequestHeaderMapImpl>();
    for (const auto& h : response->request_headers_to_add()) {
      request_headers_to_add->addCopy(Http::LowerCaseString(h.key()), h.value());
    }
  }
  callbacks_->complete(status, std::move(response_headers_to_add),
                       std::move(request_headers_to_add));
  callbacks_ = nullptr;
}

void GrpcClientImpl::onFailure(Grpc::Status::GrpcStatus status, const std::string&,
                               Tracing::Span&) {
  ASSERT(status != Grpc::Status::WellKnownGrpcStatus::Ok);
  callbacks_->complete(LimitStatus::Error, nullptr, nullptr);
  callbacks_ = nullptr;
}

ClientPtr rateLimitClient(Server::Configuration::FactoryContext& context,
                          const envoy::config::core::v3::GrpcService& grpc_service,
                          const std::chrono::milliseconds timeout,
                          envoy::config::core::v3::ApiVersion transport_api_version) {
  // TODO(ramaraochavali): register client to singleton when GrpcClientImpl supports concurrent
  // requests.
  const auto async_client_factory =
      context.clusterManager().grpcAsyncClientManager().factoryForGrpcService(
          grpc_service, context.scope(), true);
  return std::make_unique<Filters::Common::RateLimit::GrpcClientImpl>(
      async_client_factory->create(), timeout, transport_api_version);
}

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
