#include "request_trailers.h"

namespace Envoy {
namespace Platform {

RequestTrailersBuilder RequestTrailers::to_request_trailers_builder() const {
  RequestTrailersBuilder builder;
  for (const auto& pair : this->all_headers()) {
    builder.set(pair.first, pair.second);
  }
  return builder;
}

} // namespace Platform
} // namespace Envoy
