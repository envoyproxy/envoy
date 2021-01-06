#pragma once

#include "response_trailers_builder.h"
#include "trailers.h"

namespace Envoy {
namespace Platform {

class ResponseTrailersBuilder;

class ResponseTrailers : public Trailers {
public:
  ResponseTrailersBuilder to_response_trailers_builder();

private:
  ResponseTrailers(RawHeaderMap trailers) : Trailers(std::move(trailers)) {}

  friend class ResponseTrailersBuilder;
};

using ResponseTrailersSharedPtr = std::shared_ptr<ResponseTrailers>;

} // namespace Platform
} // namespace Envoy
