#include "library/cc/request_trailers_builder.h"

namespace Envoy {
namespace Platform {

RequestTrailers RequestTrailersBuilder::build() const { return RequestTrailers(allHeaders()); }

} // namespace Platform
} // namespace Envoy
