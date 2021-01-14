#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/protocol.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/service/auth/v3/attribute_context.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/tracing/http_tracer.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/async_client_impl.h"
#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

/**
 * For creating ext_authz.proto (authorization) request.
 * CheckRequestUtils is used to extract attributes from the TCP/HTTP request
 * and fill out the details in the authorization protobuf that is sent to authorization
 * service.
 * The specific information in the request is as per the specification in the
 * data plane API.
 */
class CheckRequestUtils {
public:
  /**
   * createHttpCheck is used to extract the attributes from the stream and the http headers
   * and fill them up in the CheckRequest proto message.
   * @param callbacks supplies the Http stream context from which data can be extracted.
   * @param headers supplies the header map with http headers that will be used to create the
   *        check request.
   * @param request is the reference to the check request that will be filled up.
   * @param with_request_body when true, will add the request body to the check request.
   * @param pack_as_bytes when true, will set the check request body as bytes.
   * @param include_peer_certificate whether to include the peer certificate in the check request.
   */
  static void createHttpCheck(const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
                              const Envoy::Http::RequestHeaderMap& headers,
                              Protobuf::Map<std::string, std::string>&& context_extensions,
                              envoy::config::core::v3::Metadata&& metadata_context,
                              envoy::service::auth::v3::CheckRequest& request,
                              uint64_t max_request_bytes, bool pack_as_bytes,
                              bool include_peer_certificate);

  /**
   * createTcpCheck is used to extract the attributes from the network layer and fill them up
   * in the CheckRequest proto message.
   * @param callbacks supplies the network layer context from which data can be extracted.
   * @param request is the reference to the check request that will be filled up.
   * @param include_peer_certificate whether to include the peer certificate in the check request.
   */
  static void createTcpCheck(const Network::ReadFilterCallbacks* callbacks,
                             envoy::service::auth::v3::CheckRequest& request,
                             bool include_peer_certificate);

private:
  static void setAttrContextPeer(envoy::service::auth::v3::AttributeContext::Peer& peer,
                                 const Network::Connection& connection, const std::string& service,
                                 const bool local, bool include_certificate);
  static void setRequestTime(envoy::service::auth::v3::AttributeContext::Request& req,
                             const StreamInfo::StreamInfo& stream_info);
  static void setHttpRequest(envoy::service::auth::v3::AttributeContext::HttpRequest& httpreq,
                             const uint64_t stream_id, const StreamInfo::StreamInfo& stream_info,
                             const Buffer::Instance* decoding_buffer,
                             const Envoy::Http::RequestHeaderMap& headers,
                             uint64_t max_request_bytes, bool pack_as_bytes);
  static void setAttrContextRequest(envoy::service::auth::v3::AttributeContext::Request& req,
                                    const uint64_t stream_id,
                                    const StreamInfo::StreamInfo& stream_info,
                                    const Buffer::Instance* decoding_buffer,
                                    const Envoy::Http::RequestHeaderMap& headers,
                                    uint64_t max_request_bytes, bool pack_as_bytes);
  static std::string getHeaderStr(const Envoy::Http::HeaderEntry* entry);
  static Envoy::Http::HeaderMap::Iterate fillHttpHeaders(const Envoy::Http::HeaderEntry&, void*);
};

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
