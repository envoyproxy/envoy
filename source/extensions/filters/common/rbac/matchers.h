#pragma once

#include <memory>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/config/rbac/v2alpha/rbac.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

#include "common/common/matchers.h"
#include "common/http/header_utility.h"
#include "common/network/cidr_range.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

class Matcher;
typedef std::shared_ptr<const Matcher> MatcherConstSharedPtr;

/**
 *  Matchers describe the rules for matching either a permission action or principal.
 */
class Matcher {
public:
  virtual ~Matcher() {}

  /**
   * Returns whether or not the permission/principal matches the rules of the matcher.
   *
   * @param connection the downstream connection used to match against.
   * @param headers    the request headers used to match against. An empty map should be used if
   *                   there are none headers available.
   * @param metadata   the additional information about the action/principal.
   */
  virtual bool matches(const Network::Connection& connection, const Envoy::Http::HeaderMap& headers,
                       const envoy::api::v2::core::Metadata& metadata) const PURE;

  /**
   * Creates a shared instance of a matcher based off the rules defined in the Permission config
   * proto message.
   */
  static MatcherConstSharedPtr create(const envoy::config::rbac::v2alpha::Permission& permission);

  /**
   * Creates a shared instance of a matcher based off the rules defined in the Principal config
   * proto message.
   */
  static MatcherConstSharedPtr create(const envoy::config::rbac::v2alpha::Principal& principal);
};

/**
 * Always matches, returning true for any input.
 */
class AlwaysMatcher : public Matcher {
public:
  bool matches(const Network::Connection&, const Envoy::Http::HeaderMap&,
               const envoy::api::v2::core::Metadata&) const override {
    return true;
  }
};

/**
 * A composite matcher where all sub-matchers must match for this to return true. Evaluation
 * short-circuits on the first non-match.
 */
class AndMatcher : public Matcher {
public:
  AndMatcher(const envoy::config::rbac::v2alpha::Permission_Set& rules);
  AndMatcher(const envoy::config::rbac::v2alpha::Principal_Set& ids);

  bool matches(const Network::Connection& connection, const Envoy::Http::HeaderMap& headers,
               const envoy::api::v2::core::Metadata&) const override;

private:
  std::vector<MatcherConstSharedPtr> matchers_;
};

/**
 * A composite matcher where only one sub-matcher must match for this to return true. Evaluation
 * short-circuits on the first match.
 */
class OrMatcher : public Matcher {
public:
  OrMatcher(const envoy::config::rbac::v2alpha::Permission_Set& set) : OrMatcher(set.rules()) {}
  OrMatcher(const envoy::config::rbac::v2alpha::Principal_Set& set) : OrMatcher(set.ids()) {}
  OrMatcher(const Protobuf::RepeatedPtrField<::envoy::config::rbac::v2alpha::Permission>& rules);
  OrMatcher(const Protobuf::RepeatedPtrField<::envoy::config::rbac::v2alpha::Principal>& ids);

  bool matches(const Network::Connection& connection, const Envoy::Http::HeaderMap& headers,
               const envoy::api::v2::core::Metadata&) const override;

private:
  std::vector<MatcherConstSharedPtr> matchers_;
};

/**
 * Perform a match against any HTTP header (or pseudo-header, such as `:path` or `:authority`). Will
 * always fail to match on any non-HTTP connection.
 */
class HeaderMatcher : public Matcher {
public:
  HeaderMatcher(const envoy::api::v2::route::HeaderMatcher& matcher) : header_(matcher) {}

  bool matches(const Network::Connection& connection, const Envoy::Http::HeaderMap& headers,
               const envoy::api::v2::core::Metadata&) const override;

private:
  const Envoy::Http::HeaderUtility::HeaderData header_;
};

/**
 * Perform a match against an IP CIDR range. This rule can be applied to either the source
 * (remote) or the destination (local) IP.
 */
class IPMatcher : public Matcher {
public:
  IPMatcher(const envoy::api::v2::core::CidrRange& range, bool destination)
      : range_(Network::Address::CidrRange::create(range)), destination_(destination) {}

  bool matches(const Network::Connection& connection, const Envoy::Http::HeaderMap& headers,
               const envoy::api::v2::core::Metadata&) const override;

private:
  const Network::Address::CidrRange range_;
  const bool destination_;
};

/**
 * Matches the port number of the destination (local) address.
 */
class PortMatcher : public Matcher {
public:
  PortMatcher(const uint32_t port) : port_(port) {}

  bool matches(const Network::Connection& connection, const Envoy::Http::HeaderMap& headers,
               const envoy::api::v2::core::Metadata&) const override;

private:
  const uint32_t port_;
};

/**
 * Matches the principal name as described in the peer certificate. Uses the URI SAN first. If that
 * field is not present, uses the subject instead.
 */
class AuthenticatedMatcher : public Matcher {
public:
  AuthenticatedMatcher(const envoy::config::rbac::v2alpha::Principal_Authenticated& auth)
      : name_(auth.name()) {}

  bool matches(const Network::Connection& connection, const Envoy::Http::HeaderMap& headers,
               const envoy::api::v2::core::Metadata&) const override;

private:
  const std::string name_;
};

/**
 * Matches a Policy which is a collection of permission and principal matchers. If any action
 * matches a permission, the principals are then checked for a match.
 */
class PolicyMatcher : public Matcher {
public:
  PolicyMatcher(const envoy::config::rbac::v2alpha::Policy& policy)
      : permissions_(policy.permissions()), principals_(policy.principals()) {}

  bool matches(const Network::Connection& connection, const Envoy::Http::HeaderMap& headers,
               const envoy::api::v2::core::Metadata&) const override;

private:
  const OrMatcher permissions_;
  const OrMatcher principals_;
};

class MetadataMatcher : public Matcher {
public:
  MetadataMatcher(const Envoy::Matchers::MetadataMatcher& matcher) : matcher_(matcher) {}

  bool matches(const Network::Connection& connection, const Envoy::Http::HeaderMap& headers,
               const envoy::api::v2::core::Metadata& metadata) const override;

private:
  const Envoy::Matchers::MetadataMatcher matcher_;
};

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
