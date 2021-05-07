#include "request_trailers_builder.h"

namespace Envoy {
namespace Platform {

RequestTrailers RequestTrailersBuilder::build() const {
  return RequestTrailers(this->allHeaders());
}

} // namespace Platform
} // namespace Envoy
