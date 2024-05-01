#pragma once

#include "library/cc/headers_builder.h"
#include "library/cc/request_trailers.h"

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
