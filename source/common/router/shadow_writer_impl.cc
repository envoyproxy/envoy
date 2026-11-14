#include "source/common/router/shadow_writer_impl.h"

#include <chrono>
#include <memory>
#include <string>

#include "envoy/router/router.h"

#include "source/common/common/assert.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Router {

namespace {

ShadowRequestContext* shadowContext(const Http::AsyncClient::Request& request) {
  return dynamic_cast<ShadowRequestContext*>(request.context().ptr());
}

class ShadowCallbacks : public Http::AsyncClient::Callbacks {
public:
  void onSuccess(const Http::AsyncClient::Request& request,
                 Http::ResponseMessagePtr&& response) override {
    if (auto* context = shadowContext(request); context != nullptr) {
      context->recordResponse(*response);
    }
  }
  void onFailure(const Http::AsyncClient::Request& request,
                 Http::AsyncClient::FailureReason) override {
    if (auto* context = shadowContext(request); context != nullptr) {
      context->recordFailure();
    }
  }
  void onBeforeFinalizeUpstreamSpan(Tracing::Span&, const Http::ResponseHeaderMap*) override {}
};

Http::AsyncClient::Callbacks& shadowCallbacks() {
  // Keep these shared callbacks alive through writer destruction and worker teardown.
  static auto* callbacks = new ShadowCallbacks();
  return *callbacks;
}

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

std::shared_ptr<ShadowRequestContext>
attachContext(const Http::AsyncClient::RequestOptions& options,
              const ShadowPolicyStatsSharedPtr& stats) {
  auto context = std::make_shared<ShadowRequestContext>(stats);
  const_cast<Http::AsyncClient::RequestOptions&>(options).setContext(context);
  return context;
}

} // namespace

Http::AsyncClient::OngoingRequest* ShadowWriterImpl::discover(
    const std::string& cluster, Http::RequestHeaderMapPtr&& headers,
    const Http::AsyncClient::RequestOptions& options, const ShadowPolicyStatsSharedPtr& stats,
    const Upstream::OdCdsContextSharedPtr& discovery_context, bool end_stream) {
  if (discovery_context == nullptr || !discovery_context->clientProvider()) {
    return nullptr;
  }
  auto& client = discovery_context->clientProvider()->get();
  // The normal path rewrites only when the cluster exists. Do the same exactly once here.
  if (!options.is_shadow_suffixed_disabled) {
    headers->setHost(shadowAppendedHost(headers->getHostValue()));
  }
  auto pending_options = options;
  pending_options.setIsShadow(true);
  if (stats == nullptr) {
    return client.startRequest(cluster, std::move(headers), shadowCallbacks(), pending_options,
                               discovery_context, end_stream, [](auto, auto) {});
  }
  const auto context = attachContext(pending_options, stats);
  context->onDiscoveryStart();
  auto* request =
      client.startRequest(cluster, std::move(headers), shadowCallbacks(), pending_options,
                          discovery_context, end_stream, [context](auto status, auto elapsed) {
                            context->onDiscoveryComplete(status, elapsed);
                          });
  if (request == nullptr) {
    context->recordFailure();
  }
  return request;
}

void ShadowWriterImpl::shadow(const std::string& cluster, Http::RequestMessagePtr&& request,
                              const Http::AsyncClient::RequestOptions& options,
                              const ShadowPolicyStatsSharedPtr& stats,
                              const Upstream::OdCdsContextSharedPtr& discovery_context) {
  const auto thread_local_cluster =
      getClusterAndPreprocessHeadersAndOptions(cluster, request->headers(), options);
  if (thread_local_cluster == nullptr) {
    if (stats != nullptr &&
        (discovery_context == nullptr || !discovery_context->clientProvider())) {
      stats->rq_no_cluster_.inc();
    }
    auto* pending = discover(
        cluster, Http::createHeaderMap<Http::RequestHeaderMapImpl>(request->headers()), options,
        stats, discovery_context, request->body().length() == 0 && request->trailers() == nullptr);
    if (pending != nullptr) {
      if (request->body().length() != 0) {
        pending->sendData(request->body(), request->trailers() == nullptr);
      }
      if (request->trailers() != nullptr) {
        pending->sendTrailers(*request->trailers());
      }
    }
    return;
  }
  Http::AsyncClient& async_client = thread_local_cluster->httpAsyncClient();
  if (stats == nullptr) {
    // This is basically fire and forget. We don't handle cancelling.
    async_client.send(std::move(request), shadowCallbacks(), options);
    return;
  }
  stats->rq_started_.inc();
  const auto context = attachContext(options, stats);
  if (async_client.send(std::move(request), shadowCallbacks(), options) == nullptr) {
    // The request failed inline, or could not be created and the callbacks were not called.
    context->recordFailure();
  }
}

