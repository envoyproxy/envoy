#pragma once

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/filter_config.h"

#include "common/stream_info/stream_info_impl.h"

namespace Envoy {
namespace LocalReply {

class LocalReply {
public:
  virtual ~LocalReply() = default;

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
                       StreamInfo::StreamInfoImpl& stream_info, Http::Code& code, std::string& body,
                       absl::string_view& content_type) const PURE;
};

using LocalReplyPtr = std::unique_ptr<LocalReply>;

/**
 * Access log filter factory that reads from proto.
 */
class Factory {
public:
  /**
   * Create a LocalReply object from ProtoConfig
   */
  static LocalReplyPtr
  create(const envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig&
             config,
         Server::Configuration::FactoryContext& context);

  /**
   * Create a default LocalReply object with empty config.
   * It is used at places without Server::Configuration::FactoryContext.
   */
  static LocalReplyPtr createDefault();
};

} // namespace LocalReply
} // namespace Envoy
