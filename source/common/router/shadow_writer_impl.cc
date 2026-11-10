#include "source/common/router/shadow_writer_impl.h"

#include <chrono>
#include <memory>
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

std::shared_ptr<ShadowRequestContext>
attachContext(const Http::AsyncClient::RequestOptions& options,
              const ShadowPolicyStatsSharedPtr& stats) {
  auto context = std::make_shared<ShadowRequestContext>(stats);
  const_cast<Http::AsyncClient::RequestOptions&>(options).setContext(context);
  return context;
}

ShadowRequestContext* shadowContext(const Http::AsyncClient::Request& request) {
  return dynamic_cast<ShadowRequestContext*>(request.context().ptr());
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
  const auto context = attachContext(options, stats);
  if (async_client.send(std::move(request), *this, options) == nullptr) {
    // The request failed inline, or could not be created and the callbacks were not called.
    context->recordFailure();
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
  const auto context = attachContext(options, stats);
  Http::AsyncClient::OngoingRequest* request =
      async_client.startRequest(std::move(headers), *this, options);
  if (request == nullptr) {
    context->recordFailure();
  }
  return request;
}

void ShadowWriterImpl::onSuccess(const Http::AsyncClient::Request& request,
                                 Http::ResponseMessagePtr&& response) {
  if (auto* context = shadowContext(request); context != nullptr) {
    context->recordResponse(*response);
  }
}

void ShadowWriterImpl::onFailure(const Http::AsyncClient::Request& request,
                                 Http::AsyncClient::FailureReason) {
  if (auto* context = shadowContext(request); context != nullptr) {
    context->recordFailure();
  }
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
  if (!outcome_recorded_) {
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
  stats_->rq_failure_.inc();
}

} // namespace Router
} // namespace Envoy
