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

#include "server/configuration_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

// Singleton registration via macro defined in envoy/singleton/manager.h
SINGLETON_MANAGER_REGISTRATION(ratelimit_client);

ClientFactoryPtr
ClientFactory ::rateLimitClientFactory(Server::Configuration::FactoryContext& context) {
  // TODO(ramaraochavali): figure out a way to get the singleton name here.
  Envoy::Server::Configuration::RateLimitServiceConfigPtr ratelimit_config =
      context.singletonManager().getTyped<Envoy::Server::Configuration::RateLimitServiceConfig>(
          "ratelimit_config_singleton", [] { return nullptr; });
  if (ratelimit_config) {
    return std::make_unique<Envoy::Extensions::Filters::Common::RateLimit::GrpcFactoryImpl>(
        ratelimit_config->config_, context.clusterManager().grpcAsyncClientManager(),
        context.threadLocal(), context.scope());
  }
  return std::make_unique<Envoy::Extensions::Filters::Common::RateLimit::NullFactoryImpl>();
}

GrpcClientImpl::GrpcClientImpl(Grpc::AsyncClientFactoryPtr&& factory,
                               const absl::optional<std::chrono::milliseconds>& timeout,
                               ThreadLocal::SlotAllocator& tls)
    : tls_slot_(tls.allocateSlot()) {
  SharedStateSharedPtr shared_state = std::make_shared<SharedState>(std::move(factory), timeout);
  tls_slot_->set([shared_state](Event::Dispatcher&) {
    return std::make_shared<GrpcTlsClientImpl>(shared_state);
  });
}

GrpcTlsClientImpl::GrpcTlsClientImpl(const SharedStateSharedPtr& shared_state)
    : service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.ratelimit.v2.RateLimitService.ShouldRateLimit")),
      async_client_(shared_state->factory_->create()), shared_state_(shared_state) {}

GrpcTlsClientImpl::~GrpcTlsClientImpl() { ASSERT(!callbacks_); }

void GrpcTlsClientImpl::cancel() {
  ASSERT(callbacks_ != nullptr);
  request_->cancel();
  callbacks_ = nullptr;
}

void GrpcTlsClientImpl::createRequest(
    envoy::service::ratelimit::v2::RateLimitRequest& request, const std::string& domain,
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

void GrpcTlsClientImpl::limit(RequestCallbacks& callbacks, const std::string& domain,
                              const std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                              Tracing::Span& parent_span) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;

  envoy::service::ratelimit::v2::RateLimitRequest request;
  createRequest(request, domain, descriptors);
  request_ =
      async_client_->send(service_method_, request, *this, parent_span, shared_state_->timeout_ms_);
}

void GrpcTlsClientImpl::onSuccess(
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

void GrpcTlsClientImpl::onFailure(Grpc::Status::GrpcStatus status, const std::string&,
                                  Tracing::Span&) {
  ASSERT(status != Grpc::Status::GrpcStatus::Ok);
  callbacks_->complete(LimitStatus::Error, nullptr);
  callbacks_ = nullptr;
}

GrpcFactoryImpl::GrpcFactoryImpl(const envoy::config::ratelimit::v2::RateLimitServiceConfig& config,
                                 Grpc::AsyncClientManager& async_client_manager,
                                 ThreadLocal::SlotAllocator& tls, Stats::Scope& scope)
    : tls_(tls) {
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
  return context.singletonManager().getTyped<GrpcClientImpl>(
      SINGLETON_MANAGER_REGISTERED_NAME(ratelimit_client), [timeout, this] {
        return std::make_shared<GrpcClientImpl>(std::move(async_client_factory_), timeout, tls_);
      });
}

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
