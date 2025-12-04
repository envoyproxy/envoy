#pragma once

#include <memory>

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/type/matcher/v3/path.pb.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/common/matchers.h"
#include "source/common/http/header_utility.h"
#include "source/common/network/cidr_range.h"
#include "source/common/network/lc_trie.h"
#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/common/rbac/matcher_interface.h"
#include "source/extensions/path/match/uri_template/uri_template_match.h"

#include "cel/expr/syntax.pb.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

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
  std::vector<MatcherConstPtr> matchers_;
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
  std::vector<MatcherConstPtr> matchers_;
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
  MatcherConstPtr matcher_;
};

/**
 * Perform a match against any HTTP header (or pseudo-header, such as `:path` or `:authority`). Will
 * always fail to match on any non-HTTP connection.
 */
class HeaderMatcher : public Matcher {
public:
  HeaderMatcher(const envoy::config::route::v3::HeaderMatcher& matcher,
                Server::Configuration::CommonFactoryContext& context)
      : header_(Http::HeaderUtility::createHeaderData(matcher, context)) {}

  bool matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo&) const override;

private:
  const Envoy::Http::HeaderUtility::HeaderDataPtr header_;
};

/**
 * Perform a match against IP CIDR ranges. This rule can be applied to connection remote,
 * downstream local address, downstream direct remote address or downstream remote address.
 * Uses LC Trie algorithm for optimal O(log n) performance in IP address range matching.
 */
class IPMatcher : public Matcher {
public:
  enum Type { ConnectionRemote = 0, DownstreamLocal, DownstreamDirectRemote, DownstreamRemote };

  // Single IP range constructor.
  static absl::StatusOr<std::unique_ptr<IPMatcher>>
  create(const envoy::config::core::v3::CidrRange& range, Type type);

  // Multiple IP ranges constructor.
  static absl::StatusOr<std::unique_ptr<IPMatcher>>
  create(const Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange>& ranges, Type type);

  bool matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo& info) const override;

private:
  // Private constructor for LC Trie-based matcher.
  IPMatcher(std::unique_ptr<Network::LcTrie::LcTrie<bool>> trie, Type type);

  // Helper method to extract IP address based on type, returning a reference to avoid copies.
  const Network::Address::InstanceConstSharedPtr&
  extractIpAddress(const Network::Connection& connection, const StreamInfo::StreamInfo& info) const;

  std::unique_ptr<Network::LcTrie::LcTrie<bool>> trie_;

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
      : matcher_(auth.has_principal_name() ? absl::make_optional<Matchers::StringMatcherImpl>(
                                                 auth.principal_name(), context)
                                           : absl::nullopt) {}

  bool matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo&) const override;

private:
  const absl::optional<Matchers::StringMatcherImpl> matcher_;
};

/**
 * Matches a Policy which is a collection of permission and principal matchers. If any action
 * matches a permission, the principals are then checked for a match.
 * The condition is a conjunction clause.
 */
class PolicyMatcher : public Matcher, NonCopyable {
public:
  PolicyMatcher(const envoy::config::rbac::v3::Policy& policy,
                ProtobufMessage::ValidationVisitor& validation_visitor,
                Server::Configuration::CommonFactoryContext& context,
                Expr::BuilderInstanceSharedConstPtr arena_builder)
      : permissions_(policy.permissions(), validation_visitor, context),
        principals_(policy.principals(), context),
        expr_([&]() -> absl::optional<Expr::CompiledExpression> {
          if (policy.has_condition()) {
            // Use arena-based builder if provided, otherwise use cached builder.
            auto builder =
                arena_builder != nullptr
                    ? arena_builder
                    : Expr::getBuilder(
                          context,
                          policy.has_cel_config()
                              ? Envoy::makeOptRef(policy.cel_config())
                              : OptRef<const envoy::config::core::v3::CelExpressionConfig>{});
            auto compiled = Expr::CompiledExpression::Create(builder, policy.condition());
            if (!compiled.ok()) {
              throw Expr::CelException(
                  absl::StrCat("failed to create an expression: ", compiled.status().message()));
            }
            return std::move(compiled.value());
          }
          return {};
        }()) {}

  bool matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo&) const override;

private:
  const OrMatcher permissions_;
  const OrMatcher principals_;
  const absl::optional<Expr::CompiledExpression> expr_;
};

class MetadataMatcher : public Matcher {
public:
  MetadataMatcher(const Envoy::Matchers::MetadataMatcher& matcher,
                  const envoy::config::rbac::v3::MetadataSource& metadata_source)
      : matcher_(matcher), metadata_source_(metadata_source) {}

  bool matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
               const StreamInfo::StreamInfo& info) const override;

private:
  const Envoy::Matchers::MetadataMatcher matcher_;
  const envoy::config::rbac::v3::MetadataSource metadata_source_;
};

class FilterStateMatcher : public Matcher {
public:
  FilterStateMatcher(const envoy::type::matcher::v3::FilterStateMatcher& matcher,
                     Server::Configuration::CommonFactoryContext& context);

  bool matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
               const StreamInfo::StreamInfo& info) const override;

private:
  const Envoy::Matchers::FilterStateMatcherPtr matcher_;
};

/**
 * Perform a match against the request server from the client's connection
 * request. This is typically TLS SNI.
 */
class RequestedServerNameMatcher : public Matcher, Envoy::Matchers::StringMatcherImpl {
public:
  RequestedServerNameMatcher(const envoy::type::matcher::v3::StringMatcher& requested_server_name,
                             Server::Configuration::CommonFactoryContext& context)
      : Envoy::Matchers::StringMatcherImpl(requested_server_name, context) {}

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
