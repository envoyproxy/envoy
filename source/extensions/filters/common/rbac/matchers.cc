#include "source/extensions/filters/common/rbac/matchers.h"

#include "envoy/config/rbac/v3/rbac.pb.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

MatcherConstSharedPtr Matcher::create(const envoy::config::rbac::v3::Permission& permission) {
  switch (permission.rule_case()) {
  case envoy::config::rbac::v3::Permission::RuleCase::kAndRules:
    return std::make_shared<const AndMatcher>(permission.and_rules());
  case envoy::config::rbac::v3::Permission::RuleCase::kOrRules:
    return std::make_shared<const OrMatcher>(permission.or_rules());
  case envoy::config::rbac::v3::Permission::RuleCase::kHeader:
    return std::make_shared<const HeaderMatcher>(permission.header());
  case envoy::config::rbac::v3::Permission::RuleCase::kDestinationIp:
    return std::make_shared<const IPMatcher>(permission.destination_ip(),
                                             IPMatcher::Type::DownstreamLocal);
  case envoy::config::rbac::v3::Permission::RuleCase::kDestinationPort:
    return std::make_shared<const PortMatcher>(permission.destination_port());
  case envoy::config::rbac::v3::Permission::RuleCase::kDestinationPortRange:
    return std::make_shared<const PortRangeMatcher>(permission.destination_port_range());
  case envoy::config::rbac::v3::Permission::RuleCase::kAny:
    return std::make_shared<const AlwaysMatcher>();
  case envoy::config::rbac::v3::Permission::RuleCase::kMetadata:
    return std::make_shared<const MetadataMatcher>(permission.metadata());
  case envoy::config::rbac::v3::Permission::RuleCase::kNotRule:
    return std::make_shared<const NotMatcher>(permission.not_rule());
  case envoy::config::rbac::v3::Permission::RuleCase::kRequestedServerName:
    return std::make_shared<const RequestedServerNameMatcher>(permission.requested_server_name());
  case envoy::config::rbac::v3::Permission::RuleCase::kUrlPath:
    return std::make_shared<const PathMatcher>(permission.url_path());
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

MatcherConstSharedPtr Matcher::create(const envoy::config::rbac::v3::Principal& principal) {
  switch (principal.identifier_case()) {
  case envoy::config::rbac::v3::Principal::IdentifierCase::kAndIds:
    return std::make_shared<const AndMatcher>(principal.and_ids());
  case envoy::config::rbac::v3::Principal::IdentifierCase::kOrIds:
    return std::make_shared<const OrMatcher>(principal.or_ids());
  case envoy::config::rbac::v3::Principal::IdentifierCase::kAuthenticated:
    return std::make_shared<const AuthenticatedMatcher>(principal.authenticated());
  case envoy::config::rbac::v3::Principal::IdentifierCase::kSourceIp:
    return std::make_shared<const IPMatcher>(principal.source_ip(),
                                             IPMatcher::Type::ConnectionRemote);
  case envoy::config::rbac::v3::Principal::IdentifierCase::kDirectRemoteIp:
    return std::make_shared<const IPMatcher>(principal.direct_remote_ip(),
                                             IPMatcher::Type::DownstreamDirectRemote);
  case envoy::config::rbac::v3::Principal::IdentifierCase::kRemoteIp:
    return std::make_shared<const IPMatcher>(principal.remote_ip(),
                                             IPMatcher::Type::DownstreamRemote);
  case envoy::config::rbac::v3::Principal::IdentifierCase::kHeader:
    return std::make_shared<const HeaderMatcher>(principal.header());
  case envoy::config::rbac::v3::Principal::IdentifierCase::kAny:
    return std::make_shared<const AlwaysMatcher>();
  case envoy::config::rbac::v3::Principal::IdentifierCase::kMetadata:
    return std::make_shared<const MetadataMatcher>(principal.metadata());
  case envoy::config::rbac::v3::Principal::IdentifierCase::kNotId:
    return std::make_shared<const NotMatcher>(principal.not_id());
  case envoy::config::rbac::v3::Principal::IdentifierCase::kUrlPath:
    return std::make_shared<const PathMatcher>(principal.url_path());
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

AndMatcher::AndMatcher(const envoy::config::rbac::v3::Permission::Set& set) {
  for (const auto& rule : set.rules()) {
    matchers_.push_back(Matcher::create(rule));
  }
}

AndMatcher::AndMatcher(const envoy::config::rbac::v3::Principal::Set& set) {
  for (const auto& id : set.ids()) {
    matchers_.push_back(Matcher::create(id));
  }
}

bool AndMatcher::matches(const Network::Connection& connection,
                         const Envoy::Http::RequestHeaderMap& headers,
                         const StreamInfo::StreamInfo& info) const {
  for (const auto& matcher : matchers_) {
    if (!matcher->matches(connection, headers, info)) {
      return false;
    }
  }

  return true;
}

OrMatcher::OrMatcher(const Protobuf::RepeatedPtrField<envoy::config::rbac::v3::Permission>& rules) {
  for (const auto& rule : rules) {
    matchers_.push_back(Matcher::create(rule));
  }
}

OrMatcher::OrMatcher(const Protobuf::RepeatedPtrField<envoy::config::rbac::v3::Principal>& ids) {
  for (const auto& id : ids) {
    matchers_.push_back(Matcher::create(id));
  }
}

bool OrMatcher::matches(const Network::Connection& connection,
                        const Envoy::Http::RequestHeaderMap& headers,
                        const StreamInfo::StreamInfo& info) const {
  for (const auto& matcher : matchers_) {
    if (matcher->matches(connection, headers, info)) {
      return true;
    }
  }

  return false;
}

bool NotMatcher::matches(const Network::Connection& connection,
                         const Envoy::Http::RequestHeaderMap& headers,
                         const StreamInfo::StreamInfo& info) const {
  return !matcher_->matches(connection, headers, info);
}

bool HeaderMatcher::matches(const Network::Connection&,
                            const Envoy::Http::RequestHeaderMap& headers,
                            const StreamInfo::StreamInfo&) const {
  return Envoy::Http::HeaderUtility::matchHeaders(headers, header_);
}

bool IPMatcher::matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap&,
                        const StreamInfo::StreamInfo& info) const {
  Envoy::Network::Address::InstanceConstSharedPtr ip;
  switch (type_) {
  case ConnectionRemote:
    ip = connection.addressProvider().remoteAddress();
    break;
  case DownstreamLocal:
    ip = info.downstreamAddressProvider().localAddress();
    break;
  case DownstreamDirectRemote:
    ip = info.downstreamAddressProvider().directRemoteAddress();
    break;
  case DownstreamRemote:
    ip = info.downstreamAddressProvider().remoteAddress();
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  return range_.isInRange(*ip.get());
}

bool PortMatcher::matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
                          const StreamInfo::StreamInfo& info) const {
  const Envoy::Network::Address::Ip* ip =
      info.downstreamAddressProvider().localAddress().get()->ip();
  return ip && ip->port() == port_;
}

PortRangeMatcher::PortRangeMatcher(const ::envoy::type::v3::Int32Range& range)
    : start_(range.start()), end_(range.end()) {
  auto start = range.start();
  auto end = range.end();
  if (start < 0 || start > 65536) {
    throw EnvoyException(fmt::format("range start {} is out of bounds", start));
  }
  if (end < 0 || end > 65536) {
    throw EnvoyException(fmt::format("range end {} is out of bounds", end));
  }
  if (start >= end) {
    throw EnvoyException(
        fmt::format("range start {} cannot be greater or equal than range end {}", start, end));
  }
}

bool PortRangeMatcher::matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
                               const StreamInfo::StreamInfo& info) const {
  const Envoy::Network::Address::Ip* ip =
      info.downstreamAddressProvider().localAddress().get()->ip();
  if (ip) {
    const auto port = ip->port();
    return start_ <= port && port < end_;
  } else {
    return false;
  }
}

bool AuthenticatedMatcher::matches(const Network::Connection& connection,
                                   const Envoy::Http::RequestHeaderMap&,
                                   const StreamInfo::StreamInfo&) const {
  const auto& ssl = connection.ssl();
  if (!ssl) { // connection was not authenticated
    return false;
  } else if (!matcher_.has_value()) { // matcher allows any subject
    return true;
  }

  // If set, The URI SAN  or DNS SAN in that order is used as Principal, otherwise the subject field
  // is used.
  if (!ssl->uriSanPeerCertificate().empty()) {
    for (const std::string& uri : ssl->uriSanPeerCertificate()) {
      if (matcher_.value().match(uri)) {
        return true;
      }
    }
  }
  if (!ssl->dnsSansPeerCertificate().empty()) {
    for (const std::string& dns : ssl->dnsSansPeerCertificate()) {
      if (matcher_.value().match(dns)) {
        return true;
      }
    }
  }
  return matcher_.value().match(ssl->subjectPeerCertificate());
}

bool MetadataMatcher::matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
                              const StreamInfo::StreamInfo& info) const {
  return matcher_.match(info.dynamicMetadata());
}

bool PolicyMatcher::matches(const Network::Connection& connection,
                            const Envoy::Http::RequestHeaderMap& headers,
                            const StreamInfo::StreamInfo& info) const {
  return permissions_.matches(connection, headers, info) &&
         principals_.matches(connection, headers, info) &&
         (expr_ == nullptr ? true : Expr::matches(*expr_, info, headers));
}

bool RequestedServerNameMatcher::matches(const Network::Connection& connection,
                                         const Envoy::Http::RequestHeaderMap&,
                                         const StreamInfo::StreamInfo&) const {
  return match(connection.requestedServerName());
}

bool PathMatcher::matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap& headers,
                          const StreamInfo::StreamInfo&) const {
  if (headers.Path() == nullptr) {
    return false;
  }
  return path_matcher_.match(headers.getPathValue());
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
