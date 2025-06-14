#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/network/filter.h"
#include "envoy/server/factory_context.h"

#include "source/common/matcher/matcher.h"

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

template <class DataType> struct DomainMatch {
  size_t declaration_index_;
  std::shared_ptr<OnMatch<DataType>> on_match_;

  DomainMatch(size_t index, std::shared_ptr<OnMatch<DataType>> match)
      : declaration_index_(index), on_match_(std::move(match)) {}
};

/**
 * Domain matcher implementing ServerNameMatcher specification.
 * Matches domains using exact lookups and wildcard patterns with proper precedence.
 */
template <class DataType> class DomainTrieMatcher : public MatchTree<DataType> {
public:
  DomainTrieMatcher(DataInputPtr<DataType>&& data_input,
                    absl::optional<OnMatch<DataType>> on_no_match,
                    absl::flat_hash_map<std::string, DomainMatch<DataType>> exact_matches,
                    absl::flat_hash_map<std::string, DomainMatch<DataType>> wildcard_matches,
                    std::shared_ptr<OnMatch<DataType>> global_wildcard_match)
      : data_input_(std::move(data_input)), on_no_match_(std::move(on_no_match)),
        exact_matches_(std::move(exact_matches)), wildcard_matches_(std::move(wildcard_matches)),
        global_wildcard_match_(std::move(global_wildcard_match)) {
    const auto input_type = data_input_->dataInputType();
    if (input_type != Envoy::Matcher::DefaultMatchingDataType) {
      throw EnvoyException(
          absl::StrCat("Unsupported data input type: ", input_type,
                       ", currently only string type is supported in domain matcher"));
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

    // Find best match using ServerNameMatcher specification algorithm
    const auto best_match = findBestMatch(domain);
    if (best_match) {
      return MatchTree<DataType>::handleRecursionAndSkips(*best_match, data, skipped_match_cb);
    }

    return MatchTree<DataType>::handleRecursionAndSkips(on_no_match_, data, skipped_match_cb);
  }

private:
  // Try exact match first, then wildcards from longest to shortest suffix.
  std::shared_ptr<OnMatch<DataType>> findBestMatch(absl::string_view domain) const {
    // 1. Try exact match first (highest priority).
    const auto exact_it = exact_matches_.find(std::string(domain));
    if (exact_it != exact_matches_.end()) {
      return exact_it->second.on_match_;
    }

    // 2. Try wildcard matches from longest suffix to shortest.
    // For "www.example.com", try "*.example.com", then "*.com".
    std::shared_ptr<OnMatch<DataType>> best_wildcard_match;
    size_t best_wildcard_index = SIZE_MAX;

    size_t dot_pos = domain.find('.');
    while (dot_pos != absl::string_view::npos) {
      const auto suffix = domain.substr(dot_pos + 1);
      const auto wildcard_pattern = absl::StrCat("*.", suffix);

      const auto wildcard_it = wildcard_matches_.find(wildcard_pattern);
      if (wildcard_it != wildcard_matches_.end()) {
        // Use declaration order for tie-breaking (earlier wins).
        if (!best_wildcard_match || wildcard_it->second.declaration_index_ < best_wildcard_index) {
          best_wildcard_match = wildcard_it->second.on_match_;
          best_wildcard_index = wildcard_it->second.declaration_index_;
        }
      }

      dot_pos = domain.find('.', dot_pos + 1);
    }

    if (best_wildcard_match) {
      return best_wildcard_match;
    }

    // 3. Finally try global wildcard "*" (lowest priority).
    return global_wildcard_match_;
  }

  const DataInputPtr<DataType> data_input_;
  const absl::optional<OnMatch<DataType>> on_no_match_;
  const absl::flat_hash_map<std::string, DomainMatch<DataType>> exact_matches_;
  const absl::flat_hash_map<std::string, DomainMatch<DataType>> wildcard_matches_;
  const std::shared_ptr<OnMatch<DataType>> global_wildcard_match_;
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
    const auto& typed_config =
        MessageUtil::downcastAndValidate<const xds::type::matcher::v3::ServerNameMatcher&>(
            config, factory_context.messageValidationVisitor());

    validateConfiguration(typed_config);

    std::vector<OnMatchFactoryCb<DataType>> match_children;
    match_children.reserve(typed_config.domain_matchers().size());

    absl::flat_hash_map<std::string, DomainMatch<DataType>> exact_matches;
    absl::flat_hash_map<std::string, DomainMatch<DataType>> wildcard_matches;
    std::shared_ptr<OnMatch<DataType>> global_wildcard_match;

    buildDomainMaps(typed_config, on_match_factory, match_children, exact_matches, wildcard_matches,
                    global_wildcard_match);

    // Capture all dependencies for proper lifetime management.
    auto children =
        std::make_shared<std::vector<OnMatchFactoryCb<DataType>>>(std::move(match_children));
    auto exact = std::make_shared<absl::flat_hash_map<std::string, DomainMatch<DataType>>>(
        std::move(exact_matches));
    auto wildcard = std::make_shared<absl::flat_hash_map<std::string, DomainMatch<DataType>>>(
        std::move(wildcard_matches));

    return [data_input, exact, wildcard, global_wildcard_match, children, on_no_match]() {
      return std::make_unique<DomainTrieMatcher<DataType>>(
          data_input(), on_no_match ? absl::make_optional(on_no_match.value()()) : absl::nullopt,
          *exact, *wildcard, global_wildcard_match);
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<xds::type::matcher::v3::ServerNameMatcher>();
  }

  std::string name() const override { return "envoy.matching.custom_matchers.domain_matcher"; }

private:
  void validateConfiguration(const xds::type::matcher::v3::ServerNameMatcher& config) const {
    absl::flat_hash_set<std::string> seen_domains;
    seen_domains.reserve(config.domain_matchers().size() * 2);

    for (const auto& domain_matcher : config.domain_matchers()) {
      for (const auto& domain : domain_matcher.domains()) {
        if (!seen_domains.insert(domain).second) {
          throw EnvoyException(absl::StrCat("Duplicate domain in ServerNameMatcher: ", domain));
        }
        validateDomainFormat(domain);
      }
    }
  }

  static void validateDomainFormat(absl::string_view domain) {
    if (domain == "*") {
      return; // Global wildcard is valid
    }

    if (domain.empty()) {
      throw EnvoyException("Empty domain in ServerNameMatcher");
    }

    // Validate wildcard format: must be "*.domain" with exactly one * at start
    if (domain[0] == '*') {
      if (domain.size() < 3 || domain[1] != '.' || domain.find('*', 1) != absl::string_view::npos) {
        throw EnvoyException(absl::StrCat("Invalid wildcard domain format: ", domain));
      }
    }
  }

  void buildDomainMaps(const xds::type::matcher::v3::ServerNameMatcher& config,
                       OnMatchFactory<DataType>& on_match_factory,
                       std::vector<OnMatchFactoryCb<DataType>>& match_children,
                       absl::flat_hash_map<std::string, DomainMatch<DataType>>& exact_matches,
                       absl::flat_hash_map<std::string, DomainMatch<DataType>>& wildcard_matches,
                       std::shared_ptr<OnMatch<DataType>>& global_wildcard_match) const {

    size_t declaration_index = 0;

    for (const auto& domain_matcher : config.domain_matchers()) {
      match_children.push_back(*on_match_factory.createOnMatch(domain_matcher.on_match()));
      const auto on_match_cb = match_children.back();
      auto on_match = std::make_shared<OnMatch<DataType>>(on_match_cb());

      for (const auto& domain : domain_matcher.domains()) {
        if (domain == "*") {
          // Global wildcard. We use earliest declaration if multiple exist.
          if (!global_wildcard_match) {
            global_wildcard_match = on_match;
          }
        } else if (domain[0] == '*') {
          // Wildcard pattern.
          wildcard_matches.emplace(domain, DomainMatch<DataType>(declaration_index, on_match));
        } else {
          // Exact match.
          exact_matches.emplace(domain, DomainMatch<DataType>(declaration_index, on_match));
        }
        declaration_index++;
      }
    }
  }
};

class NetworkDomainMatcherFactory : public DomainTrieMatcherFactoryBase<Network::MatchingData> {};
class HttpDomainMatcherFactory : public DomainTrieMatcherFactoryBase<Http::HttpMatchingData> {};

} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
