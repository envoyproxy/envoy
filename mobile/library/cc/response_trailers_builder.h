#pragma once

#include "library/cc/headers_builder.h"
#include "library/cc/response_trailers.h"

namespace Envoy {
namespace Platform {

class ResponseTrailers;
using ResponseTrailersSharedPtr = std::shared_ptr<ResponseTrailers>;

class ResponseTrailersBuilder : public HeadersBuilder {
public:
  ResponseTrailersBuilder() {}

  ResponseTrailersSharedPtr build() const;
};

using ResponseTrailersBuilderSharedPtr = std::shared_ptr<ResponseTrailersBuilder>;

} // namespace Platform
} // namespace Envoy
