#pragma once

#include <memory>

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"
#include "envoy/type/matcher/v3/path.pb.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/common/matchers.h"
#include "source/common/http/header_utility.h"
#include "source/common/network/cidr_range.h"
#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/path/match/uri_template/uri_template_match.h"

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

/**
 * Always matches, returning true for any input.
 */
class AlwaysMatcher : public Matcher {
public:
  bool matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
               const StreamInfo::StreamInfo&) const override {
    return true;
  }
};

/**
 * A composite matcher where all sub-matchers must match for this to return true. Evaluation
 * short-circuits on the first non-match.
 */
class AndMatcher : public Matcher {
public:
  AndMatcher(const envoy::config::rbac::v3::Permission::Set& rules,
             ProtobufMessage::ValidationVisitor& validation_visitor,
             Server::Configuration::CommonFactoryContext& context);
  AndMatcher(const envoy::config::rbac::v3::Principal::Set& ids,
             Server::Configuration::CommonFactoryContext& context);

  bool matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo&) const override;

private:
  std::vector<MatcherConstSharedPtr> matchers_;
};

/**
 * A composite matcher where only one sub-matcher must match for this to return true. Evaluation
 * short-circuits on the first match.
 */
class OrMatcher : public Matcher {
public:
  OrMatcher(const envoy::config::rbac::v3::Permission::Set& set,
            ProtobufMessage::ValidationVisitor& validation_visitor,
            Server::Configuration::CommonFactoryContext& context)
      : OrMatcher(set.rules(), validation_visitor, context) {}
  OrMatcher(const envoy::config::rbac::v3::Principal::Set& set,
            Server::Configuration::CommonFactoryContext& context)
      : OrMatcher(set.ids(), context) {}
  OrMatcher(const Protobuf::RepeatedPtrField<envoy::config::rbac::v3::Permission>& rules,
            ProtobufMessage::ValidationVisitor& validation_visitor,
            Server::Configuration::CommonFactoryContext& context);
  OrMatcher(const Protobuf::RepeatedPtrField<envoy::config::rbac::v3::Principal>& ids,
            Server::Configuration::CommonFactoryContext& context);

  bool matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo&) const override;

private:
  std::vector<MatcherConstSharedPtr> matchers_;
};

class NotMatcher : public Matcher {
public:
  NotMatcher(const envoy::config::rbac::v3::Permission& permission,
             ProtobufMessage::ValidationVisitor& validation_visitor,
             Server::Configuration::CommonFactoryContext& context)
      : matcher_(Matcher::create(permission, validation_visitor, context)) {}
  NotMatcher(const envoy::config::rbac::v3::Principal& principal,
             Server::Configuration::CommonFactoryContext& context)
      : matcher_(Matcher::create(principal, context)) {}

  bool matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo&) const override;

private:
  MatcherConstSharedPtr matcher_;
};

/**
 * Perform a match against any HTTP header (or pseudo-header, such as `:path` or `:authority`). Will
 * always fail to match on any non-HTTP connection.
 */
class HeaderMatcher : public Matcher {
public:
  HeaderMatcher(const envoy::config::route::v3::HeaderMatcher& matcher,
                Server::Configuration::CommonFactoryContext& context)
      : header_(matcher, context) {}

  bool matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo&) const override;

private:
  const Envoy::Http::HeaderUtility::HeaderData header_;
};

/**
 * Perform a match against an IP CIDR range. This rule can be applied to connection remote,
 * downstream local address, downstream direct remote address or downstream remote address.
 */
class IPMatcher : public Matcher {
public:
  enum Type { ConnectionRemote = 0, DownstreamLocal, DownstreamDirectRemote, DownstreamRemote };

  IPMatcher(const envoy::config::core::v3::CidrRange& range, Type type)
      : range_(THROW_OR_RETURN_VALUE(Network::Address::CidrRange::create(range),
                                     Network::Address::CidrRange)),
        type_(type) {}

  bool matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo& info) const override;

private:
  const Network::Address::CidrRange range_;
  const Type type_;
};

