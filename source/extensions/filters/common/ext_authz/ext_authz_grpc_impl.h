#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3/ext_authz.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/protocol.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/grpc/typed_async_client.h"

#include "extensions/filters/common/ext_authz/check_request_utils.h"
#include "extensions/filters/common/ext_authz/ext_authz.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

using ExtAuthzAsyncCallbacks = Grpc::AsyncRequestCallbacks<envoy::service::auth::v3::CheckResponse>;

/*
 * This client implementation is used when the Ext_Authz filter needs to communicate with an gRPC
 * authorization server. Unlike the HTTP client, the gRPC allows the server to define response
 * objects which contain the HTTP attributes to be sent to the upstream or to the downstream client.
 * The gRPC client does not rewrite path. NOTE: We create gRPC client for each filter stack instead
 * of a client per thread. That is ok since this is unary RPC and the cost of doing this is minimal.
 */
class GrpcClientImpl : public Client,
                       public ExtAuthzAsyncCallbacks,
                       public Logger::Loggable<Logger::Id::ext_authz> {
public:
  GrpcClientImpl(Grpc::RawAsyncClientSharedPtr async_client,
                 const absl::optional<std::chrono::milliseconds>& timeout,
                 envoy::config::core::v3::ApiVersion transport_api_version);
  ~GrpcClientImpl() override;

  // ExtAuthz::Client
  void cancel() override;
  void check(RequestCallbacks& callbacks, Event::Dispatcher& dispatcher,
             const envoy::service::auth::v3::CheckRequest& request, Tracing::Span& parent_span,
             const StreamInfo::StreamInfo& stream_info) override;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onSuccess(std::unique_ptr<envoy::service::auth::v3::CheckResponse>&& response,
                 Tracing::Span& span) override;
  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span& span) override;

private:
  void onTimeout();
  void respondFailure(Filters::Common::ExtAuthz::ErrorKind kind);
  void toAuthzResponseHeader(
      ResponsePtr& response,
      const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption>& headers);

  Grpc::AsyncClient<envoy::service::auth::v3::CheckRequest, envoy::service::auth::v3::CheckResponse>
      async_client_;
  Grpc::AsyncRequest* request_{};
  absl::optional<std::chrono::milliseconds> timeout_;
  RequestCallbacks* callbacks_{};
  const Protobuf::MethodDescriptor& service_method_;
  const envoy::config::core::v3::ApiVersion transport_api_version_;
  Event::TimerPtr timeout_timer_;
};

using GrpcClientImplPtr = std::unique_ptr<GrpcClientImpl>;

// The client cache for RawAsyncClient for Google grpc so channel is not created for each request.
// TODO(fpliu233): The cache will cause resource leak that a new channel is created every time a new
// config is pushed. Improve gRPC channel cache with better solution.
class AsyncClientCache : public Singleton::Instance {
public:
  AsyncClientCache(Grpc::AsyncClientManager& async_client_manager, Stats::Scope& scope,
                   ThreadLocal::SlotAllocator& tls)
      : async_client_manager_(async_client_manager), scope_(scope), tls_slot_(tls.allocateSlot()) {
    tls_slot_->set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalCache>(); });
  }

  const Grpc::RawAsyncClientSharedPtr getOrCreateAsyncClient(
      const envoy::extensions::filters::http::ext_authz::v3::ExtAuthz& proto_config);

private:
  /**
   * Per-thread cache.
   */
  struct ThreadLocalCache : public ThreadLocal::ThreadLocalObject {
    ThreadLocalCache() = default;
    // The client cache stored with key as hash of
    // envoy::config::core::v3::GrpcService::GoogleGrpc config.
    // TODO(fpliu233): Remove when the cleaner and generic solution for gRPC is live.
    absl::flat_hash_map<std::size_t, Grpc::RawAsyncClientSharedPtr> async_clients_;
  };

  Grpc::AsyncClientManager& async_client_manager_;
  Stats::Scope& scope_;
  ThreadLocal::SlotPtr tls_slot_;
};

using AsyncClientCacheSharedPtr = std::shared_ptr<AsyncClientCache>;

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
