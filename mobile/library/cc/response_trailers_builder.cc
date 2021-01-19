#include "response_trailers_builder.h"

namespace Envoy {
namespace Platform {

ResponseTrailersSharedPtr ResponseTrailersBuilder::build() const {
  ResponseTrailers* trailers = new ResponseTrailers(this->all_headers());
  return std::shared_ptr<ResponseTrailers>(trailers);
}

} // namespace Platform
} // namespace Envoy
