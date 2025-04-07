#pragma once

#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

class Matcher;
using MatcherConstSharedPtr = std::shared_ptr<const Matcher>;

/**
 *  Matchers describe the rules for matching either a permission action or principal.
 */
class Matcher {
public:
  virtual ~Matcher() = default;

  /**
   * Returns whether or not the permission/principal matches the rules of the matcher.
   *
   * @param connection the downstream connection used to match against.
   * @param headers    the request headers used to match against. An empty map should be used if
   *                   there are none headers available.
   * @param info       the additional information about the action/principal.
   */
  virtual bool matches(const Network::Connection& connection,
                       const Envoy::Http::RequestHeaderMap& headers,
                       const StreamInfo::StreamInfo& info) const PURE;

  /**
   * Creates a shared instance of a matcher based off the rules defined in the Permission config
   * proto message.
   */
  static MatcherConstSharedPtr create(const envoy::config::rbac::v3::Permission& permission,
                                      ProtobufMessage::ValidationVisitor& validation_visitor,
                                      Server::Configuration::CommonFactoryContext& context);

  /**
   * Creates a shared instance of a matcher based off the rules defined in the Principal config
   * proto message.
   */
  static MatcherConstSharedPtr create(const envoy::config::rbac::v3::Principal& principal,
                                      Server::Configuration::CommonFactoryContext& context);
};

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
