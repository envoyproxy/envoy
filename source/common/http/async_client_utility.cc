#include "common/http/async_client_utility.h"

namespace Envoy {
namespace Http {

AsyncClientRequestTracker::~AsyncClientRequestTracker() {
  for (auto* active_request : active_requests_) {
    active_request->cancel();
  }
}

AsyncClientRequestTracker& AsyncClientRequestTracker::operator+=(AsyncClient::Request* request) {
  // Let client code to avoid conditionals.
  if (request) {
    ASSERT(active_requests_.find(request) == active_requests_.end());
    active_requests_.insert(request);
  }
  return *this;
}

AsyncClientRequestTracker&
AsyncClientRequestTracker::operator-=(const AsyncClient::Request* request) {
  // Let client code to avoid conditionals.
  if (request) {
    auto it = active_requests_.find(const_cast<AsyncClient::Request*>(request));
    // Support a use case where request callbacks might get called prior to a request handle
    // is returned from AsyncClient::send().
    if (it != active_requests_.end()) {
      active_requests_.erase(it);
    }
  }
  return *this;
}

} // namespace Http
} // namespace Envoy
