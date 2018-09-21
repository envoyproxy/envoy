#pragma once

#include "envoy/http/header_map.h"

#include "extensions/filters/http/jwt_authn/verifier.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class Matcher;
typedef std::shared_ptr<const Matcher> MatcherConstSharedPtr;

/**
 * Supports matching a HTTP requests with JWT requirements.
 */
class Matcher {
public:
  virtual ~Matcher() {}

  /**
   * Returns if a HTTP request matches with the rules of the matcher.
   *
   * @param headers    the request headers used to match against. An empty map should be used if
   *                   there are none headers available.
   * @return  true if request is a match, false otherwise.
   */
  virtual bool matches(const Http::HeaderMap& headers) const PURE;

  /**
   * Returns the configured verifier for this route.
   *
   * @return reference to verifier pointer.
   */
  virtual const VerifierPtr& verifier() const PURE;

  /**
   * Factory method to create a shared instance of a matcher based on the rule defined.
   *
   * @param rule  the proto rule match message.
   * @param providers  the provider name to config map
   * @param factory  the Authenticator factory
   * @return the matcher instance.
   */
  static MatcherConstSharedPtr
  create(const ::envoy::config::filter::http::jwt_authn::v2alpha::RequirementRule& rule,
         const Protobuf::Map<ProtobufTypes::String,
                             ::envoy::config::filter::http::jwt_authn::v2alpha::JwtProvider>&
             providers,
         const AuthFactory& factory, const Extractor& extractor);
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
