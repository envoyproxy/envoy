#pragma once

#include "envoy/extensions/http/custom_response/local_response_policy/v3/local_response_policy.pb.h"
#include "envoy/extensions/http/custom_response/local_response_policy/v3/local_response_policy.pb.validate.h"

#include "source/common/router/header_parser.h"
#include "source/extensions/filters/http/custom_response/policy.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {
class CustomResponseFilter;
} // namespace CustomResponse
} // namespace HttpFilters

namespace Http {
namespace CustomResponse {

class LocalResponsePolicy : public Extensions::HttpFilters::CustomResponse::Policy,
                            public Logger::Loggable<Logger::Id::filter> {

public:
  LocalResponsePolicy(const envoy::extensions::http::custom_response::local_response_policy::v3::
                          LocalResponsePolicy& config,
                      Server::Configuration::ServerFactoryContext& context);

  Envoy::Http::FilterHeadersStatus
  encodeHeaders(Envoy::Http::ResponseHeaderMap&, bool,
                Extensions::HttpFilters::CustomResponse::CustomResponseFilter&) const override;

  Envoy::Http::Code
  getStatusCodeForLocalReply(const Envoy::Http::ResponseHeaderMap& response_headers) const;

private:
  // Rewrite the response body for locally specified bodies.
  void formatBody(const Envoy::Http::RequestHeaderMap& request_headers,
                  const Envoy::Http::ResponseHeaderMap& response_headers,
                  const StreamInfo::StreamInfo& stream_info, std::string& body) const;

  // Body read from local data source.
  const absl::optional<std::string> local_body_;

  // body format
  Formatter::FormatterPtr formatter_;

  const absl::optional<Envoy::Http::Code> status_code_;
  const std::unique_ptr<Envoy::Router::HeaderParser> header_parser_;
};
} // namespace CustomResponse
} // namespace Http
} // namespace Extensions
} // namespace Envoy
