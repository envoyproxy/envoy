#include "extensions/filters/common/ratelimit/ratelimit_impl.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/api/v2/ratelimit/ratelimit.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/singleton/manager.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(ratelimit_client);

GrpcClientImpl::GrpcClientImpl(Grpc::AsyncClientPtr&& async_client,
                               const absl::optional<std::chrono::milliseconds>& timeout)
    : service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.ratelimit.v2.RateLimitService.ShouldRateLimit")),
      async_client_(std::move(async_client)), timeout_(timeout) {}

GrpcClientImpl::~GrpcClientImpl() { ASSERT(!callbacks_); }

void GrpcClientImpl::cancel() {
  ASSERT(callbacks_ != nullptr);
  request_->cancel();
  callbacks_ = nullptr;
}

void GrpcClientImpl::createRequest(envoy::service::ratelimit::v2::RateLimitRequest& request,
                                   const std::string& domain,
                                   const std::vector<Envoy::RateLimit::Descriptor>& descriptors) {
  request.set_domain(domain);
  for (const Envoy::RateLimit::Descriptor& descriptor : descriptors) {
    envoy::api::v2::ratelimit::RateLimitDescriptor* new_descriptor = request.add_descriptors();
    for (const Envoy::RateLimit::DescriptorEntry& entry : descriptor.entries_) {
      envoy::api::v2::ratelimit::RateLimitDescriptor::Entry* new_entry =
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

  envoy::service::ratelimit::v2::RateLimitRequest request;
  createRequest(request, domain, descriptors);

  request_ = async_client_->send(service_method_, request, *this, parent_span, timeout_);
}

void GrpcClientImpl::onSuccess(
    std::unique_ptr<envoy::service::ratelimit::v2::RateLimitResponse>&& response,
    Tracing::Span& span) {
  LimitStatus status = LimitStatus::OK;
  ASSERT(response->overall_code() != envoy::service::ratelimit::v2::RateLimitResponse_Code_UNKNOWN);
  if (response->overall_code() ==
      envoy::service::ratelimit::v2::RateLimitResponse_Code_OVER_LIMIT) {
    status = LimitStatus::OverLimit;
    span.setTag(Constants::get().TraceStatus, Constants::get().TraceOverLimit);
  } else {
    span.setTag(Constants::get().TraceStatus, Constants::get().TraceOk);
  }

  Http::HeaderMapPtr headers = std::make_unique<Http::HeaderMapImpl>();
  if (response->headers_size()) {
    for (const auto& h : response->headers()) {
      headers->addCopy(Http::LowerCaseString(h.key()), h.value());
    }
  }
  callbacks_->complete(status, std::move(headers));
  callbacks_ = nullptr;
}

void GrpcClientImpl::onFailure(Grpc::Status::GrpcStatus status, const std::string&,
                               Tracing::Span&) {
  ASSERT(status != Grpc::Status::GrpcStatus::Ok);
  callbacks_->complete(LimitStatus::Error, nullptr);
  callbacks_ = nullptr;
}

ClientFactoryPtr
ClientFactory ::rateLimitClientFactory(Server::Configuration::FactoryContext& context) {
  // TODO(ramaraochavali): Figure out how to get the registered name here.
  Envoy::RateLimit::RateLimitServiceConfigSharedPtr ratelimit_service_config =
      context.singletonManager().getTyped<Envoy::RateLimit::RateLimitServiceConfig>(
          "ratelimit_service_config_singleton_name", [] { return nullptr; });
  ClientFactoryPtr client_factory;
  if (ratelimit_service_config) {
    return std::make_unique<Envoy::Extensions::Filters::Common::RateLimit::GrpcFactoryImpl>(
        ratelimit_service_config->config_, context.clusterManager().grpcAsyncClientManager(),
        context.scope());
  }
  return std::make_unique<Envoy::Extensions::Filters::Common::RateLimit::NullFactoryImpl>();
}

GrpcFactoryImpl::GrpcFactoryImpl(const envoy::config::ratelimit::v2::RateLimitServiceConfig& config,
                                 Grpc::AsyncClientManager& async_client_manager,
                                 Stats::Scope& scope) {
  envoy::api::v2::core::GrpcService grpc_service;
  grpc_service.MergeFrom(config.grpc_service());
  // TODO(htuch): cluster_name is deprecated, remove after 1.6.0.
  if (config.service_specifier_case() ==
      envoy::config::ratelimit::v2::RateLimitServiceConfig::kClusterName) {
    grpc_service.mutable_envoy_grpc()->set_cluster_name(config.cluster_name());
  }
  async_client_factory_ = async_client_manager.factoryForGrpcService(grpc_service, scope, false);
}

ClientPtr GrpcFactoryImpl::create(const absl::optional<std::chrono::milliseconds>& timeout,
                                  Server::Configuration::FactoryContext& context) {
  ClientPtr ratelimit_client =
      context.singletonManager().getTyped<Envoy::Extensions::Filters::Common::RateLimit::Client>(
          SINGLETON_MANAGER_REGISTERED_NAME(ratelimit_client), [timeout, this] {
            return std::make_shared<GrpcClientImpl>(async_client_factory_->create(), timeout);
          });
  return ratelimit_client;
}

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
