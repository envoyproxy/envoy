#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/network/filter.h"
#include "envoy/server/factory_context.h"

#include "source/common/matcher/matcher.h"

#include "absl/status/status.h"
#include "xds/type/matcher/v3/domain.pb.h"
#include "xds/type/matcher/v3/domain.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {

using ::Envoy::Matcher::DataInputFactoryCb;
using ::Envoy::Matcher::DataInputGetResult;
using ::Envoy::Matcher::DataInputPtr;
using ::Envoy::Matcher::MatchResult;
using ::Envoy::Matcher::MatchTree;
using ::Envoy::Matcher::OnMatch;
using ::Envoy::Matcher::OnMatchFactory;
using ::Envoy::Matcher::OnMatchFactoryCb;
using ::Envoy::Matcher::SkippedMatchCb;

/**
 * Configuration for domain matcher that holds all domain mappings and match actions.
 */
template <class DataType> struct DomainMatcherConfig {
  // Exact domain matches (e.g., "api.example.com")
  absl::flat_hash_map<std::string, std::shared_ptr<OnMatch<DataType>>> exact_matches_;

  // Wildcard matches stored without "*." prefix for efficient lookups.
  // Maps suffix (e.g., "example.com") to match action.
  absl::flat_hash_map<std::string, std::shared_ptr<OnMatch<DataType>>> wildcard_matches_;

  // Global wildcard "*" match. They are given lowest priority.
  std::shared_ptr<OnMatch<DataType>> global_wildcard_match_;
};

/**
 * Domain matcher which implements ServerNameMatcher specs. It matches domains
 * using exact lookups and wildcard patterns in the following order:
 * 1. Exact matches (highest priority)
 * 2. Wildcards by longest suffix match (not declaration order)
 * 3. Global wildcard "*" (lowest priority).
 */
template <class DataType> class DomainTrieMatcher : public MatchTree<DataType> {
public:
  DomainTrieMatcher(DataInputPtr<DataType>&& data_input,
                    absl::optional<OnMatch<DataType>> on_no_match,
                    std::shared_ptr<DomainMatcherConfig<DataType>> config)
      : data_input_(std::move(data_input)), on_no_match_(std::move(on_no_match)),
        config_(std::move(config)) {
    absl::Status validation_status = validateDataInputType();
    if (!validation_status.ok()) {
      throw EnvoyException(std::string(validation_status.message()));
    }
  }

  MatchResult match(const DataType& data, SkippedMatchCb skipped_match_cb = nullptr) override {
    const auto input = data_input_->get(data);
    if (input.data_availability_ != DataInputGetResult::DataAvailability::AllDataAvailable) {
      return MatchResult::insufficientData();
    }

    if (absl::holds_alternative<absl::monostate>(input.data_)) {
      return MatchTree<DataType>::handleRecursionAndSkips(on_no_match_, data, skipped_match_cb);
    }

    const auto& domain = absl::get<std::string>(input.data_);
    if (domain.empty()) {
      return MatchTree<DataType>::handleRecursionAndSkips(on_no_match_, data, skipped_match_cb);
    }

    // 1. Try exact match first (highest priority).
    auto exact_it = config_->exact_matches_.find(domain);
    if (exact_it != config_->exact_matches_.end()) {
      MatchResult result =
          MatchTree<DataType>::handleRecursionAndSkips(*(exact_it->second), data, skipped_match_cb);

      // If ``keep_matching`` is used, treat as no match and continue to wildcards.
      if (result.isMatch() || result.isInsufficientData()) {
        return result;
      }
    }

    // 2. Try wildcard matches from longest suffix to shortest.
    // For "www.example.com", try "example.com", then "com".
    auto wildcard_matches = findAllWildcardMatches(domain);
    for (const auto& wildcard_match : wildcard_matches) {
      MatchResult result =
          MatchTree<DataType>::handleRecursionAndSkips(*wildcard_match, data, skipped_match_cb);

      // If ``keep_matching`` is used, treat as no match and continue to next wildcard.
      if (result.isMatch() || result.isInsufficientData()) {
        return result;
      }
    }

    // 3. Finally try global wildcard "*" (lowest priority).
    if (config_->global_wildcard_match_) {
      MatchResult result = MatchTree<DataType>::handleRecursionAndSkips(
          *(config_->global_wildcard_match_), data, skipped_match_cb);

      if (result.isMatch() || result.isInsufficientData()) {
        return result;
      }
    }

    return MatchTree<DataType>::handleRecursionAndSkips(on_no_match_, data, skipped_match_cb);
  }

private:
  // Validate that the data input type is supported.
  absl::Status validateDataInputType() const {
    const auto input_type = data_input_->dataInputType();
    if (input_type != Envoy::Matcher::DefaultMatchingDataType) {
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported data input type: ", input_type,
                       ", currently only string type is supported in domain matcher"));
    }
    return absl::OkStatus();
  }

  // Find all wildcard matches for the given domain ordered from longest to shortest suffix.
  // Returns empty vector if no wildcard matches are found.
  std::vector<std::shared_ptr<OnMatch<DataType>>>
  findAllWildcardMatches(absl::string_view domain) const {
    std::vector<std::shared_ptr<OnMatch<DataType>>> matches;

    size_t dot_pos = domain.find('.');
    while (dot_pos != absl::string_view::npos) {
      const auto suffix = domain.substr(dot_pos + 1);

      // Direct lookup without creating temporary strings.
      auto wildcard_it = config_->wildcard_matches_.find(suffix);
      if (wildcard_it != config_->wildcard_matches_.end()) {
        matches.push_back(wildcard_it->second);
      }

      // Find next "dot" for shorter patterns.
      dot_pos = domain.find('.', dot_pos + 1);
    }

    return matches;
  }

  const DataInputPtr<DataType> data_input_;
  const absl::optional<OnMatch<DataType>> on_no_match_;
  const std::shared_ptr<DomainMatcherConfig<DataType>> config_;
};

