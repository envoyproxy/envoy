#include "common/local_reply/local_reply.h"

#include <string>
#include <vector>

#include "common/access_log/access_log_formatter.h"

namespace Envoy {
namespace LocalReply {

ResponseRewriter::ResponseRewriter(absl::optional<uint32_t> response_code)
    : response_code_(response_code) {}

void ResponseRewriter::rewrite(Http::Code& code) {
  if (response_code_.has_value()) {
    code = static_cast<Http::Code>(response_code_.value());
  }
}

ResponseMapper::ResponseMapper(AccessLog::FilterPtr&& filter, ResponseRewriterPtr&& rewriter)
    : filter_(std::move(filter)), rewriter_(std::move(rewriter)) {}

bool ResponseMapper::match(const Http::HeaderMap* request_headers,
                           const Http::HeaderMap* response_headers,
                           const Http::HeaderMap* response_trailers,
                           const StreamInfo::StreamInfo& stream_info) {
  return filter_->evaluate(stream_info, *request_headers, *response_headers, *response_trailers);
}

void ResponseMapper::rewrite(Http::Code& status_code) { rewriter_->rewrite(status_code); }

LocalReply::LocalReply(std::list<ResponseMapperPtr> mappers, AccessLog::FormatterPtr&& formatter,
                       std::string content_type)
    : mappers_(std::move(mappers)), formatter_(std::move(formatter)), content_type_(content_type) {}

void LocalReply::matchAndRewrite(const Http::HeaderMap* request_headers,
                                 const Http::HeaderMap* response_headers,
                                 const Http::HeaderMap* response_trailers,
                                 const StreamInfo::StreamInfo& stream_info, Http::Code& code) {

  for (auto& mapper : mappers_) {
    if (mapper->match(request_headers, response_headers, response_trailers, stream_info)) {
      mapper->rewrite(code);
      break;
    }
  }
}

std::string LocalReply::format(const Http::HeaderMap* request_headers,
                               const Http::HeaderMap* response_headers,
                               const Http::HeaderMap* response_trailers,
                               const StreamInfo::StreamInfo& stream_info,
                               const absl::string_view& body) {
  return formatter_->format(*request_headers, *response_headers, *response_trailers, stream_info,
                            body);
}

void LocalReply::insertContentHeaders(const absl::string_view& body, Http::HeaderMap* headers) {
  if (!body.empty()) {
    headers->insertContentLength().value(body.size());
    headers->insertContentType().value(content_type_);
  }
}

} // namespace LocalReply
} // namespace Envoy
