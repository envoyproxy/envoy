#include "extensions/filters/common/rbac/matchers.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

MatcherConstSharedPtr Matcher::create(const envoy::config::rbac::v2::Permission& permission) {
  switch (permission.rule_case()) {
  case envoy::config::rbac::v2::Permission::RuleCase::kAndRules:
    return std::make_shared<const AndMatcher>(permission.and_rules());
  case envoy::config::rbac::v2::Permission::RuleCase::kOrRules:
    return std::make_shared<const OrMatcher>(permission.or_rules());
  case envoy::config::rbac::v2::Permission::RuleCase::kHeader:
    return std::make_shared<const HeaderMatcher>(permission.header());
  case envoy::config::rbac::v2::Permission::RuleCase::kDestinationIp:
    return std::make_shared<const IPMatcher>(permission.destination_ip(), true);
  case envoy::config::rbac::v2::Permission::RuleCase::kDestinationPort:
    return std::make_shared<const PortMatcher>(permission.destination_port());
  case envoy::config::rbac::v2::Permission::RuleCase::kAny:
    return std::make_shared<const AlwaysMatcher>();
  case envoy::config::rbac::v2::Permission::RuleCase::kMetadata:
    return std::make_shared<const MetadataMatcher>(permission.metadata());
  case envoy::config::rbac::v2::Permission::RuleCase::kNotRule:
    return std::make_shared<const NotMatcher>(permission.not_rule());
  case envoy::config::rbac::v2::Permission::RuleCase::kRequestedServerName:
    return std::make_shared<const RequestedServerNameMatcher>(permission.requested_server_name());
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

MatcherConstSharedPtr Matcher::create(const envoy::config::rbac::v2::Principal& principal) {
  switch (principal.identifier_case()) {
  case envoy::config::rbac::v2::Principal::IdentifierCase::kAndIds:
    return std::make_shared<const AndMatcher>(principal.and_ids());
  case envoy::config::rbac::v2::Principal::IdentifierCase::kOrIds:
    return std::make_shared<const OrMatcher>(principal.or_ids());
  case envoy::config::rbac::v2::Principal::IdentifierCase::kAuthenticated:
    return std::make_shared<const AuthenticatedMatcher>(principal.authenticated());
  case envoy::config::rbac::v2::Principal::IdentifierCase::kSourceIp:
    return std::make_shared<const IPMatcher>(principal.source_ip(), false);
  case envoy::config::rbac::v2::Principal::IdentifierCase::kHeader:
    return std::make_shared<const HeaderMatcher>(principal.header());
  case envoy::config::rbac::v2::Principal::IdentifierCase::kAny:
    return std::make_shared<const AlwaysMatcher>();
  case envoy::config::rbac::v2::Principal::IdentifierCase::kMetadata:
    return std::make_shared<const MetadataMatcher>(principal.metadata());
  case envoy::config::rbac::v2::Principal::IdentifierCase::kNotId:
    return std::make_shared<const NotMatcher>(principal.not_id());
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

AndMatcher::AndMatcher(const envoy::config::rbac::v2::Permission_Set& set) {
  for (const auto& rule : set.rules()) {
    matchers_.push_back(Matcher::create(rule));
  }
}

AndMatcher::AndMatcher(const envoy::config::rbac::v2::Principal_Set& set) {
  for (const auto& id : set.ids()) {
    matchers_.push_back(Matcher::create(id));
  }
}

bool AndMatcher::matches(const Network::Connection& connection,
                         const Envoy::Http::HeaderMap& headers,
                         const StreamInfo::StreamInfo& info) const {
  for (const auto& matcher : matchers_) {
    if (!matcher->matches(connection, headers, info)) {
      return false;
    }
  }

  return true;
}

OrMatcher::OrMatcher(
    const Protobuf::RepeatedPtrField<::envoy::config::rbac::v2::Permission>& rules) {
  for (const auto& rule : rules) {
    matchers_.push_back(Matcher::create(rule));
  }
}

OrMatcher::OrMatcher(const Protobuf::RepeatedPtrField<::envoy::config::rbac::v2::Principal>& ids) {
  for (const auto& id : ids) {
    matchers_.push_back(Matcher::create(id));
  }
}

bool OrMatcher::matches(const Network::Connection& connection,
                        const Envoy::Http::HeaderMap& headers,
                        const StreamInfo::StreamInfo& info) const {
  for (const auto& matcher : matchers_) {
    if (matcher->matches(connection, headers, info)) {
      return true;
    }
  }

  return false;
}

bool NotMatcher::matches(const Network::Connection& connection,
                         const Envoy::Http::HeaderMap& headers,
                         const StreamInfo::StreamInfo& info) const {
  return !matcher_->matches(connection, headers, info);
}

bool HeaderMatcher::matches(const Network::Connection&, const Envoy::Http::HeaderMap& headers,
                            const StreamInfo::StreamInfo&) const {
  return Envoy::Http::HeaderUtility::matchHeaders(headers, header_);
}

bool IPMatcher::matches(const Network::Connection& connection, const Envoy::Http::HeaderMap&,
                        const StreamInfo::StreamInfo&) const {
  const Envoy::Network::Address::InstanceConstSharedPtr& ip =
      destination_ ? connection.localAddress() : connection.remoteAddress();

  return range_.isInRange(*ip.get());
}

bool PortMatcher::matches(const Network::Connection& connection, const Envoy::Http::HeaderMap&,
                          const StreamInfo::StreamInfo&) const {
  const Envoy::Network::Address::Ip* ip = connection.localAddress().get()->ip();
  return ip && ip->port() == port_;
}

bool AuthenticatedMatcher::matches(const Network::Connection& connection,
                                   const Envoy::Http::HeaderMap&,
                                   const StreamInfo::StreamInfo&) const {
  const auto& ssl = connection.ssl();
  if (!ssl) { // connection was not authenticated
    return false;
  } else if (!matcher_.has_value()) { // matcher allows any subject
    return true;
  }

  const auto uriSans = ssl->uriSanPeerCertificate();
  std::string principal;
  // If set, The URI SAN  or DNS SAN in that order is used as Principal, otherwise the subject field
  // is used.
  if (!uriSans.empty()) {
    principal = uriSans[0];
  } else {
    const auto dnsSans = ssl->dnsSansPeerCertificate();
    if (!dnsSans.empty()) {
      principal = dnsSans[0];
    } else {
      principal = ssl->subjectPeerCertificate();
    }
  }

  return matcher_.value().match(principal);
}

bool MetadataMatcher::matches(const Network::Connection&, const Envoy::Http::HeaderMap&,
                              const StreamInfo::StreamInfo& info) const {
  return matcher_.match(info.dynamicMetadata());
}

bool PolicyMatcher::matches(const Network::Connection& connection,
                            const Envoy::Http::HeaderMap& headers,
                            const StreamInfo::StreamInfo& info) const {
  return permissions_.matches(connection, headers, info) &&
         principals_.matches(connection, headers, info) &&
         (expr_ == nullptr ? true : Expr::matches(*expr_, info, headers));
}

bool RequestedServerNameMatcher::matches(const Network::Connection& connection,
                                         const Envoy::Http::HeaderMap&,
                                         const StreamInfo::StreamInfo&) const {
  return match(connection.requestedServerName());
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
