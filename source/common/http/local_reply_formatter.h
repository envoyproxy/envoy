#pragma once

#include <unordered_map>

#include "envoy/http/local_reply.h"
#include "common/access_log/access_log_formatter.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy{
namespace Http{


class JsonFormatterImpl : public Formatter {
public:
  JsonFormatterImpl(std::unordered_map<std::string, std::string> formatter);

  // Formatter::format
const std::string format(const Http::HeaderMap* request_headers,
                     const Http::HeaderMap* response_headers,
                     const Http::HeaderMap* response_trailers,
                     const absl::string_view& body,
                     const StreamInfo::StreamInfo& stream_info) const override;

void insertContentHeaders(const absl::string_view& body,
                     Http::HeaderMap* headers) const override;

private:
  AccessLog::FormatterPtr formatter_;
};
}
}

