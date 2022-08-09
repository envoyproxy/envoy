#pragma once

#include <memory>

#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.h"
#include "envoy/extensions/filters/http/custom_response/v3/custom_response.pb.validate.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/config/datasource.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/router/header_parser.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CustomResponse {

class Response {
public:
  Response(const envoy::extensions::filters::http::custom_response::v3::CustomResponse::Response&
               response,
           Server::Configuration::CommonFactoryContext& context);

  // Rewrite the response.
  void rewrite(Http::ResponseHeaderMap& response_headers, StreamInfo::StreamInfo& stream_info,
               std::string& body, Http::Code& code);

  const std::string& name() const { return name_; }

  const absl::optional<envoy::extensions::filters::http::custom_response::v3::CustomResponse::
                           Response::RemoteDataSource>&
  remoteDataSource() const {
    return remote_data_source_;
  }

  bool isRemote() const { return remote_data_source_.has_value(); }

  const absl::optional<Http::Code>& statusCode() const { return status_code_; }

private:
  const std::string name_;

  // Body read from local data source.
  // Note that one of local_body_ or remote_data_source_ needs to be populated.
  absl::optional<std::string> local_body_;

  // Remote source to retrieve the data from in an async manner.
  absl::optional<envoy::extensions::filters::http::custom_response::v3::CustomResponse::Response::
                     RemoteDataSource>
      remote_data_source_;

  // body format
  const Formatter::FormatterPtr formatter_;

  const absl::optional<std::string> content_type_;

  absl::optional<Http::Code> status_code_;
  std::unique_ptr<Envoy::Router::HeaderParser> header_parser_;
};

using ResponseSharedPtr = std::shared_ptr<Response>;

} // namespace CustomResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