Http::AsyncClient::OngoingRequest*
ShadowWriterImpl::streamingShadow(const std::string& cluster, Http::RequestHeaderMapPtr&& headers,
                                  const Http::AsyncClient::RequestOptions& options,
                                  const ShadowPolicyStatsSharedPtr& stats,
                                  const Upstream::OdCdsContextSharedPtr& discovery_context) {
  const auto thread_local_cluster =
      getClusterAndPreprocessHeadersAndOptions(cluster, *headers, options);
  if (thread_local_cluster == nullptr) {
    if (stats != nullptr &&
        (discovery_context == nullptr || !discovery_context->clientProvider())) {
      stats->rq_no_cluster_.inc();
    }
    return discover(cluster, std::move(headers), options, stats, discovery_context, false);
  }
  Http::AsyncClient& async_client = thread_local_cluster->httpAsyncClient();
  if (stats == nullptr) {
    return async_client.startRequest(std::move(headers), shadowCallbacks(), options);
  }
  stats->rq_started_.inc();
  const auto context = attachContext(options, stats);
  Http::AsyncClient::OngoingRequest* request =
      async_client.startRequest(std::move(headers), shadowCallbacks(), options);
  if (request == nullptr) {
    context->recordFailure();
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

ShadowRequestContext::~ShadowRequestContext() {
  if (http_started_ && !outcome_recorded_) {
    stats_->rq_cancelled_.inc();
  }
}

void ShadowRequestContext::recordResponse(Http::ResponseMessage& response) {
  ASSERT(!outcome_recorded_);
  outcome_recorded_ = true;
  const std::optional<uint64_t> status =
      Http::Utility::getResponseStatusOrNullopt(response.headers());
  if (status.has_value() && Http::CodeUtility::is2xx(status.value())) {
    stats_->rq_success_.inc();
  } else {
    stats_->rq_failure_.inc();
  }
}

void ShadowRequestContext::recordFailure() {
  if (outcome_recorded_) {
    return;
  }
  outcome_recorded_ = true;
  if (http_started_) {
    stats_->rq_failure_.inc();
  }
}

void ShadowRequestContext::onDiscoveryStart() {
  http_started_ = false;
  stats_->discovery_attempt_.inc();
  stats_->discovery_pending_.inc();
}

void ShadowRequestContext::onDiscoveryComplete(Http::OnDemandAsyncClient::DiscoveryResult result,
                                               std::chrono::milliseconds elapsed) {
  using DiscoveryResult = Http::OnDemandAsyncClient::DiscoveryResult;
  stats_->discovery_pending_.dec();
  stats_->discovery_time_.recordValue(elapsed.count());
  switch (result) {
  case DiscoveryResult::Cancelled:
    stats_->discovery_cancelled_.inc();
    break;
  case DiscoveryResult::BufferOverflow:
    stats_->discovery_overflow_.inc();
    break;
  case DiscoveryResult::Available:
    stats_->discovery_success_.inc();
    stats_->rq_started_.inc();
    http_started_ = true;
    break;
  case DiscoveryResult::Missing:
    stats_->discovery_missing_.inc();
    stats_->rq_no_cluster_.inc();
    break;
  case DiscoveryResult::Timeout:
    stats_->discovery_timeout_.inc();
    stats_->rq_no_cluster_.inc();
    break;
  }
}

} // namespace Router
} // namespace Envoy
