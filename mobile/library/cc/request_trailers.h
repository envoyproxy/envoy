#pragma once

#include "request_trailers_builder.h"
#include "trailers.h"

namespace Envoy {
namespace Platform {

class RequestTrailersBuilder;

class RequestTrailers : public Trailers {
public:
  RequestTrailersBuilder to_request_trailers_builder() const;

private:
  RequestTrailers(RawHeaderMap headers) : Trailers(std::move(headers)) {}

  friend class RequestTrailersBuilder;
};

using RequestTrailersSharedPtr = std::shared_ptr<RequestTrailers>;

} // namespace Platform
} // namespace Envoy
