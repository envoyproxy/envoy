#pragma once

#include <chrono>
#include <string>

#include "envoy/router/shadow_writer.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Router {

/**
 * Implementation of ShadowWriter that takes incoming requests to shadow and implements "fire and
 * forget" behavior using an async client.
 */
class ShadowWriterImpl : Logger::Loggable<Logger::Id::router>,
                         public ShadowWriter,
                         public Http::AsyncClient::Callbacks {
public:
  ShadowWriterImpl(Upstream::ClusterManager& cm) : cm_(cm) {}

  // Router::ShadowWriter
  void shadow(const std::string& cluster, Http::RequestMessagePtr&& request,
              const Http::AsyncClient::RequestOptions& options,
              const ShadowPolicyStatsSharedPtr& stats) override;

  Http::AsyncClient::OngoingRequest*
  streamingShadow(const std::string& cluster, Http::RequestHeaderMapPtr&& headers,
                  const Http::AsyncClient::RequestOptions& options,
                  const ShadowPolicyStatsSharedPtr& stats) override;

  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request& request,
                 Http::ResponseMessagePtr&& response) override;
  void onFailure(const Http::AsyncClient::Request& request,
                 Http::AsyncClient::FailureReason) override;
  void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                    const Http::ResponseHeaderMap*) override {}

private:
  Upstream::ThreadLocalCluster*
  getClusterAndPreprocessHeadersAndOptions(absl::string_view cluster,
                                           Http::RequestHeaderMap& headers,
                                           const Http::AsyncClient::RequestOptions& options);
  Upstream::ClusterManager& cm_;
};

/**
 * Context of one mirror request of a policy with stats. It records how the mirror ended. The
 * request holds it until it is destroyed, so a context destroyed without an outcome was cancelled.
 */
class ShadowRequestContext : public Http::AsyncClient::RequestContext {
public:
  explicit ShadowRequestContext(ShadowPolicyStatsSharedPtr stats) : stats_(std::move(stats)) {}
  ~ShadowRequestContext() override;

  /**
   * Records a 2xx response as a success and any other response as a failure.
   */
  void recordResponse(Http::ResponseMessage& response);

  /**
   * Records a failure, unless an outcome was already recorded.
   */
  void recordFailure();

private:
  const ShadowPolicyStatsSharedPtr stats_;
  bool outcome_recorded_{};
};

} // namespace Router
} // namespace Envoy
