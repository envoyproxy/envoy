#include "shadow_writer_impl.h"

#include "common/common/assert.h"
#include "common/http/headers.h"

namespace Router {

void ShadowWriterImpl::shadow(const std::string& cluster, Http::MessagePtr&& request,
                              std::chrono::milliseconds timeout) {
  // Switch authority to add a shadow postfix. This allows upstream logging to make a more sense.
  std::string host = request->headers().get(Http::Headers::get().Host);
  ASSERT(!host.empty());
  host += "-shadow";
  request->headers().replaceViaMoveValue(Http::Headers::get().Host, std::move(host));

  // Configuration should guarantee that cluster exists before calling here. This is basically
  // fire and forget. We don't handle cancelling.
  cm_.httpAsyncClientForCluster(cluster)
      .send(std::move(request), *this, Optional<std::chrono::milliseconds>(timeout));
}

} // Router
