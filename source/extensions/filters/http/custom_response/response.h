#pragma once

#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/config/datasource.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/router/header_parser.h"

#include "absl/strings/string_view.h"
#include "absl/container/flat_hash_map.h"

#include <memory>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

/**
 * Async callbacks used during fetchBody() calls.
 * TODO: what is this beast?
 */
class RequestCallbacks {
public:
  virtual ~RequestCallbacks() = default;

  /**
   * Called on completion of request.
   *
   * @param response the pointer to the response message. Null response pointer means the request
   *        was completed with error.
   */
  virtual void onComplete(const Http::ResponseMessage* response_ptr) PURE;
};

class Response {
public:
  Response(const envoy::extensions::filters::http::custom_response::v3::CustomResponse::Response&
               response,
           Server::Configuration::CommonFactoryContext& context);

  // Rewrite the response.
  void rewrite(const Http::RequestHeaderMap& request_headers,
               Http::ResponseHeaderMap& response_headers, StreamInfo::StreamInfo& stream_info,
               Http::Code& code, std::string& body);

  const std::string& name() { return name_; }

private:
  const std::string name_;
  // Body read from local data source.
  // Note that one of local_body_ or remote_data_source_ needs to be populated.
  std::optional<std::string> local_body_;
  // Remote source to retrieve the data from in an async mannder.
  std::optional<envoy::extensions::filters::http::custom_response::v3::CustomResponse::Response::
                    RemoteDataSource>
      remote_data_source_;

  // body format
  const Formatter::FormatterPtr formatter_;
  const std::optional<std::string> content_type_;

  absl::optional<Http::Code> status_code_;
  std::unique_ptr<Envoy::Router::HeaderParser> header_parser_;
};

using ResponseSharedPtr = std::shared_ptr<Response>;

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
