#include "shadow_writer_impl.h"

namespace Router {

void ShadowWriterImpl::shadow(const std::string& cluster, Http::MessagePtr&& request,
                              std::chrono::milliseconds timeout) {
  // Configuration should guarantee that cluster exists before calling here. This is basically
  // fire and forget. We don't handle cancelling.
  cm_.httpAsyncClientForCluster(cluster)
      .send(std::move(request), *this, Optional<std::chrono::milliseconds>(timeout));
}

} // Router
