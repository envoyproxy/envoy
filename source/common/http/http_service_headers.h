#pragma once

#include "envoy/config/core/v3/http_service.pb.h"
#include "envoy/formatter/substitution_formatter_base.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"

#include "source/common/http/headers.h"
#include "source/common/stream_info/stream_info_impl.h"

namespace Envoy {
namespace Http {

/**
 * Parses and applies request_headers_to_add from an HTTP service configuration.
 *
 * Separates headers into static (plain string value) and formatted (substitution
 * formatter) groups. Static headers are evaluated once at construction time.
 * Formatted headers are re-evaluated on each apply() call, so that
 * runtime updates such as SDS secret rotation are reflected in outgoing requests.
 */
class HttpServiceHeadersApplicator {
public:
  HttpServiceHeadersApplicator(const envoy::config::core::v3::HttpService& http_service,
                               Server::Configuration::ServerFactoryContext& server_context,
                               absl::Status& creation_status);

  static std::unique_ptr<HttpServiceHeadersApplicator>
  createOrThrow(const envoy::config::core::v3::HttpService& http_service,
                Server::Configuration::ServerFactoryContext& server_context);

  /**
   * Apply all parsed headers to the outgoing request message.
   */
  void apply(RequestHeaderMap& headers) const;

private:
  std::vector<std::pair<const LowerCaseString, std::string>> static_headers_;
  std::vector<std::pair<const LowerCaseString, Formatter::FormatterPtr>> formatted_headers_;

  // A `StreamInfo` is required, but in this context we don't have one, so create an empty one.
  // This allows formatters that don't require any stream info to succeed, such as extensions that
  // load data externally for API keys and similar.
  const StreamInfo::StreamInfoImpl stream_info_;
};

} // namespace Http
} // namespace Envoy
