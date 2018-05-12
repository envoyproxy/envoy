#pragma once

#include <memory>

#include "envoy/config/rbac/v2alpha/rbac.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

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
   * Returns whether or not the connection matches the rules of the matcher. This overload of
   * `matches` should only be used for non-HTTP network calls (e.g., a binary TCP protocol).
   *
   * @param connection the downstream connection used to match against.
   */
  virtual bool matches(const Network::Connection& connection) const PURE;

  /**
   * Returns whether or not the HTTP request matches the rules of the matcher. This overload of
   * `matches` should only be used for HTTP requests.
   *
   * @param connection the downstream connection used to match against.
   * @param headers    the request headers used to match against.
   */
  virtual bool matches(const Network::Connection& connection,
                       const Envoy::Http::HeaderMap& headers) const PURE;

  /**
   * Creates a shared instance of a matcher based off the rules defined in the Permission config
   * proto message.
   */
  static MatcherConstSharedPtr create(const envoy::config::rbac::v2alpha::Permission&);

  /**
   * Creates a shared instance of a matcher based off the rules defined in the Principal config
   * proto message.
   */
  static MatcherConstSharedPtr create(const envoy::config::rbac::v2alpha::Principal&);
};

/**
 * Always matches, returning true for any input.
 */
class AlwaysMatcher : public Matcher {
public:
  virtual ~AlwaysMatcher() {}

  bool matches(const Network::Connection&) const override { return true; }
  bool matches(const Network::Connection&, const Envoy::Http::HeaderMap&) const override {
    return true;
  }
};

/**
 * A composite matcher where all sub-matchers must match for this to return true. Evaluation
 * short-circuits on the first non-match.
 */
class AndMatcher : public Matcher {
public:
  AndMatcher(const envoy::config::rbac::v2alpha::Permission_Set&);
  AndMatcher(const envoy::config::rbac::v2alpha::Principal_Set&);
  ~AndMatcher() {}

  bool matches(const Network::Connection&) const override;
  bool matches(const Network::Connection&, const Envoy::Http::HeaderMap&) const override;

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
  OrMatcher(const Protobuf::RepeatedPtrField<::envoy::config::rbac::v2alpha::Permission>&);
  OrMatcher(const Protobuf::RepeatedPtrField<::envoy::config::rbac::v2alpha::Principal>&);
  ~OrMatcher() {}

  bool matches(const Network::Connection&) const override;

  bool matches(const Network::Connection&, const Envoy::Http::HeaderMap&) const override;

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
  ~HeaderMatcher() {}

  bool matches(const Network::Connection&) const override { return false; }
  bool matches(const Network::Connection&, const Envoy::Http::HeaderMap&) const override;

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
  ~IPMatcher() {}

  bool matches(const Network::Connection&) const override;
  bool matches(const Network::Connection& connection,
               const Envoy::Http::HeaderMap&) const override {
    return matches(connection);
  };

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
  ~PortMatcher() {}

  bool matches(const Network::Connection&) const override;
  bool matches(const Network::Connection& connection,
               const Envoy::Http::HeaderMap&) const override {
    return matches(connection);
  };

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
  ~AuthenticatedMatcher() {}

  bool matches(const Network::Connection&) const override;
  bool matches(const Network::Connection& connection,
               const Envoy::Http::HeaderMap&) const override {
    return matches(connection);
  };

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
  ~PolicyMatcher(){};

  bool matches(const Network::Connection&) const override;
  bool matches(const Network::Connection&, const Envoy::Http::HeaderMap&) const override;

private:
  const OrMatcher permissions_;
  const OrMatcher principals_;
};

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
