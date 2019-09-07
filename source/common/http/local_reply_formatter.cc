#include "common/http/local_reply_formatter.h"

#include "envoy/http/local_reply.h"
#include "envoy/stream_info/stream_info.h"
#include "common/http/headers.h"
#include "common/access_log/access_log_formatter.h"

namespace Envoy{
namespace Http{
JsonFormatterImpl::JsonFormatterImpl(std::unordered_map<std::string, std::string> formatter){
    formatter_ = std::make_unique<AccessLog::JsonFormatterImpl>(formatter);
}

const std::string JsonFormatterImpl::format(const Http::HeaderMap* request_headers,
                                      const Http::HeaderMap* response_headers,
                                      const Http::HeaderMap* response_trailers,
                                      const absl::string_view& body,
                                      const StreamInfo::StreamInfo& stream_info) const {
  return formatter_->format(*request_headers, *response_headers, *response_trailers, stream_info, body);
}

void JsonFormatterImpl::insertContentHeaders(const absl::string_view& body, 
                                      Http::HeaderMap* headers) const {
    headers->insertContentLength().value(body.size());
    headers->insertContentType().value(Headers::get().ContentTypeValues.Json);
}

}
}

