#pragma once

#include <string>

#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"
#include "envoy/service/auth/v2alpha/attribute_context.pb.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Auth {

class AttributeContextUtils {
public:
  static void setSourcePeer(envoy::service::auth::v2alpha::AttributeContext& context,
                            const Network::Connection& connection, const std::string& service);
  static void setDestinationPeer(envoy::service::auth::v2alpha::AttributeContext& context,
                                 const Network::Connection& connection);
  static void setHttpRequest(envoy::service::auth::v2alpha::AttributeContext& context,
                             const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
                             const Envoy::Http::HeaderMap& headers);

private:
  static void setPeer(envoy::service::auth::v2alpha::AttributeContext_Peer& peer,
                      const Network::Connection& connection, const std::string& service,
                      const bool local);

  static std::string getHeaderStr(const Envoy::Http::HeaderEntry* entry);
};

} // namespace Auth
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
