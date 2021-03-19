#pragma once

#include "envoy/http/codec.h"
#include "envoy/router/router.h"

#include "extensions/upstreams/http/http/upstream_request.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Metadata {

class MetadataUpstream : public Upstreams::Http::Http::HttpUpstream {
public:
  MetadataUpstream(Router::UpstreamToDownstream& upstream_request,
                   Envoy::Http::RequestEncoder* encoder)
      : HttpUpstream(upstream_request, encoder) {}
};

} // namespace Metadata
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
