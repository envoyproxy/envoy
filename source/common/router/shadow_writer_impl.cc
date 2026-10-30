#include "source/common/router/shadow_writer_impl.h"

#include <chrono>
#include <string>

#include "envoy/router/router.h"

#include "source/common/common/assert.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Router {

namespace {

std::string shadowAppendedHost(absl::string_view host) {
  ASSERT(!host.empty());
  // Switch authority to add a shadow postfix. This allows upstream logging to
  // make more sense.
  absl::string_view::size_type port_start = Http::HeaderUtility::getPortStart(host);
  if (port_start == absl::string_view::npos) {
    return absl::StrCat(host, "-shadow");
  }
  return absl::StrCat(host.substr(0, port_start), "-shadow",
                      host.substr(port_start, host.length()));
}

} // namespace

void ShadowWriterImpl::shadow(const std::string& cluster, Http::RequestMessagePtr&& request,
                              const Http::AsyncClient::RequestOptions& options,
                              const ShadowPolicyStatsSharedPtr& stats) {
  const auto thread_local_cluster =
      getClusterAndPreprocessHeadersAndOptions(cluster, request->headers(), options);
  if (thread_local_cluster == nullptr) {
    if (stats != nullptr) {
      stats->rq_no_cluster_.inc();
    }
    return;
  }
  Http::AsyncClient& async_client = thread_local_cluster->httpAsyncClient();
  if (stats == nullptr) {
    // This is basically fire and forget. We don't handle cancelling.
    async_client.send(std::move(request), *this, options);
    return;
  }
  stats->rq_started_.inc();
  auto* callbacks = new ShadowRequestCallbacks(stats, async_client.dispatcher());
  if (async_client.send(std::move(request), *callbacks, options) == nullptr) {
    callbacks->onStartFailed();
  }
}

Http::AsyncClient::OngoingRequest*
ShadowWriterImpl::streamingShadow(const std::string& cluster, Http::RequestHeaderMapPtr&& headers,
                                  const Http::AsyncClient::RequestOptions& options,
                                  const ShadowPolicyStatsSharedPtr& stats) {
  const auto thread_local_cluster =
      getClusterAndPreprocessHeadersAndOptions(cluster, *headers, options);
  if (thread_local_cluster == nullptr) {
    if (stats != nullptr) {
      stats->rq_no_cluster_.inc();
    }
    return nullptr;
  }
  Http::AsyncClient& async_client = thread_local_cluster->httpAsyncClient();
  if (stats == nullptr) {
    return async_client.startRequest(std::move(headers), *this, options);
  }
  stats->rq_started_.inc();
  auto* callbacks = new ShadowRequestCallbacks(stats, async_client.dispatcher());
  Http::AsyncClient::OngoingRequest* request =
      async_client.startRequest(std::move(headers), *callbacks, options);
  if (request == nullptr) {
    callbacks->onStartFailed();
  }
  return request;
}

Upstream::ThreadLocalCluster* ShadowWriterImpl::getClusterAndPreprocessHeadersAndOptions(
    absl::string_view cluster, Http::RequestHeaderMap& headers,
    const Http::AsyncClient::RequestOptions& options) {
  // It's possible that the cluster specified in the route configuration no longer exists due
  // to a CDS removal. Check that it still exists before shadowing.
  const auto thread_local_cluster = cm_.getThreadLocalCluster(cluster);
  if (thread_local_cluster == nullptr) {
    ENVOY_LOG(debug, "shadow cluster '{}' does not exist", cluster);
    return nullptr;
  }

  // Append "-shadow" suffix if option is disabled.
  if (!options.is_shadow_suffixed_disabled) {
    headers.setHost(shadowAppendedHost(headers.getHostValue()));
  }

  const_cast<Http::AsyncClient::RequestOptions&>(options).setIsShadow(true);
  return thread_local_cluster;
}

ShadowRequestCallbacks::~ShadowRequestCallbacks() {
  if (!outcome_recorded_) {
    stats_->rq_cancelled_.inc();
  }
}

void ShadowRequestCallbacks::onSuccess(const Http::AsyncClient::Request&,
                                       Http::ResponseMessagePtr&& response) {
  outcome_recorded_ = true;
  const std::optional<uint64_t> status =
      Http::Utility::getResponseStatusOrNullopt(response->headers());
  if (status.has_value() && Http::CodeUtility::is2xx(status.value())) {
    stats_->rq_success_.inc();
  } else {
    stats_->rq_failure_.inc();
  }
}

void ShadowRequestCallbacks::onFailure(const Http::AsyncClient::Request&,
                                       Http::AsyncClient::FailureReason) {
  outcome_recorded_ = true;
  stats_->rq_failure_.inc();
}

void ShadowRequestCallbacks::onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                                          const Http::ResponseHeaderMap*) {
  if (!finalized_) {
    finalized_ = true;
    dispatcher_.deferredDelete(Event::DeferredDeletablePtr{this});
  }
}

void ShadowRequestCallbacks::onStartFailed() {
  if (finalized_) {
    return;
  }
  outcome_recorded_ = true;
  stats_->rq_failure_.inc();
  delete this;
}

} // namespace Router
} // namespace Envoy
