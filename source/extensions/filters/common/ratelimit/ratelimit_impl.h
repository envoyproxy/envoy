#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/service/ratelimit/v2/rls.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/singleton/const_singleton.h"

#include "extensions/filters/common/ratelimit/ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

typedef Grpc::TypedAsyncRequestCallbacks<envoy::service::ratelimit::v2::RateLimitResponse>
    RateLimitAsyncCallbacks;

struct ConstantValues {
  const std::string TraceStatus = "ratelimit_status";
  const std::string TraceOverLimit = "over_limit";
  const std::string TraceOk = "ok";
};

typedef ConstSingleton<ConstantValues> Constants;

/**
 * Shared state that is owned by the per-thread rate limit clients.
 */
struct SharedState {
  SharedState(Grpc::AsyncClientFactoryPtr&& factory,
              const absl::optional<std::chrono::milliseconds>& timeout_ms)
      : factory_(std::move(factory)), timeout_ms_(timeout_ms) {}

  Grpc::AsyncClientFactoryPtr factory_;
  const absl::optional<std::chrono::milliseconds> timeout_ms_;
};

typedef std::shared_ptr<SharedState> SharedStateSharedPtr;

/**
 * Per Thread Rate Limit client implementation.
 */
class GrpcTlsClientImpl : public Client,
                          public RateLimitAsyncCallbacks,
                          public ThreadLocal::ThreadLocalObject,
                          public Logger::Loggable<Logger::Id::config> {
public:
  GrpcTlsClientImpl(const SharedStateSharedPtr& shared_state);
  ~GrpcTlsClientImpl();

  static void createRequest(envoy::service::ratelimit::v2::RateLimitRequest& request,
                            const std::string& domain,
                            const std::vector<Envoy::RateLimit::Descriptor>& descriptors);

  // Filters::Common::RateLimit::Client
  void cancel() override;
  void limit(RequestCallbacks& callbacks, const std::string& domain,
             const std::vector<Envoy::RateLimit::Descriptor>& descriptors,
             Tracing::Span& parent_span) override;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::HeaderMap&) override {}
  void onSuccess(std::unique_ptr<envoy::service::ratelimit::v2::RateLimitResponse>&& response,
                 Tracing::Span& span) override;
  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span& span) override;

private:
  const Protobuf::MethodDescriptor& service_method_;
  Grpc::AsyncClientPtr async_client_;
  Grpc::AsyncRequest* request_{};
  SharedStateSharedPtr shared_state_;
  RequestCallbacks* callbacks_{};
};

/**
 * Implementation of Client that manages per-thread clients. This is registered with Singleton
 * Manager so that all filters can reuse it.
 */
class GrpcClientImpl : public Client,
                       public RateLimitAsyncCallbacks,
                       public Singleton::Instance,
                       public Logger::Loggable<Logger::Id::config> {
public:
  GrpcClientImpl(Grpc::AsyncClientFactoryPtr&& factory,
                 const absl::optional<std::chrono::milliseconds>& timeout,
                 ThreadLocal::SlotAllocator& tls);
  ~GrpcClientImpl() {}

  static void createRequest(envoy::service::ratelimit::v2::RateLimitRequest& request,
                            const std::string& domain,
                            const std::vector<Envoy::RateLimit::Descriptor>& descriptors);

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::HeaderMap&) override {}
  void onSuccess(std::unique_ptr<envoy::service::ratelimit::v2::RateLimitResponse>&& response,
                 Tracing::Span& span) override {
    tls_slot_->getTyped<GrpcTlsClientImpl>().onSuccess(std::move(response), span);
  }
  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span& span) override {
    tls_slot_->getTyped<GrpcTlsClientImpl>().onFailure(status, message, span);
  }

  // Filters::Common::RateLimit::Client
  void cancel() override { tls_slot_->getTyped<GrpcTlsClientImpl>().cancel(); }

  void limit(RequestCallbacks& callbacks, const std::string& domain,
             const std::vector<Envoy::RateLimit::Descriptor>& descriptors,
             Tracing::Span& parent_span) override {
    tls_slot_->getTyped<GrpcTlsClientImpl>().limit(callbacks, domain, descriptors, parent_span);
  }

private:
  ThreadLocal::SlotPtr tls_slot_;
};

class GrpcFactoryImpl : public ClientFactory {
public:
  GrpcFactoryImpl(const envoy::config::ratelimit::v2::RateLimitServiceConfig& config,
                  Grpc::AsyncClientManager& async_client_manager,
                  ThreadLocal::SlotAllocator& tls_slot, Stats::Scope& scope);

  // Filters::Common::RateLimit::ClientFactory
  ClientPtr create(const absl::optional<std::chrono::milliseconds>& timeout,
                   Server::Configuration::FactoryContext& context) override;

private:
  Grpc::AsyncClientFactoryPtr async_client_factory_;
  ThreadLocal::SlotAllocator& tls_;
};

class NullClientImpl : public Client {
public:
  // Filters::Common::RateLimit::Client
  void cancel() override {}
  void limit(RequestCallbacks& callbacks, const std::string&,
             const std::vector<Envoy::RateLimit::Descriptor>&, Tracing::Span&) override {
    callbacks.complete(LimitStatus::OK, nullptr);
  }
};

class NullFactoryImpl : public ClientFactory {
public:
  // Filters::Common::RateLimit::ClientFactory
  ClientPtr create(const absl::optional<std::chrono::milliseconds>&,
                   Server::Configuration::FactoryContext&) override {
    return ClientPtr{new NullClientImpl()};
  }
};

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
