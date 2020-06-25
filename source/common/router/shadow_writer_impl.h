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
              const Http::AsyncClient::RequestOptions& options) override;

  // Http::AsyncClient::Callbacks
  void onSuccess(const Http::AsyncClient::Request&, Http::ResponseMessagePtr&&) override {}
  void onFailure(const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason) override {}
  void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                    const Http::ResponseHeaderMap*) override {}

private:
  Upstream::ClusterManager& cm_;
};

} // namespace Router
} // namespace Envoy
