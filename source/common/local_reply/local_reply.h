#pragma once

#include <string>
#include <vector>

#include "envoy/http/codes.h"

#include "common/access_log/access_log_formatter.h"

namespace Envoy {
namespace LocalReply {

/**
 * Configuration of response rewriter which contains status code to which matched local response
 * should be mapped.
 */
class ResponseRewriter {
public:
  ResponseRewriter(absl::optional<uint32_t> response_code);

  /**
   * Change given status code to one defined during instance creation.
   * @param status_code supplies reference to status code.
   */
  void rewrite(Http::Code& code);

private:
  absl::optional<uint32_t> response_code_;
};

using ResponseRewriterPtr = std::unique_ptr<ResponseRewriter>;

/**
 * Configuration of response mapper which contains pair of filter and definition of rewriter.
 */
class ResponseMapper {
public:
  ResponseMapper(AccessLog::FilterPtr&& filter, ResponseRewriterPtr&& rewriter);

  /**
   * Check if given request matches defined filter.
   * @param request_headers supplies the information about request headers required by filters
   * @param stream_info supplies the information about streams required by filters.
   * @return true if matches filter.
   */
  bool match(const Http::HeaderMap* request_headers, const Http::HeaderMap* response_headers,
             const Http::HeaderMap* response_trailers, const StreamInfo::StreamInfo& stream_info);

  /**
   * Call rewriter method from ResponseRewriter.
   * @param status_code supplies status code.
   */
  void rewrite(Http::Code& status_code);

private:
  AccessLog::FilterPtr filter_;
  ResponseRewriterPtr rewriter_;
};

using ResponseMapperPtr = std::unique_ptr<ResponseMapper>;

/**
 * Configuration of local reply which contains list of ResponseMapper and formatter with proper
 * content type.
 */
class LocalReply {
public:
  LocalReply(std::list<ResponseMapperPtr> mappers, AccessLog::FormatterPtr&& formatter,
             std::string content_type);

  /**
   * Run through defined in configuration filters and for first matched filter rewrite local
   * response.
   * @param request_headers supplies the information about request headers required by filters.
   * @param response_headers supplies response headers.
   * @param response_trailers supplies response trailers.
   * @param stream_info supplies the information about streams required by filters.
   * @param code local reply status code.
   */
  void matchAndRewrite(const Http::HeaderMap& request_headers,
                       const Http::HeaderMap& response_headers,
                       const Http::HeaderMap& response_trailers,
                       const StreamInfo::StreamInfo& stream_info, Http::Code& code);

  /**
   * Run AccessLogFormatter format method which reformat data to structure defined in configuration.
   * @param request_headers supplies the information about request headers required by filters.
   * @param request_headers supplies the incoming request headers after filtering.
   * @param response_headers supplies response headers.
   * @param response_trailers supplies response trailers.
   * @param stream_info supplies the information about streams required by filters.
   * @param body original response body.
   * @return std::string formatted response body.
   */
  std::string format(const Http::HeaderMap& request_headers,
                     const Http::HeaderMap& response_headers,
                     const Http::HeaderMap& response_trailers,
                     const StreamInfo::StreamInfo& stream_info, const absl::string_view& body);

  /**
   * Insert proper content-length and content-type headers for configured format.
   * @param body supplies the body of response.
   * @param headers supplies the pointer to header to which new one will be added.
   */
  void insertContentHeaders(const absl::string_view& body, Http::HeaderMap* headers);

private:
  std::list<ResponseMapperPtr> mappers_;
  AccessLog::FormatterPtr formatter_;
  std::string content_type_;
};

using LocalReplyPtr = std::unique_ptr<LocalReply>;

} // namespace LocalReply
} // namespace Envoy
