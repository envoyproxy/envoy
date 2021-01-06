#pragma once

#include "headers_builder.h"
#include "request_trailers.h"

namespace Envoy {
namespace Platform {

class RequestTrailers;

class RequestTrailersBuilder : public HeadersBuilder {
public:
  RequestTrailersBuilder() : HeadersBuilder() {}

  RequestTrailers build() const;
};

using RequestTrailersBuilderSharedPtr = std::shared_ptr<RequestTrailersBuilder>;

} // namespace Platform
} // namespace Envoy
