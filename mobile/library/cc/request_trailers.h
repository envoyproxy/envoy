#pragma once

// NOLINT(namespace-envoy)

#include "request_trailers_builder.h"
#include "trailers.h"

class RequestTrailers : public Trailers {
public:
  RequestTrailersBuilder to_request_trailers_builder() const;

private:
  RequestTrailers(RawHeaders headers) : Trailers(std::move(headers)) {}

  friend class RequestTrailersBuilder;
};

using RequestTrailersSharedPtr = std::shared_ptr<RequestTrailers>;
