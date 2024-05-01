#pragma once

#include "library/cc/response_trailers_builder.h"
#include "library/cc/trailers.h"

namespace Envoy {
namespace Platform {

class ResponseTrailersBuilder;

class ResponseTrailers : public Trailers {
public:
  ResponseTrailersBuilder toResponseTrailersBuilder();

private:
  ResponseTrailers(RawHeaderMap trailers) : Trailers(std::move(trailers)) {}

  friend class ResponseTrailersBuilder;
};

using ResponseTrailersSharedPtr = std::shared_ptr<ResponseTrailers>;

} // namespace Platform
} // namespace Envoy
