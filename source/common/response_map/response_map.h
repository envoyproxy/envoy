#pragma once

#include "envoy/extensions/filters/http/response_map/v3/response_map.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/filter_config.h"

#include "common/stream_info/stream_info_impl.h"

namespace Envoy {
namespace ResponseMap {

class ResponseMap {
public:
  virtual ~ResponseMap() = default;

  /**
   * rewrite the response status code, body and content_type.
   * @param request_headers supplies the information about request headers required by filters.
   * @param stream_info supplies the information about streams required by filters.
   * @param code status code.
   * @param body response body.
   * @param content_type response content_type.
   */
  virtual void rewrite(const Http::RequestHeaderMap* request_headers,
                       Http::ResponseHeaderMap& response_headers,
                       StreamInfo::StreamInfo& stream_info, std::string& body,
                       absl::string_view& content_type) const PURE;

  virtual bool match(const Http::RequestHeaderMap* request_headers,
                     const Http::ResponseHeaderMap& response_headers,
                     StreamInfo::StreamInfo& stream_info) const PURE;
};

using ResponseMapPtr = std::unique_ptr<ResponseMap>;

/**
 * Access log filter factory that reads from proto.
 */
class Factory {
public:
  /**
   * Create a ResponseMap object from ProtoConfig
   */
  static ResponseMapPtr
  create(const envoy::extensions::filters::http::response_map::v3::ResponseMap& config,
         Server::Configuration::CommonFactoryContext& context,
         ProtobufMessage::ValidationVisitor& validationVisitor);

  /**
   * Create a default ResponseMap object with empty config.
   * It is used at places without Server::Configuration::CommonFactoryContext.
   */
  static ResponseMapPtr createDefault();
};

} // namespace ResponseMap
} // namespace Envoy
