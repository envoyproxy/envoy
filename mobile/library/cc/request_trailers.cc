#include "request_trailers.h"

namespace Envoy {
namespace Platform {

RequestTrailersBuilder RequestTrailers::toRequestTrailersBuilder() const {
  RequestTrailersBuilder builder;
  for (const auto& pair : this->allHeaders()) {
    builder.set(pair.first, pair.second);
  }
  return builder;
}

} // namespace Platform
} // namespace Envoy
