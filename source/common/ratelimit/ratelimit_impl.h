#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/grpc/rpc_channel.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/tracing/context.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/json/json_loader.h"
#include "common/ratelimit/ratelimit.pb.h"

namespace Envoy {
namespace RateLimit {

class GrpcClientImpl : public Client, public Grpc::RpcChannelCallbacks {
public:
  GrpcClientImpl(Grpc::RpcChannelFactory& factory,
                 const Optional<std::chrono::milliseconds>& timeout);
  ~GrpcClientImpl();

  static void createRequest(pb::lyft::ratelimit::RateLimitRequest& request,
                            const std::string& domain, const std::vector<Descriptor>& descriptors);

  // RateLimit::Client
  void cancel() override;
  void limit(RequestCallbacks& callbacks, const std::string& domain,
             const std::vector<Descriptor>& descriptors,
             const Tracing::TransportContext& context) override;

  // Grpc::RpcChannelCallbacks
  void onPreRequestCustomizeHeaders(Http::HeaderMap&) override;
  void onSuccess() override;
  void onFailure(const Optional<uint64_t>& grpc_status, const std::string& message) override;

private:
  Grpc::RpcChannelPtr channel_;
  pb::lyft::ratelimit::RateLimitService::Stub service_;
  RequestCallbacks* callbacks_{};
  pb::lyft::ratelimit::RateLimitResponse response_;
  Tracing::TransportContext context_;
};

class GrpcFactoryImpl : public ClientFactory, public Grpc::RpcChannelFactory {
public:
  GrpcFactoryImpl(const Json::Object& config, Upstream::ClusterManager& cm);

  // RateLimit::ClientFactory
  ClientPtr create(const Optional<std::chrono::milliseconds>& timeout) override;

  // Grpc::RpcChannelFactory
  Grpc::RpcChannelPtr create(Grpc::RpcChannelCallbacks& callbacks,
                             const Optional<std::chrono::milliseconds>& timeout) override;

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
