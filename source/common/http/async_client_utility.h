#pragma once

#include "envoy/http/async_client.h"

namespace Envoy {
namespace Http {

/**
 * Keeps track of active async HTTP requests to be able to cancel them on destruction.
 */
class AsyncClientRequestTracker {
public:
  /**
   * Cancels all known active async HTTP requests.
   */
  ~AsyncClientRequestTracker();
  /**
   * Includes a given async HTTP request into a set of known active requests.
   * @param request request handle
   */
  void add(AsyncClient::Request& request);
  /**
   * Excludes a given async HTTP request from a set of known active requests.
   *
   * NOTE: Asymmetry between signatures of add() and remove() is caused by the difference
   *       between contexts in which these methods will be used.
   *       add() will be called right after AsyncClient::send() when request.cancel() is
   *       perfectly valid and desirable.
   *       However, remove() will be called in the context of
   *       AsyncClient::Callbacks::[onSuccess | onFailure] where request.cancel() is no longer
   *       expected and therefore get prevented by means of "const" modifier.
   *
   * @param request request handle
   */
  void remove(const AsyncClient::Request& request);

private:
  // Track active async HTTP requests to be able to cancel them on destruction.
  absl::flat_hash_set<AsyncClient::Request*> active_requests_;
};

} // namespace Http
} // namespace Envoy
