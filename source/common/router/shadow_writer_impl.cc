#include "common/router/shadow_writer_impl.h"

#include <chrono>
#include <string>

#include "common/common/assert.h"
#include "common/http/headers.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Router {

void ShadowWriterImpl::shadow(const std::string& cluster, Http::MessagePtr&& request,
                              std::chrono::milliseconds timeout) {
  ASSERT(!request->headers().Host()->value().empty());
  // Switch authority to add a shadow postfix. This allows upstream logging to make more sense.
  auto parts = StringUtil::splitToken(request->headers().Host()->value().c_str(), ":");
  ASSERT(parts.size() > 0 && parts.size() <= 2);
  request->headers().Host()->value(
      parts.size() == 2 ? absl::StrJoin(parts, "-shadow:")
                        : absl::StrCat(request->headers().Host()->value().c_str(), "-shadow"));
  // Configuration should guarantee that cluster exists before calling here. This is basically
  // fire and forget. We don't handle cancelling.
  cm_.httpAsyncClientForCluster(cluster).send(std::move(request), *this,
                                              Optional<std::chrono::milliseconds>(timeout));
}

} // namespace Router
} // namespace Envoy
