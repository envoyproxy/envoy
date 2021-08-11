#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "envoy/http/async_client.h"
#include "envoy/http/message.h"

namespace Envoy {
namespace Router {

/**
 * Interface used to shadow complete requests to an alternate upstream cluster in a "fire and
 * forget" fashion. Right now this interface takes a fully buffered request and cannot be used for
 * streaming. This is sufficient for current use cases.
 */
class ShadowWriter {
public:
  virtual ~ShadowWriter() = default;

  /**
   * Shadow a request.
   * @param cluster supplies the cluster name to shadow to.
   * @param message supplies the complete request to shadow.
   * @param timeout supplies the shadowed request timeout.
   */
  virtual void shadow(const std::string& cluster, Http::RequestMessagePtr&& request,
                      const Http::AsyncClient::RequestOptions& options) PURE;
};

using ShadowWriterPtr = std::unique_ptr<ShadowWriter>;

} // namespace Router
} // namespace Envoy
