#include "source/common/router/shadow_writer_impl.h"

#include <chrono>
#include <string>

#include "source/common/common/assert.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"

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
                              const Http::AsyncClient::RequestOptions& options) {
  const auto thread_local_cluster =
      getClusterAndPreprocessHeadersAndOptions(cluster, request->headers(), options);
  if (thread_local_cluster == nullptr) {
    return;
  }
  // This is basically fire and forget. We don't handle cancelling.
  thread_local_cluster->httpAsyncClient().send(std::move(request), *this, options);
}

Http::AsyncClient::OngoingRequest*
ShadowWriterImpl::streamingShadow(const std::string& cluster, Http::RequestHeaderMapPtr&& headers,
                                  const Http::AsyncClient::RequestOptions& options) {
  const auto thread_local_cluster =
      getClusterAndPreprocessHeadersAndOptions(cluster, *headers, options);
  if (thread_local_cluster == nullptr) {
    return nullptr;
  }
  return thread_local_cluster->httpAsyncClient().startRequest(std::move(headers), *this, options);
}

Upstream::ThreadLocalCluster* ShadowWriterImpl::getClusterAndPreprocessHeadersAndOptions(
    absl::string_view cluster, Http::RequestHeaderMap& headers,
    const Http::AsyncClient::RequestOptions& options) {
  // It's possible that the cluster specified in the route configuration no longer exists due
  // to a CDS removal. Check that it still exists before shadowing.
  // TODO(mattklein123): Optimally we would have a stat but for now just fix the crashing issue.
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

} // namespace Router
} // namespace Envoy
