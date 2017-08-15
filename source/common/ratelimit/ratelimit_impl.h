#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/grpc/async_client.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/tracing/context.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/empty_string.h"
#include "common/json/json_loader.h"
#include "common/ratelimit/ratelimit.pb.h"

namespace Envoy {
namespace RateLimit {

typedef Grpc::AsyncClient<pb::lyft::ratelimit::RateLimitRequest,
                          pb::lyft::ratelimit::RateLimitResponse>
    RateLimitAsyncClient;
typedef std::unique_ptr<RateLimitAsyncClient> RateLimitAsyncClientPtr;

typedef Grpc::AsyncRequestCallbacks<pb::lyft::ratelimit::RateLimitResponse> RateLimitAsyncCallbacks;

typedef Grpc::AsyncSpanFinalizerFactory<pb::lyft::ratelimit::RateLimitRequest,
                                        pb::lyft::ratelimit::RateLimitResponse>
    RateLimitSpanFinalizerFactory;

// TODO(htuch): We should have only one client per thread, but today we create one per filter stack.
// This will require support for more than one outstanding request per client (limit() assumes only
// one today).
class GrpcClientImpl : public Client, public RateLimitAsyncCallbacks {
public:
  GrpcClientImpl(RateLimitAsyncClientPtr&& async_client,
                 const Optional<std::chrono::milliseconds>& timeout);
  ~GrpcClientImpl();

  static void createRequest(pb::lyft::ratelimit::RateLimitRequest& request,
                            const std::string& domain, const std::vector<Descriptor>& descriptors);

  // RateLimit::Client
  void cancel() override;
  void limit(RequestCallbacks& callbacks, const std::string& domain,
             const std::vector<Descriptor>& descriptors, const std::string& request_id,
             Tracing::Span& parent_span) override;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override;
  void onSuccess(std::unique_ptr<pb::lyft::ratelimit::RateLimitResponse>&& response) override;
  void onFailure(Grpc::Status::GrpcStatus status) override;

private:
  const Protobuf::MethodDescriptor& service_method_;
  std::unique_ptr<RateLimitAsyncClient> async_client_;
  Grpc::AsyncRequest* request_{};
  Optional<std::chrono::milliseconds> timeout_;
  RequestCallbacks* callbacks_{};
  std::string request_id_ = "";
  std::unique_ptr<RateLimitSpanFinalizerFactory> finalizer_factory_;
};

class GrpcFactoryImpl : public ClientFactory {
public:
  GrpcFactoryImpl(const Json::Object& config, Upstream::ClusterManager& cm);

  // RateLimit::ClientFactory
  ClientPtr create(const Optional<std::chrono::milliseconds>& timeout) override;

private:
  const std::string cluster_name_;
  Upstream::ClusterManager& cm_;
};

class NullClientImpl : public Client {
public:
  // RateLimit::Client
  void cancel() override {}
  void limit(RequestCallbacks& callbacks, const std::string&, const std::vector<Descriptor>&,
             const std::string&, Tracing::Span&) override {
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

class RateLimitSpanFinalizerFactoryImpl : public RateLimitSpanFinalizerFactory {
public:
  RateLimitSpanFinalizerFactoryImpl() {}

  Tracing::SpanFinalizerPtr create(const pb::lyft::ratelimit::RateLimitRequest& request,
                                   const pb::lyft::ratelimit::RateLimitResponse* response) override;
};

} // namespace RateLimit
} // namespace Envoy
