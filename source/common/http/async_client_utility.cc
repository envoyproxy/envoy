#include "common/http/async_client_utility.h"

namespace Envoy {
namespace Http {

AsyncClientRequestTracker::~AsyncClientRequestTracker() {
  for (auto* active_request : active_requests_) {
    active_request->cancel();
  }
}

void AsyncClientRequestTracker::add(AsyncClient::Request& request) {
  ASSERT(active_requests_.find(&request) == active_requests_.end());
  active_requests_.insert(&request);
}

void AsyncClientRequestTracker::remove(const AsyncClient::Request& request) {
  auto it = active_requests_.find(const_cast<AsyncClient::Request*>(&request));
  // Support a use case where request callbacks might get called prior to a request handle
  // is returned from AsyncClient::send().
  if (it != active_requests_.end()) {
    active_requests_.erase(it);
  }
}

} // namespace Http
} // namespace Envoy
