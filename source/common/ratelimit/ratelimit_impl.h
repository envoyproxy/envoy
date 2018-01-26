#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/singleton/const_singleton.h"

#include "source/common/ratelimit/ratelimit.pb.h"

namespace Envoy {
namespace RateLimit {

typedef Grpc::TypedAsyncRequestCallbacks<pb::lyft::ratelimit::RateLimitResponse>
    RateLimitAsyncCallbacks;

struct ConstantValues {
  const std::string TraceStatus = "ratelimit_status";
  const std::string TraceOverLimit = "over_limit";
  const std::string TraceOk = "ok";
};

typedef ConstSingleton<ConstantValues> Constants;

// TODO(htuch): We should have only one client per thread, but today we create one per filter stack.
// This will require support for more than one outstanding request per client (limit() assumes only
// one today).
class GrpcClientImpl : public Client, public RateLimitAsyncCallbacks {
public:
  GrpcClientImpl(Grpc::AsyncClientPtr&& async_client,
                 const Optional<std::chrono::milliseconds>& timeout);
  ~GrpcClientImpl();

  static void createRequest(pb::lyft::ratelimit::RateLimitRequest& request,
                            const std::string& domain, const std::vector<Descriptor>& descriptors);

  // RateLimit::Client
  void cancel() override;
  void limit(RequestCallbacks& callbacks, const std::string& domain,
             const std::vector<Descriptor>& descriptors, Tracing::Span& parent_span) override;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::HeaderMap&) override {}
  void onSuccess(std::unique_ptr<pb::lyft::ratelimit::RateLimitResponse>&& response,
                 Tracing::Span& span) override;
  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span& span) override;

private:
  const Protobuf::MethodDescriptor& service_method_;
  Grpc::AsyncClientPtr async_client_;
  Grpc::AsyncRequest* request_{};
  Optional<std::chrono::milliseconds> timeout_;
  RequestCallbacks* callbacks_{};
};

class GrpcFactoryImpl : public ClientFactory {
public:
  GrpcFactoryImpl(const envoy::config::ratelimit::v2::RateLimitServiceConfig& config,
                  Grpc::AsyncClientManager& async_client_manager, Stats::Scope& scope);

  // RateLimit::ClientFactory
  ClientPtr create(const Optional<std::chrono::milliseconds>& timeout) override;

private:
  Grpc::AsyncClientFactoryPtr async_client_factory_;
};

class NullClientImpl : public Client {
public:
  // RateLimit::Client
  void cancel() override {}
  void limit(RequestCallbacks& callbacks, const std::string&, const std::vector<Descriptor>&,
             Tracing::Span&) override {
    callbacks.complete(LimitStatus::OK);
  }
};

class NullFactoryImpl : public ClientFactory {
public:
  // RateLimit::ClientFactory
  ClientPtr create(const Optional<std::chrono::milliseconds>&) override {
    return ClientPtr{new NullClientImpl()};
  }
};

} // namespace RateLimit
} // namespace Envoy
