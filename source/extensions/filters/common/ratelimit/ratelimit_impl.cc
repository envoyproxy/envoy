#include "source/extensions/filters/common/ratelimit/ratelimit_impl.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/extensions/common/ratelimit/v3/ratelimit.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

GrpcClientImpl::GrpcClientImpl(const Grpc::RawAsyncClientSharedPtr& async_client,
                               const absl::optional<std::chrono::milliseconds>& timeout)
    : async_client_(async_client), timeout_(timeout),
      service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.ratelimit.v3.RateLimitService.ShouldRateLimit")) {}

GrpcClientImpl::~GrpcClientImpl() { ASSERT(!callbacks_); }

void GrpcClientImpl::cancel() {
  ASSERT(callbacks_ != nullptr);
  request_->cancel();
  callbacks_ = nullptr;
}

void GrpcClientImpl::createRequest(envoy::service::ratelimit::v3::RateLimitRequest& request,
                                   const std::string& domain,
                                   const std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                                   uint32_t hits_addend) {
  request.set_domain(domain);
  request.set_hits_addend(hits_addend);
  for (const Envoy::RateLimit::Descriptor& descriptor : descriptors) {
    envoy::extensions::common::ratelimit::v3::RateLimitDescriptor* new_descriptor =
        request.add_descriptors();
    for (const Envoy::RateLimit::DescriptorEntry& entry : descriptor.entries_) {
      envoy::extensions::common::ratelimit::v3::RateLimitDescriptor::Entry* new_entry =
          new_descriptor->add_entries();
      new_entry->set_key(entry.key_);
      new_entry->set_value(entry.value_);
    }
    if (descriptor.limit_) {
      envoy::extensions::common::ratelimit::v3::RateLimitDescriptor_RateLimitOverride* new_limit =
          new_descriptor->mutable_limit();
      new_limit->set_requests_per_unit(descriptor.limit_.value().requests_per_unit_);
      new_limit->set_unit(descriptor.limit_.value().unit_);
    }
  }
}

void GrpcClientImpl::limit(RequestCallbacks& callbacks, const std::string& domain,
                           const std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                           Tracing::Span& parent_span, const StreamInfo::StreamInfo& stream_info,
                           uint32_t hits_addend) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;

  envoy::service::ratelimit::v3::RateLimitRequest request;
  createRequest(request, domain, descriptors, hits_addend);

  request_ =
      async_client_->send(service_method_, request, *this, parent_span,
                          Http::AsyncClient::RequestOptions().setTimeout(timeout_).setParentContext(
                              Http::AsyncClient::ParentContext{&stream_info}));
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
    response_headers_to_add = Http::ResponseHeaderMapImpl::create();
    for (const auto& h : response->response_headers_to_add()) {
      response_headers_to_add->addCopy(Http::LowerCaseString(h.key()), h.value());
    }
  }

  if (!response->request_headers_to_add().empty()) {
    request_headers_to_add = Http::RequestHeaderMapImpl::create();
    for (const auto& h : response->request_headers_to_add()) {
      request_headers_to_add->addCopy(Http::LowerCaseString(h.key()), h.value());
    }
  }

  DescriptorStatusListPtr descriptor_statuses = std::make_unique<DescriptorStatusList>(
      response->statuses().begin(), response->statuses().end());
  DynamicMetadataPtr dynamic_metadata =
      response->has_dynamic_metadata()
          ? std::make_unique<ProtobufWkt::Struct>(response->dynamic_metadata())
          : nullptr;
  callbacks_->complete(status, std::move(descriptor_statuses), std::move(response_headers_to_add),
                       std::move(request_headers_to_add), response->raw_body(),
                       std::move(dynamic_metadata));
  callbacks_ = nullptr;
}

void GrpcClientImpl::onFailure(Grpc::Status::GrpcStatus status, const std::string& msg,
                               Tracing::Span&) {
  ASSERT(status != Grpc::Status::WellKnownGrpcStatus::Ok);
  ENVOY_LOG_TO_LOGGER(Logger::Registry::getLog(Logger::Id::filter), debug,
                      "rate limit fail, status={} msg={}", status, msg);
  callbacks_->complete(LimitStatus::Error, nullptr, nullptr, nullptr, EMPTY_STRING, nullptr);
  callbacks_ = nullptr;
}

ClientPtr rateLimitClient(Server::Configuration::FactoryContext& context,
                          const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
                          const std::chrono::milliseconds timeout) {
  // TODO(ramaraochavali): register client to singleton when GrpcClientImpl supports concurrent
  // requests.
  auto client_or_error =
      context.serverFactoryContext()
          .clusterManager()
          .grpcAsyncClientManager()
          .getOrCreateRawAsyncClientWithHashKey(config_with_hash_key, context.scope(), true);
  THROW_IF_NOT_OK_REF(client_or_error.status());
  return std::make_unique<Filters::Common::RateLimit::GrpcClientImpl>(client_or_error.value(),
                                                                      timeout);
}

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
