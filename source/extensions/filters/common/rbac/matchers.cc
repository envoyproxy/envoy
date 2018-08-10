#include "extensions/filters/common/rbac/matchers.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

MatcherConstSharedPtr Matcher::create(const envoy::config::rbac::v2alpha::Permission& permission) {
  switch (permission.rule_case()) {
  case envoy::config::rbac::v2alpha::Permission::RuleCase::kAndRules:
    return std::make_shared<const AndMatcher>(permission.and_rules());
  case envoy::config::rbac::v2alpha::Permission::RuleCase::kOrRules:
    return std::make_shared<const OrMatcher>(permission.or_rules());
  case envoy::config::rbac::v2alpha::Permission::RuleCase::kHeader:
    return std::make_shared<const HeaderMatcher>(permission.header());
  case envoy::config::rbac::v2alpha::Permission::RuleCase::kDestinationIp:
    return std::make_shared<const IPMatcher>(permission.destination_ip(), true);
  case envoy::config::rbac::v2alpha::Permission::RuleCase::kDestinationPort:
    return std::make_shared<const PortMatcher>(permission.destination_port());
  case envoy::config::rbac::v2alpha::Permission::RuleCase::kAny:
    return std::make_shared<const AlwaysMatcher>();
  case envoy::config::rbac::v2alpha::Permission::RuleCase::kMetadata:
    return std::make_shared<const MetadataMatcher>(permission.metadata());
  case envoy::config::rbac::v2alpha::Permission::RuleCase::kNotRule:
    return std::make_shared<const NotMatcher>(permission.not_rule());
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

MatcherConstSharedPtr Matcher::create(const envoy::config::rbac::v2alpha::Principal& principal) {
  switch (principal.identifier_case()) {
  case envoy::config::rbac::v2alpha::Principal::IdentifierCase::kAndIds:
    return std::make_shared<const AndMatcher>(principal.and_ids());
  case envoy::config::rbac::v2alpha::Principal::IdentifierCase::kOrIds:
    return std::make_shared<const OrMatcher>(principal.or_ids());
  case envoy::config::rbac::v2alpha::Principal::IdentifierCase::kAuthenticated:
    return std::make_shared<const AuthenticatedMatcher>(principal.authenticated());
  case envoy::config::rbac::v2alpha::Principal::IdentifierCase::kSourceIp:
    return std::make_shared<const IPMatcher>(principal.source_ip(), false);
  case envoy::config::rbac::v2alpha::Principal::IdentifierCase::kHeader:
    return std::make_shared<const HeaderMatcher>(principal.header());
  case envoy::config::rbac::v2alpha::Principal::IdentifierCase::kAny:
    return std::make_shared<const AlwaysMatcher>();
  case envoy::config::rbac::v2alpha::Principal::IdentifierCase::kMetadata:
    return std::make_shared<const MetadataMatcher>(principal.metadata());
  case envoy::config::rbac::v2alpha::Principal::IdentifierCase::kNotId:
    return std::make_shared<const NotMatcher>(principal.not_id());
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

AndMatcher::AndMatcher(const envoy::config::rbac::v2alpha::Permission_Set& set) {
  for (const auto& rule : set.rules()) {
    matchers_.push_back(Matcher::create(rule));
  }
}

AndMatcher::AndMatcher(const envoy::config::rbac::v2alpha::Principal_Set& set) {
  for (const auto& id : set.ids()) {
    matchers_.push_back(Matcher::create(id));
  }
}

bool AndMatcher::matches(const Network::Connection& connection,
                         const Envoy::Http::HeaderMap& headers,
                         const envoy::api::v2::core::Metadata& metadata) const {
  for (const auto& matcher : matchers_) {
    if (!matcher->matches(connection, headers, metadata)) {
      return false;
    }
  }

  return true;
}

OrMatcher::OrMatcher(
    const Protobuf::RepeatedPtrField<::envoy::config::rbac::v2alpha::Permission>& rules) {
  for (const auto& rule : rules) {
    matchers_.push_back(Matcher::create(rule));
  }
}

OrMatcher::OrMatcher(
    const Protobuf::RepeatedPtrField<::envoy::config::rbac::v2alpha::Principal>& ids) {
  for (const auto& id : ids) {
    matchers_.push_back(Matcher::create(id));
  }
}

bool OrMatcher::matches(const Network::Connection& connection,
                        const Envoy::Http::HeaderMap& headers,
                        const envoy::api::v2::core::Metadata& metadata) const {
  for (const auto& matcher : matchers_) {
    if (matcher->matches(connection, headers, metadata)) {
      return true;
    }
  }

  return false;
}

bool NotMatcher::matches(const Network::Connection& connection,
                         const Envoy::Http::HeaderMap& headers,
                         const envoy::api::v2::core::Metadata& metadata) const {
  return !matcher_->matches(connection, headers, metadata);
}

bool HeaderMatcher::matches(const Network::Connection&, const Envoy::Http::HeaderMap& headers,
                            const envoy::api::v2::core::Metadata&) const {
  return Envoy::Http::HeaderUtility::matchHeaders(headers, header_);
}

bool IPMatcher::matches(const Network::Connection& connection, const Envoy::Http::HeaderMap&,
                        const envoy::api::v2::core::Metadata&) const {
  const Envoy::Network::Address::InstanceConstSharedPtr& ip =
      destination_ ? connection.localAddress() : connection.remoteAddress();

  return range_.isInRange(*ip.get());
}

bool PortMatcher::matches(const Network::Connection& connection, const Envoy::Http::HeaderMap&,
                          const envoy::api::v2::core::Metadata&) const {
  const Envoy::Network::Address::Ip* ip = connection.localAddress().get()->ip();
  return ip && ip->port() == port_;
}

bool AuthenticatedMatcher::matches(const Network::Connection& connection,
                                   const Envoy::Http::HeaderMap&,
                                   const envoy::api::v2::core::Metadata&) const {
  const auto* ssl = connection.ssl();
  if (!ssl) { // connection was not authenticated
    return false;
  } else if (name_.empty()) { // matcher allows any subject
    return true;
  }

  std::string principal = ssl->uriSanPeerCertificate();
  principal = principal.empty() ? ssl->subjectPeerCertificate() : principal;

  return principal == name_;
}

bool MetadataMatcher::matches(const Network::Connection&, const Envoy::Http::HeaderMap&,
                              const envoy::api::v2::core::Metadata& metadata) const {
  return matcher_.match(metadata);
}

bool PolicyMatcher::matches(const Network::Connection& connection,
                            const Envoy::Http::HeaderMap& headers,
                            const envoy::api::v2::core::Metadata& metadata) const {
  return permissions_.matches(connection, headers, metadata) &&
         principals_.matches(connection, headers, metadata);
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
