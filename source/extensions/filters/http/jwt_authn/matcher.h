#pragma once

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class Matcher;
using MatcherConstPtr = std::unique_ptr<const Matcher>;

/**
 * Supports matching a HTTP requests with JWT requirements.
 */
class Matcher {
public:
  virtual ~Matcher() = default;

  /**
   * Returns if a HTTP request matches with the rules of the matcher.
   *
   * @param headers    the request headers used to match against. An empty map should be used if
   *                   there are none headers available.
   * @return  true if request is a match, false otherwise.
   */
  virtual bool matches(const Http::RequestHeaderMap& headers) const PURE;

  /**
   * Factory method to create a shared instance of a matcher based on the rule defined.
   *
   * @param rule  the proto rule match message.
   * @return the matcher instance.
   */
  static MatcherConstPtr
  create(const envoy::extensions::filters::http::jwt_authn::v3::RequirementRule& rule,
         Server::Configuration::CommonFactoryContext& context);
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
