#pragma once

// NOLINT(namespace-envoy)

#include "headers_builder.h"
#include "request_trailers.h"

class RequestTrailers;

class RequestTrailersBuilder : public HeadersBuilder {
public:
  RequestTrailersBuilder() : HeadersBuilder() {}

  RequestTrailers build() const;
};

using RequestTrailersBuilderSharedPtr = std::shared_ptr<RequestTrailersBuilder>;
