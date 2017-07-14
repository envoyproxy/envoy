#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/grpc/async_client.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/tracing/context.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/json/json_loader.h"
#include "common/ratelimit/ratelimit.pb.h"

namespace Envoy {
namespace RateLimit {

typedef Grpc::AsyncClient<pb::lyft::ratelimit::RateLimitRequest,
                          pb::lyft::ratelimit::RateLimitResponse>
    RateLimitAsyncClient;
typedef std::unique_ptr<RateLimitAsyncClient> RateLimitAsyncClientPtr;

typedef Grpc::AsyncClientStream<pb::lyft::ratelimit::RateLimitRequest> RateLimitAsyncStream;
typedef Grpc::AsyncClientCallbacks<pb::lyft::ratelimit::RateLimitResponse> RateLimitAsyncCallbacks;

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
             const std::vector<Descriptor>& descriptors,
             const Tracing::TransportContext& context) override;

  // Grpc::AsyncClientCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::HeaderMapPtr&&) override {}
  void onReceiveMessage(std::unique_ptr<pb::lyft::ratelimit::RateLimitResponse>&& message) override;
  void onReceiveTrailingMetadata(Http::HeaderMapPtr&&) override {}
  void onRemoteClose(Grpc::Status::GrpcStatus status) override;

private:
  const Protobuf::MethodDescriptor& service_method_;
  std::unique_ptr<RateLimitAsyncClient> async_client_;
  RateLimitAsyncStream* stream_{};
  Optional<std::chrono::milliseconds> timeout_;
  RequestCallbacks* callbacks_{};
  std::unique_ptr<pb::lyft::ratelimit::RateLimitResponse> response_;
  Tracing::TransportContext context_;
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
             const Tracing::TransportContext&) override {
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
