#include "common/router/shadow_writer_impl.h"

#include <chrono>
#include <string>

#include "common/common/assert.h"
#include "common/http/headers.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Router {

void ShadowWriterImpl::shadow(const std::string& cluster, Http::RequestMessagePtr&& request,
                              const Http::AsyncClient::RequestOptions& options) {
  // It's possible that the cluster specified in the route configuration no longer exists due
  // to a CDS removal. Check that it still exists before shadowing.
  // TODO(mattklein123): Optimally we would have a stat but for now just fix the crashing issue.
  if (!cm_.get(cluster)) {
    ENVOY_LOG(debug, "shadow cluster '{}' does not exist", cluster);
    return;
  }

  ASSERT(!request->headers().Host()->value().empty());
  // Switch authority to add a shadow postfix. This allows upstream logging to make more sense.
  auto parts = StringUtil::splitToken(request->headers().Host()->value().getStringView(), ":");
  ASSERT(!parts.empty() && parts.size() <= 2);
  request->headers().setHost(
      parts.size() == 2
          ? absl::StrJoin(parts, "-shadow:")
          : absl::StrCat(request->headers().Host()->value().getStringView(), "-shadow"));
  // This is basically fire and forget. We don't handle cancelling.
  cm_.httpAsyncClientForCluster(cluster).send(std::move(request), *this, options);
}

} // namespace Router
} // namespace Envoy
