#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/ratelimit/ratelimit.h"
#include "envoy/server/filter_config.h"
#include "envoy/service/ratelimit/v3/rls.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/tracing/tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/grpc/typed_async_client.h"
#include "source/common/singleton/const_singleton.h"
#include "source/extensions/filters/common/ratelimit/ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RateLimit {

using RateLimitAsyncCallbacks =
    Grpc::AsyncRequestCallbacks<envoy::service::ratelimit::v3::RateLimitResponse>;

struct ConstantValues {
  const std::string TraceStatus = "ratelimit_status";
  const std::string TraceOverLimit = "over_limit";
  const std::string TraceOk = "ok";
};

using Constants = ConstSingleton<ConstantValues>;

// TODO(htuch): We should have only one client per thread, but today we create one per filter stack.
// This will require support for more than one outstanding request per client (limit() assumes only
// one today).
class GrpcClientImpl : public Client,
                       public RateLimitAsyncCallbacks,
                       public Logger::Loggable<Logger::Id::config> {
public:
  GrpcClientImpl(const Grpc::RawAsyncClientSharedPtr& async_client,
                 const absl::optional<std::chrono::milliseconds>& timeout);
  ~GrpcClientImpl() override;

  static void createRequest(envoy::service::ratelimit::v3::RateLimitRequest& request,
                            const std::string& domain,
                            const std::vector<Envoy::RateLimit::Descriptor>& descriptors,
                            uint32_t hits_addend);

  // Filters::Common::RateLimit::Client
  void cancel() override;
  void limit(RequestCallbacks& callbacks, const std::string& domain,
             const std::vector<Envoy::RateLimit::Descriptor>& descriptors,
             Tracing::Span& parent_span, const StreamInfo::StreamInfo& stream_info,
             uint32_t hits_addend = 0) override;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onSuccess(std::unique_ptr<envoy::service::ratelimit::v3::RateLimitResponse>&& response,
                 Tracing::Span& span) override;
  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span& span) override;

private:
  Grpc::AsyncClient<envoy::service::ratelimit::v3::RateLimitRequest,
                    envoy::service::ratelimit::v3::RateLimitResponse>
      async_client_;
  Grpc::AsyncRequest* request_{};
  absl::optional<std::chrono::milliseconds> timeout_;
  RequestCallbacks* callbacks_{};
  const Protobuf::MethodDescriptor& service_method_;
};

/**
 * Builds the rate limit client.
 */
ClientPtr rateLimitClient(Server::Configuration::FactoryContext& context,
                          const Grpc::GrpcServiceConfigWithHashKey& config_with_hash_key,
                          const std::chrono::milliseconds timeout);

} // namespace RateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
