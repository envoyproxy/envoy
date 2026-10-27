#pragma once

#include <chrono>
#include <string>

#include "envoy/event/deferred_deletable.h"
#include "envoy/event/dispatcher.h"
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
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) override {}
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override {}
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
 * Async client callbacks of one mirror request of a policy with stats. They record how the mirror
 * ended. The async client always calls onBeforeFinalizeUpstreamSpan() once when the request ends,
 * before onSuccess() or onFailure() and also when the request is cancelled, so it schedules the
 * deletion of this object. A request deleted without onSuccess() or onFailure() was cancelled.
 */
class ShadowRequestCallbacks : public Http::AsyncClient::Callbacks,
                               public Event::DeferredDeletable {
public:
  ShadowRequestCallbacks(ShadowPolicyStatsSharedPtr stats, Event::Dispatcher& dispatcher)
      : stats_(std::move(stats)), dispatcher_(dispatcher) {}
  ~ShadowRequestCallbacks() override;

  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) override;
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override;
  void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&, const Http::ResponseHeaderMap*) override;

  /**
   * Records a failure and deletes the callbacks if the async client did not take ownership of
   * the request, which happens when the request cannot be created.
   */
  void onStartFailed();

private:
  const ShadowPolicyStatsSharedPtr stats_;
  Event::Dispatcher& dispatcher_;
  bool finalized_{};
  bool outcome_recorded_{};
};

} // namespace Router
} // namespace Envoy