/**
 * Matches the port number of the destination (local) address.
 */
class PortMatcher : public Matcher {
public:
  PortMatcher(const uint32_t port) : port_(port) {}

  bool matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
               const StreamInfo::StreamInfo& info) const override;

private:
  const uint32_t port_;
};

class PortRangeMatcher : public Matcher {
public:
  PortRangeMatcher(const ::envoy::type::v3::Int32Range& range);

  bool matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
               const StreamInfo::StreamInfo& info) const override;

private:
  const uint32_t start_;
  const uint32_t end_;
};

/**
 * Matches the principal name as described in the peer certificate. Uses the URI SAN first. If that
 * field is not present, uses the subject instead.
 */
class AuthenticatedMatcher : public Matcher {
public:
  AuthenticatedMatcher(const envoy::config::rbac::v3::Principal::Authenticated& auth,
                       Server::Configuration::CommonFactoryContext& context)
      : matcher_(auth.has_principal_name()
                     ? absl::make_optional<
                           Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
                           auth.principal_name(), context)
                     : absl::nullopt) {}

  bool matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo&) const override;

private:
  const absl::optional<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>
      matcher_;
};

/**
 * Matches a Policy which is a collection of permission and principal matchers. If any action
 * matches a permission, the principals are then checked for a match.
 * The condition is a conjunction clause.
 */
class PolicyMatcher : public Matcher, NonCopyable {
public:
  PolicyMatcher(const envoy::config::rbac::v3::Policy& policy, Expr::Builder* builder,
                ProtobufMessage::ValidationVisitor& validation_visitor,
                Server::Configuration::CommonFactoryContext& context)
      : permissions_(policy.permissions(), validation_visitor, context),
        principals_(policy.principals(), context), condition_(policy.condition()) {
    if (policy.has_condition()) {
      expr_ = Expr::createExpression(*builder, condition_);
    }
  }

  bool matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo&) const override;

private:
  const OrMatcher permissions_;
  const OrMatcher principals_;
  const google::api::expr::v1alpha1::Expr condition_;
  Expr::ExpressionPtr expr_;
};

class MetadataMatcher : public Matcher {
public:
  MetadataMatcher(const Envoy::Matchers::MetadataMatcher& matcher) : matcher_(matcher) {}

  bool matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo& info) const override;

private:
  const Envoy::Matchers::MetadataMatcher matcher_;
};

class FilterStateMatcher : public Matcher {
public:
  FilterStateMatcher(const envoy::type::matcher::v3::FilterStateMatcher& matcher,
                     Server::Configuration::CommonFactoryContext& context)
      : matcher_(matcher, context) {}

  bool matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
               const StreamInfo::StreamInfo& info) const override;

private:
  const Envoy::Matchers::FilterStateMatcher matcher_;
};

/**
 * Perform a match against the request server from the client's connection
 * request. This is typically TLS SNI.
 */
class RequestedServerNameMatcher
    : public Matcher,
      Envoy::Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher> {
public:
  RequestedServerNameMatcher(const envoy::type::matcher::v3::StringMatcher& requested_server_name,
                             Server::Configuration::CommonFactoryContext& context)
      : Envoy::Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>(
            requested_server_name, context) {}

  bool matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo&) const override;
};

/**
 * Perform a match against the path header on the HTTP request. The query and fragment string are
 * removed from the path header before matching.
 */
class PathMatcher : public Matcher {
public:
  PathMatcher(const envoy::type::matcher::v3::PathMatcher& path_matcher,
              Server::Configuration::CommonFactoryContext& context)
      : path_matcher_(path_matcher, context) {}

  bool matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo&) const override;

private:
  const Matchers::PathMatcher path_matcher_;
};

class UriTemplateMatcher : public Matcher {
public:
  UriTemplateMatcher(const absl::StatusOr<Router::PathMatcherSharedPtr> uri_template_matcher)
      : uri_template_matcher_(uri_template_matcher.value()) {}

  bool matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo&) const override;

private:
  const Router::PathMatcherSharedPtr uri_template_matcher_;
};

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
