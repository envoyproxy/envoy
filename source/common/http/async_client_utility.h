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
   */
  AsyncClientRequestTracker& add(AsyncClient::Request* request);
  /**
   * Excludes a given async HTTP request from a set of known active requests.
   */
  AsyncClientRequestTracker& remove(const AsyncClient::Request* request);

private:
  // Track active async HTTP requests to be able to cancel them on destruction.
  absl::flat_hash_set<AsyncClient::Request*> active_requests_;
};

} // namespace Http
} // namespace Envoy
