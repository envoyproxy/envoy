#pragma once

// NOLINT(namespace-envoy)

#include "headers_builder.h"
#include "response_trailers.h"

class ResponseTrailers;

class ResponseTrailersBuilder : public HeadersBuilder {
public:
  ResponseTrailersBuilder() {}

  ResponseTrailers build() const;
};

using ResponseTrailersBuilderSharedPtr = std::shared_ptr<ResponseTrailersBuilder>;
