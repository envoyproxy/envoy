#include "common/router/shadow_writer_impl.h"

#include <chrono>
#include <string>

#include "common/common/assert.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Router {

void ShadowWriterImpl::shadow(const std::string& cluster, Http::MessagePtr&& request,
                              std::chrono::milliseconds timeout) {
  // Switch authority to add a shadow postfix. This allows upstream logging to make a more sense.
  // TODO PERF: Avoid copy.
  std::string host = request->headers().Host()->value().c_str();
  ASSERT(!host.empty());
  host += "-shadow";
  request->headers().Host()->value(host);

  // Configuration should guarantee that cluster exists before calling here. This is basically
  // fire and forget. We don't handle cancelling.
  cm_.httpAsyncClientForCluster(cluster).send(std::move(request), *this,
                                              Optional<std::chrono::milliseconds>(timeout));
}

} // namespace Router
} // namespace Envoy