template <class DataType>
class DomainTrieMatcherFactoryBase : public ::Envoy::Matcher::CustomMatcherFactory<DataType> {
public:
  ::Envoy::Matcher::MatchTreeFactoryCb<DataType>
  createCustomMatcherFactoryCb(const Protobuf::Message& config,
                               Server::Configuration::ServerFactoryContext& factory_context,
                               DataInputFactoryCb<DataType> data_input,
                               absl::optional<OnMatchFactoryCb<DataType>> on_no_match,
                               OnMatchFactory<DataType>& on_match_factory) override {
    auto typed_config = std::make_shared<xds::type::matcher::v3::ServerNameMatcher>(
        MessageUtil::downcastAndValidate<const xds::type::matcher::v3::ServerNameMatcher&>(
            config, factory_context.messageValidationVisitor()));

    absl::Status validation_status = validateConfiguration(*typed_config);
    if (!validation_status.ok()) {
      throw EnvoyException(std::string(validation_status.message()));
    }

    auto domain_config = buildDomainMatcherConfig(*typed_config, on_match_factory);

    return [data_input, domain_config, on_no_match]() {
      return std::make_unique<DomainTrieMatcher<DataType>>(
          data_input(), on_no_match ? absl::make_optional(on_no_match.value()()) : absl::nullopt,
          domain_config);
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<xds::type::matcher::v3::ServerNameMatcher>();
  }

  std::string name() const override { return "envoy.matching.custom_matchers.domain_matcher"; }

private:
  absl::Status
  validateConfiguration(const xds::type::matcher::v3::ServerNameMatcher& config) const {
    absl::flat_hash_set<std::string> seen_domains;
    seen_domains.reserve(getTotalDomainCount(config));

    for (const auto& domain_matcher : config.domain_matchers()) {
      for (const auto& domain : domain_matcher.domains()) {
        if (!seen_domains.insert(domain).second) {
          return absl::InvalidArgumentError(
              absl::StrCat("Duplicate domain in ServerNameMatcher: ", domain));
        }

        absl::Status validation_status = validateDomainFormat(domain);
        if (!validation_status.ok()) {
          return validation_status;
        }
      }
    }

    return absl::OkStatus();
  }

  static absl::Status validateDomainFormat(absl::string_view domain) {
    if (domain == "*") {
      return absl::OkStatus(); // Global wildcard is valid.
    }

    if (domain.empty()) {
      return absl::InvalidArgumentError("Empty domain in ServerNameMatcher");
    }

    // Check for invalid wildcard patterns anywhere in the domain.
    const size_t wildcard_pos = domain.find('*');
    if (wildcard_pos != absl::string_view::npos) {
      // Only allow "*." at the beginning (prefix wildcard).
      if (wildcard_pos != 0 || domain.size() < 3 || domain[1] != '.') {
        return absl::InvalidArgumentError(
            absl::StrCat("Invalid wildcard domain format: ", domain,
                         ". Only '*' and '*.domain' patterns are supported"));
      }

      // Ensure no additional wildcards exist.
      if (domain.find('*', 1) != absl::string_view::npos) {
        return absl::InvalidArgumentError(absl::StrCat("Invalid wildcard domain format: ", domain,
                                                       ". Multiple wildcards are not supported"));
      }
    }

    return absl::OkStatus();
  }

  static size_t getTotalDomainCount(const xds::type::matcher::v3::ServerNameMatcher& config) {
    size_t count = 0;
    for (const auto& domain_matcher : config.domain_matchers()) {
      count += domain_matcher.domains().size();
    }
    return count;
  }

  std::shared_ptr<DomainMatcherConfig<DataType>>
  buildDomainMatcherConfig(const xds::type::matcher::v3::ServerNameMatcher& config,
                           OnMatchFactory<DataType>& on_match_factory) const {
    auto domain_config = std::make_shared<DomainMatcherConfig<DataType>>();

    for (const auto& domain_matcher : config.domain_matchers()) {
      auto on_match_factory_cb = *on_match_factory.createOnMatch(domain_matcher.on_match());
      auto on_match = std::make_shared<OnMatch<DataType>>(on_match_factory_cb());

      for (const auto& domain : domain_matcher.domains()) {
        if (domain == "*") {
          // Global wildcard. We use first declaration if multiple exist.
          if (!domain_config->global_wildcard_match_) {
            domain_config->global_wildcard_match_ = on_match;
          }
        } else if (domain[0] == '*') {
          // Wildcard pattern. We strip "*." prefix for efficient lookup.
          const auto suffix = domain.substr(2); // Remove "*."
          domain_config->wildcard_matches_.emplace(std::string(suffix), on_match);
        } else {
          // Exact match.
          domain_config->exact_matches_.emplace(domain, on_match);
        }
      }
    }

    return domain_config;
  }
};

class NetworkDomainMatcherFactory : public DomainTrieMatcherFactoryBase<Network::MatchingData> {};
class HttpDomainMatcherFactory : public DomainTrieMatcherFactoryBase<Http::HttpMatchingData> {};

} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
