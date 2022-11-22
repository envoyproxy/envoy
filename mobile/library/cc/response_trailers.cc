#include "response_trailers.h"

namespace Envoy {
namespace Platform {

ResponseTrailersBuilder ResponseTrailers::toResponseTrailersBuilder() {
  ResponseTrailersBuilder builder;
  for (const auto& pair : this->allHeaders()) {
    builder.set(pair.first, pair.second);
  }
  return builder;
}

} // namespace Platform
} // namespace Envoy
