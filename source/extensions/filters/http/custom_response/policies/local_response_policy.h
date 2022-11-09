#pragma once

#include "envoy/extensions/filters/http/custom_response/v3/policies.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/policies.pb.validate.h"

#include "source/common/router/header_parser.h"
#include "source/extensions/filters/http/custom_response/policy.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

class CustomResponseFilter;

class LocalResponsePolicy : public Policy, public Logger::Loggable<Logger::Id::filter> {

public:
  LocalResponsePolicy(
      const envoy::extensions::filters::http::custom_response::v3::LocalResponsePolicy& config,
      Server::Configuration::CommonFactoryContext& context);

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool,
                                          CustomResponseFilter&) const override;

  Http::Code getStatusCodeForLocalReply(const Http::ResponseHeaderMap& response_headers) const;

private:
  // Rewrite the response body for locally specified bodies.
  void formatBody(const Http::RequestHeaderMap& request_headers,
                  const Http::ResponseHeaderMap& response_headers,
                  const StreamInfo::StreamInfo& stream_info, std::string& body) const;

  // Body read from local data source.
  const absl::optional<std::string> local_body_;

  // body format
  const Formatter::FormatterPtr formatter_;

  const absl::optional<Http::Code> status_code_;
  const std::unique_ptr<Envoy::Router::HeaderParser> header_parser_;
};
} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
