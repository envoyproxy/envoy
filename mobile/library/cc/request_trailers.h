#pragma once

#include "library/cc/request_trailers_builder.h"
#include "library/cc/trailers.h"

namespace Envoy {
namespace Platform {

class RequestTrailersBuilder;

class RequestTrailers : public Trailers {
public:
  RequestTrailersBuilder toRequestTrailersBuilder() const;

private:
  RequestTrailers(RawHeaderMap headers) : Trailers(std::move(headers)) {}

  friend class RequestTrailersBuilder;
};

using RequestTrailersSharedPtr = std::shared_ptr<RequestTrailers>;

} // namespace Platform
} // namespace Envoy
