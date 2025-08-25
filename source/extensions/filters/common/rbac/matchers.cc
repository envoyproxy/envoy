#include "source/extensions/filters/common/rbac/matchers.h"

#include "envoy/common/exception.h"
#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/upstream/upstream.h"

#include "source/common/config/utility.h"
#include "source/extensions/filters/common/rbac/matcher_extension.h"
#include "source/extensions/filters/common/rbac/principal_extension.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

MatcherConstPtr Matcher::create(const envoy::config::rbac::v3::Permission& permission,
                                ProtobufMessage::ValidationVisitor& validation_visitor,
                                Server::Configuration::CommonFactoryContext& context) {
  switch (permission.rule_case()) {
  case envoy::config::rbac::v3::Permission::RuleCase::kAndRules:
    return std::make_unique<const AndMatcher>(permission.and_rules(), validation_visitor, context);
  case envoy::config::rbac::v3::Permission::RuleCase::kOrRules:
    return std::make_unique<const OrMatcher>(permission.or_rules(), validation_visitor, context);
  case envoy::config::rbac::v3::Permission::RuleCase::kHeader:
    return std::make_unique<const HeaderMatcher>(permission.header(), context);
  case envoy::config::rbac::v3::Permission::RuleCase::kDestinationIp: {
    auto matcher_result =
        IPMatcher::create(permission.destination_ip(), IPMatcher::Type::DownstreamLocal);
    THROW_IF_NOT_OK_REF(matcher_result.status());
    return std::move(matcher_result.value());
  }
  case envoy::config::rbac::v3::Permission::RuleCase::kDestinationPort:
    return std::make_unique<const PortMatcher>(permission.destination_port());
  case envoy::config::rbac::v3::Permission::RuleCase::kDestinationPortRange:
    return std::make_unique<const PortRangeMatcher>(permission.destination_port_range());
  case envoy::config::rbac::v3::Permission::RuleCase::kAny:
    return std::make_unique<const AlwaysMatcher>();
  case envoy::config::rbac::v3::Permission::RuleCase::kMetadata:
    return std::make_unique<const MetadataMatcher>(
        Matchers::MetadataMatcher(permission.metadata(), context),
        envoy::config::rbac::v3::MetadataSource::DYNAMIC);
  case envoy::config::rbac::v3::Permission::RuleCase::kSourcedMetadata:
    return std::make_unique<const MetadataMatcher>(
        Matchers::MetadataMatcher(permission.sourced_metadata().metadata_matcher(), context),
        permission.sourced_metadata().metadata_source());
  case envoy::config::rbac::v3::Permission::RuleCase::kNotRule:
    return std::make_unique<const NotMatcher>(permission.not_rule(), validation_visitor, context);
  case envoy::config::rbac::v3::Permission::RuleCase::kRequestedServerName:
    return std::make_unique<const RequestedServerNameMatcher>(permission.requested_server_name(),
                                                              context);
  case envoy::config::rbac::v3::Permission::RuleCase::kUrlPath:
    return std::make_unique<const PathMatcher>(permission.url_path(), context);
  case envoy::config::rbac::v3::Permission::RuleCase::kUriTemplate: {
    auto& factory =
        Config::Utility::getAndCheckFactory<Router::PathMatcherFactory>(permission.uri_template());
    ProtobufTypes::MessagePtr config = Envoy::Config::Utility::translateAnyToFactoryConfig(
        permission.uri_template().typed_config(), validation_visitor, factory);
    return std::make_unique<const UriTemplateMatcher>(factory.createPathMatcher(*config));
  }
  case envoy::config::rbac::v3::Permission::RuleCase::kMatcher: {
    auto& factory =
        Config::Utility::getAndCheckFactory<MatcherExtensionFactory>(permission.matcher());
    return factory.create(permission.matcher(), validation_visitor);
  }
  case envoy::config::rbac::v3::Permission::RuleCase::RULE_NOT_SET:
    break; // Fall through to PANIC.
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

MatcherConstPtr Matcher::create(const envoy::config::rbac::v3::Principal& principal,
                                Server::Configuration::CommonFactoryContext& context) {
  switch (principal.identifier_case()) {
  case envoy::config::rbac::v3::Principal::IdentifierCase::kAndIds:
    return std::make_unique<const AndMatcher>(principal.and_ids(), context);
  case envoy::config::rbac::v3::Principal::IdentifierCase::kOrIds:
    return std::make_unique<const OrMatcher>(principal.or_ids(), context);
  case envoy::config::rbac::v3::Principal::IdentifierCase::kAuthenticated:
    return std::make_unique<const AuthenticatedMatcher>(principal.authenticated(), context);
  case envoy::config::rbac::v3::Principal::IdentifierCase::kSourceIp: {
    auto matcher_result =
        IPMatcher::create(principal.source_ip(), IPMatcher::Type::ConnectionRemote);
    THROW_IF_NOT_OK_REF(matcher_result.status());
    return std::move(matcher_result.value());
  }
  case envoy::config::rbac::v3::Principal::IdentifierCase::kDirectRemoteIp: {
    auto matcher_result =
        IPMatcher::create(principal.direct_remote_ip(), IPMatcher::Type::DownstreamDirectRemote);
    THROW_IF_NOT_OK_REF(matcher_result.status());
    return std::move(matcher_result.value());
  }
  case envoy::config::rbac::v3::Principal::IdentifierCase::kRemoteIp: {
    auto matcher_result =
        IPMatcher::create(principal.remote_ip(), IPMatcher::Type::DownstreamRemote);
    THROW_IF_NOT_OK_REF(matcher_result.status());
    return std::move(matcher_result.value());
  }
  case envoy::config::rbac::v3::Principal::IdentifierCase::kHeader:
    return std::make_unique<const HeaderMatcher>(principal.header(), context);
  case envoy::config::rbac::v3::Principal::IdentifierCase::kAny:
    return std::make_unique<const AlwaysMatcher>();
  case envoy::config::rbac::v3::Principal::IdentifierCase::kMetadata:
    return std::make_unique<const MetadataMatcher>(
        Matchers::MetadataMatcher(principal.metadata(), context),
        envoy::config::rbac::v3::MetadataSource::DYNAMIC);
  case envoy::config::rbac::v3::Principal::IdentifierCase::kSourcedMetadata:
    return std::make_unique<const MetadataMatcher>(
        Matchers::MetadataMatcher(principal.sourced_metadata().metadata_matcher(), context),
        principal.sourced_metadata().metadata_source());
  case envoy::config::rbac::v3::Principal::IdentifierCase::kNotId:
    return std::make_unique<const NotMatcher>(principal.not_id(), context);
  case envoy::config::rbac::v3::Principal::IdentifierCase::kUrlPath:
    return std::make_unique<const PathMatcher>(principal.url_path(), context);
  case envoy::config::rbac::v3::Principal::IdentifierCase::kFilterState:
    return std::make_unique<const FilterStateMatcher>(principal.filter_state(), context);
  case envoy::config::rbac::v3::Principal::IdentifierCase::kCustom:
    return Config::Utility::getAndCheckFactory<PrincipalExtensionFactory>(principal.custom())
        .create(principal.custom(), context);
  case envoy::config::rbac::v3::Principal::IdentifierCase::IDENTIFIER_NOT_SET:
    break; // Fall through to PANIC.
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

AndMatcher::AndMatcher(const envoy::config::rbac::v3::Permission::Set& set,
                       ProtobufMessage::ValidationVisitor& validation_visitor,
                       Server::Configuration::CommonFactoryContext& context) {
  matchers_.reserve(set.rules_size());
  for (const auto& rule : set.rules()) {
    matchers_.emplace_back(Matcher::create(rule, validation_visitor, context));
  }
}

AndMatcher::AndMatcher(const envoy::config::rbac::v3::Principal::Set& set,
                       Server::Configuration::CommonFactoryContext& context) {
  matchers_.reserve(set.ids_size());
  for (const auto& id : set.ids()) {
    matchers_.emplace_back(Matcher::create(id, context));
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

OrMatcher::OrMatcher(const Protobuf::RepeatedPtrField<envoy::config::rbac::v3::Permission>& rules,
                     ProtobufMessage::ValidationVisitor& validation_visitor,
                     Server::Configuration::CommonFactoryContext& context) {
  matchers_.reserve(rules.size());
  for (const auto& rule : rules) {
    matchers_.emplace_back(Matcher::create(rule, validation_visitor, context));
  }
}

OrMatcher::OrMatcher(const Protobuf::RepeatedPtrField<envoy::config::rbac::v3::Principal>& ids,
                     Server::Configuration::CommonFactoryContext& context) {
  matchers_.reserve(ids.size());
  for (const auto& id : ids) {
    matchers_.emplace_back(Matcher::create(id, context));
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
  return header_->matchesHeaders(headers);
}

// static
absl::StatusOr<std::unique_ptr<IPMatcher>>
IPMatcher::create(const envoy::config::core::v3::CidrRange& range, Type type) {
  // Convert single range to CidrRange with proper error handling.
  auto cidr_result = Network::Address::CidrRange::create(range);
  if (!cidr_result.ok()) {
    return absl::InvalidArgumentError(
        fmt::format("Failed to create CIDR range: {}", cidr_result.status().message()));
  }

  std::vector<Network::Address::CidrRange> ranges;
  ranges.push_back(std::move(cidr_result.value()));

  // Create LC Trie directly following the pattern from Unified IP Matcher.
  // Note: LcTrie constructor may throw EnvoyException on invalid input, but this
  // should not happen as we've already validated the CIDR range above.
  auto trie = std::make_unique<Network::LcTrie::LcTrie<bool>>(
      std::vector<std::pair<bool, std::vector<Network::Address::CidrRange>>>{{true, ranges}});

  return std::unique_ptr<IPMatcher>(new IPMatcher(std::move(trie), type));
}

// static
absl::StatusOr<std::unique_ptr<IPMatcher>>
IPMatcher::create(const Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange>& ranges,
                  Type type) {
  if (ranges.empty()) {
    return absl::InvalidArgumentError("Empty IP range list provided");
  }

  // Convert protobuf ranges to CidrRange vector.
  std::vector<Network::Address::CidrRange> cidr_ranges;
  cidr_ranges.reserve(ranges.size());
  for (const auto& range : ranges) {
    auto cidr_result = Network::Address::CidrRange::create(range);
    if (!cidr_result.ok()) {
      return absl::InvalidArgumentError(
          fmt::format("Failed to create CIDR range: {}", cidr_result.status().message()));
    }
    cidr_ranges.push_back(std::move(cidr_result.value()));
  }

  // Create LC Trie directly following the pattern from Unified IP Matcher.
  // Note: LcTrie constructor may throw EnvoyException on invalid input, but this
  // should not happen as we've already validated the CIDR range above.
  auto trie = std::make_unique<Network::LcTrie::LcTrie<bool>>(
      std::vector<std::pair<bool, std::vector<Network::Address::CidrRange>>>{{true, cidr_ranges}});

  return std::unique_ptr<IPMatcher>(new IPMatcher(std::move(trie), type));
}

IPMatcher::IPMatcher(std::unique_ptr<Network::LcTrie::LcTrie<bool>> trie, Type type)
    : trie_(std::move(trie)), type_(type) {}

const Network::Address::InstanceConstSharedPtr&
IPMatcher::extractIpAddress(const Network::Connection& connection,
                            const StreamInfo::StreamInfo& info) const {
  switch (type_) {
  case ConnectionRemote:
    return connection.connectionInfoProvider().remoteAddress();
  case DownstreamLocal:
    return info.downstreamAddressProvider().localAddress();
  case DownstreamDirectRemote:
    return info.downstreamAddressProvider().directRemoteAddress();
  case DownstreamRemote:
    return info.downstreamAddressProvider().remoteAddress();
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

bool IPMatcher::matches(const Network::Connection& connection, const Envoy::Http::RequestHeaderMap&,
                        const StreamInfo::StreamInfo& info) const {
  // Extract IP address using reference to avoid shared_ptr copies.
  const auto& address = extractIpAddress(connection, info);
  // Guard against non-IP addresses (e.g., pipe) or missing address.
  if (!address) {
    return false;
  }
  const auto* ip = address->ip();
  if (ip == nullptr) {
    return false;
  }
  return !trie_->getData(address).empty();
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
  if (metadata_source_ == envoy::config::rbac::v3::MetadataSource::ROUTE) {
    // Return false if there's no route since we can't match its metadata
    return info.route() ? matcher_.match(info.route()->metadata()) : false;
  }
  return matcher_.match(info.dynamicMetadata());
}

FilterStateMatcher::FilterStateMatcher(const envoy::type::matcher::v3::FilterStateMatcher& matcher,
                                       Server::Configuration::CommonFactoryContext& context)
    : matcher_(THROW_OR_RETURN_VALUE(Envoy::Matchers::FilterStateMatcher::create(matcher, context),
                                     Envoy::Matchers::FilterStateMatcherPtr)) {}

bool FilterStateMatcher::matches(const Network::Connection&, const Envoy::Http::RequestHeaderMap&,
                                 const StreamInfo::StreamInfo& info) const {
  return matcher_->match(info.filterState());
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

bool UriTemplateMatcher::matches(const Network::Connection&,
                                 const Envoy::Http::RequestHeaderMap& headers,
                                 const StreamInfo::StreamInfo&) const {
  return uri_template_matcher_->match(headers.getPathValue());
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
