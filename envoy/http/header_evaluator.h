#pragma once

#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Http {
class HeaderEvaluator {
public:
  virtual ~HeaderEvaluator() = default;
  virtual void evaluateHeaders(Http::HeaderMap& headers,
                               const StreamInfo::StreamInfo* stream_info) const PURE;
};
} // namespace Http
} // namespace Envoy
