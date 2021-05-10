#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
//#include "envoy/ratelimit/ratelimit.h"
#include "envoy/server/filter_config.h"
//#include "envoy/service/ratelimit/v3/rls.pb.h"
#include "envoy/stats/scope.h"
//#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/grpc/typed_async_client.h"
#include "common/singleton/const_singleton.h"

//#include "extensions/filters/common/ratelimit/ratelimit.h"
#include "extensions/filters/network/sip_proxy/loadbalancer/loadbalancer.h"
#include "api/envoy/extensions/filters/network/sip_proxy/v3/load_balancer.pb.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace LoadBalancer {

// TODO(htuch): We should have only one client per thread, but today we create one per filter stack.
// This will require support for more than one outstanding request per client (limit() assumes only
// one today).
class GrpcClientImpl : public Client,
                       public LoadBalancerAsyncCallbacks,
                       public Logger::Loggable<Logger::Id::config> {
public:
  GrpcClientImpl(Grpc::RawAsyncClientPtr&& async_client,
                 const absl::optional<std::chrono::milliseconds>& timeout,
                 envoy::config::core::v3::ApiVersion transport_api_version);
  ~GrpcClientImpl() override;

  static void createRequest(envoy::extensions::filters::network::sip_proxy::v3::TraRequest& request,
                            const std::string& fqdn);

  // Filters::Common::LoadBalancer::Client
  void cancel() override;
  void nodes(RequestCallbacks& callbacks, const std::string& fqdn,
             Tracing::Span& parent_span) override;

  // Grpc::AsyncRequestCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap&) override {}
  void onSuccess(
      std::unique_ptr<envoy::extensions::filters::network::sip_proxy::v3::TraResponse>&& response,
      Tracing::Span& span) override;
  void onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                 Tracing::Span& span) override;

private:
  Grpc::AsyncClient<envoy::extensions::filters::network::sip_proxy::v3::TraRequest,
                    envoy::extensions::filters::network::sip_proxy::v3::TraResponse>
      async_client_;
  Grpc::AsyncRequest* request_{};
  absl::optional<std::chrono::milliseconds> timeout_;
  RequestCallbacks* callbacks_{};
  const Protobuf::MethodDescriptor& service_method_;
  const envoy::config::core::v3::ApiVersion transport_api_version_;
};

} // namespace LoadBalancer
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
