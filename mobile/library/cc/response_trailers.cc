#include "response_trailers.h"

namespace Envoy {
namespace Platform {

ResponseTrailersBuilder ResponseTrailers::to_response_trailers_builder() {
  ResponseTrailersBuilder builder;
  for (const auto& pair : this->all_headers()) {
    builder.set(pair.first, pair.second);
  }
  return builder;
}

} // namespace Platform
} // namespace Envoy
