#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/grpc/async_client.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/protocol.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/service/auth/v2alpha/external_auth.pb.h"
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
   *
   */
  static void
  createHttpCheck(const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
                  const Envoy::Http::HeaderMap& headers,
                  Protobuf::Map<ProtobufTypes::String, ProtobufTypes::String>&& context_extensions,
                  envoy::service::auth::v2alpha::CheckRequest& request);

  /**
   * createTcpCheck is used to extract the attributes from the network layer and fill them up
   * in the CheckRequest proto message.
   * @param callbacks supplies the network layer context from which data can be extracted.
   * @param request is the reference to the check request that will be filled up.
   *
   */
  static void createTcpCheck(const Network::ReadFilterCallbacks* callbacks,
                             envoy::service::auth::v2alpha::CheckRequest& request);

private:
  static void setAttrContextPeer(envoy::service::auth::v2alpha::AttributeContext_Peer& peer,
                                 const Network::Connection& connection, const std::string& service,
                                 const bool local);
  static void setHttpRequest(::envoy::service::auth::v2alpha::AttributeContext_HttpRequest& httpreq,
                             const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
                             const Envoy::Http::HeaderMap& headers);
  static void setAttrContextRequest(::envoy::service::auth::v2alpha::AttributeContext_Request& req,
                                    const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
                                    const Envoy::Http::HeaderMap& headers);
  static std::string getHeaderStr(const Envoy::Http::HeaderEntry* entry);
  static Envoy::Http::HeaderMap::Iterate fillHttpHeaders(const Envoy::Http::HeaderEntry&, void*);
};

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
