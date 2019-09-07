#pragma once

#include <string>
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Http {

class Formatter {
public:
  virtual ~Formatter() = default;

  virtual const std::string format(const Http::HeaderMap* request_headers,
                             const Http::HeaderMap* response_headers,
                             const Http::HeaderMap* response_trailers,
                             const absl::string_view& body,
                             const StreamInfo::StreamInfo& stream_info) const PURE;

  virtual void insertContentHeaders(const absl::string_view& body, 
                             Http::HeaderMap* headers) const PURE;
};

using FormatterPtr = std::unique_ptr<Http::Formatter>;

} // namespace Http
} // namespace Envoy
