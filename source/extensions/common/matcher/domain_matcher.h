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

    // Get all candidates.
    const auto candidates = findAllMatchCandidates(domain);

    // Try candidates in order of priority with backtracking.
    bool first_candidate = true;
    for (const auto& candidate : candidates) {
      // Track skipped results to detect keep_matching behavior.
      size_t initial_skipped_count = 0;
      SkippedMatchCb tracking_skipped_cb = nullptr;

      if (skipped_match_cb && first_candidate) {
        tracking_skipped_cb = [&initial_skipped_count, skipped_match_cb](
                                  const ::Envoy::Matcher::ActionFactoryCb& action_cb) {
          initial_skipped_count++;
          skipped_match_cb(action_cb);
        };
      } else {
        tracking_skipped_cb = skipped_match_cb;
      }

      MatchResult processed_match =
          MatchTree<DataType>::handleRecursionAndSkips(*candidate, data, tracking_skipped_cb);

      if (processed_match.isMatch() || processed_match.isInsufficientData()) {
        return processed_match;
      }

      // If the first candidate (most specific match) returns NoMatch and adds skipped results,
      // this indicates ``keep_matching`` behavior.
      // Domain matcher cannot provide additional matches for the same domain, so we should
      // stop here rather than trying less specific patterns.
      if (first_candidate && processed_match.isNoMatch() && initial_skipped_count > 0) {
        return processed_match;
      }

      first_candidate = false;
      // No-match isn't definitive, so continue checking candidates for backtracking.
    }

    return MatchTree<DataType>::handleRecursionAndSkips(on_no_match_, data, skipped_match_cb);
  }

private:
  // Find all match candidates in priority order for backtracking and return candidates ordered
  // by ServerNameMatcher specification:
  // Exact first, then wildcards from longest to shortest suffix, finally global wildcard.
  std::vector<std::shared_ptr<OnMatch<DataType>>>
  findAllMatchCandidates(absl::string_view domain) const {
    std::vector<std::shared_ptr<OnMatch<DataType>>> candidates;

    // 1. Try exact match first (highest priority).
    const auto exact_it = exact_matches_.find(std::string(domain));
    if (exact_it != exact_matches_.end()) {
      candidates.push_back(exact_it->second.on_match_);
    }

    // 2. Collect wildcard matches from longest suffix to shortest.
    // For "www.example.com", collect "*.example.com", then "*.com".
    std::vector<std::pair<std::shared_ptr<OnMatch<DataType>>, size_t>> wildcard_candidates;

    size_t dot_pos = domain.find('.');
    while (dot_pos != absl::string_view::npos) {
      const auto suffix = domain.substr(dot_pos + 1);
      const auto wildcard_pattern = absl::StrCat("*.", suffix);

      const auto wildcard_it = wildcard_matches_.find(wildcard_pattern);
      if (wildcard_it != wildcard_matches_.end()) {
        wildcard_candidates.emplace_back(wildcard_it->second.on_match_,
                                         wildcard_it->second.declaration_index_);
      }

      dot_pos = domain.find('.', dot_pos + 1);
    }

    // Sort wildcards by declaration order for tie-breaking (earlier declared wins).
    std::sort(wildcard_candidates.begin(), wildcard_candidates.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    // Add sorted wildcard candidates to the main candidates list.
    for (const auto& wildcard_candidate : wildcard_candidates) {
      candidates.push_back(wildcard_candidate.first);
    }

    // 3. Finally add global wildcard "*" (lowest priority).
    if (global_wildcard_match_) {
      candidates.push_back(global_wildcard_match_);
    }

    return candidates;
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
