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
              std::chrono::milliseconds timeout) override;

  // Http::AsyncClient::Callbacks
  void onSuccess(Http::ResponseMessagePtr&&) override {}
  void onFailure(Http::AsyncClient::FailureReason) override {}

private:
  Upstream::ClusterManager& cm_;
};

} // namespace Router
} // namespace Envoy
