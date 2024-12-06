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
using ::Envoy::Matcher::evaluateMatch;
using ::Envoy::Matcher::MatchState;
using ::Envoy::Matcher::MatchTree;
using ::Envoy::Matcher::OnMatch;
using ::Envoy::Matcher::OnMatchFactory;
using ::Envoy::Matcher::OnMatchFactoryCb;

template <class DataType> struct DomainNode {
  size_t index_;            // Preserve original index for ordering
  std::string domain_part_; // Individual part of domain (e.g., "com", "example")
  bool is_wildcard_;        // Whether this node represents a wildcard
  bool is_terminal_;        // Whether this node is end of a domain pattern
  std::shared_ptr<OnMatch<DataType>> on_match_; // Action to take on match
  absl::flat_hash_map<std::string, std::unique_ptr<DomainNode>> children_; // Child nodes

  DomainNode() : index_(0), is_wildcard_(false), is_terminal_(false) {}

  std::unique_ptr<DomainNode> clone() const {
    auto node = std::make_unique<DomainNode>();
    node->index_ = index_;
    node->domain_part_ = domain_part_;
    node->is_wildcard_ = is_wildcard_;
    node->is_terminal_ = is_terminal_;
    node->on_match_ = on_match_;

    for (const auto& [key, child] : children_) {
      node->children_.emplace(key, child ? child->clone() : nullptr);
    }
    return node;
  }
};

/**
 * Implementation of a domain-specific trie matcher.
 */
template <class DataType> class DomainTrieMatcher : public MatchTree<DataType> {
public:
  DomainTrieMatcher(DataInputPtr<DataType>&& data_input,
                    absl::optional<OnMatch<DataType>> on_no_match,
                    std::unique_ptr<DomainNode<DataType>> root)
      : data_input_(std::move(data_input)), on_no_match_(std::move(on_no_match)),
        root_(std::move(root)) {
    auto input_type = data_input_->dataInputType();
    if (input_type != Envoy::Matcher::DefaultMatchingDataType) {
      throw EnvoyException(
          absl::StrCat("Unsupported data input type: ", input_type,
                       ", currently only string type is supported in domain matcher"));
    }
  }

  static std::vector<std::string> splitAndReverseDomain(absl::string_view domain) {
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos < domain.length()) {
      size_t dot_pos = domain.find('.', pos);
      if (dot_pos == absl::string_view::npos) {
        parts.push_back(std::string(domain.substr(pos)));
        break;
      }
      parts.push_back(std::string(domain.substr(pos, dot_pos - pos)));
      pos = dot_pos + 1;
    }
    std::reverse(parts.begin(), parts.end());
    return parts;
  }

  typename MatchTree<DataType>::MatchResult match(const DataType& data) override {
    const auto input = data_input_->get(data);
    if (input.data_availability_ != DataInputGetResult::DataAvailability::AllDataAvailable) {
      return {MatchState::UnableToMatch, absl::nullopt};
    }

    if (absl::holds_alternative<absl::monostate>(input.data_)) {
      return {MatchState::MatchComplete, on_no_match_};
    }

    const auto& domain = absl::get<std::string>(input.data_);
    if (domain.empty()) {
      return {MatchState::MatchComplete, on_no_match_};
    }

    const auto parts = splitAndReverseDomain(domain);
    std::shared_ptr<OnMatch<DataType>> best_match;
    size_t best_match_length = 0;

    // Check exact matches first
    {
      const DomainNode<DataType>* current = root_.get();
      size_t matched_length = 0;
      for (const auto& part : parts) {
        auto it = current->children_.find(part);
        if (it == current->children_.end()) {
          break;
        }
        matched_length++;
        if (it->second->is_terminal_ && it->second->on_match_) {
          best_match = it->second->on_match_;
          best_match_length = matched_length;
        }
        current = it->second.get();
      }
    }

    // If no exact match found, check wildcard matches
    if (!best_match) {
      const DomainNode<DataType>* current = root_.get();
      size_t matched_length = 0;
      for (const auto& part : parts) {
        auto wildcard = current->children_.find("*");
        if (wildcard != current->children_.end() && wildcard->second->is_terminal_ &&
            wildcard->second->on_match_ && matched_length > best_match_length) {
          best_match = wildcard->second->on_match_;
          best_match_length = matched_length;
        }

        auto it = current->children_.find(part);
        if (it == current->children_.end()) {
          break;
        }
        matched_length++;
        current = it->second.get();
      }
    }

    // Finally, check global wildcard only if no other match was found
    if (!best_match && root_->is_wildcard_ && root_->is_terminal_ && root_->on_match_) {
      best_match = root_->on_match_;
    }

    if (best_match) {
      return {MatchState::MatchComplete, OnMatch<DataType>{best_match->action_cb_, nullptr}};
    }

    return {MatchState::MatchComplete, on_no_match_};
  }

private:
  const DataInputPtr<DataType> data_input_;
  const absl::optional<OnMatch<DataType>> on_no_match_;
  std::unique_ptr<DomainNode<DataType>> root_;
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

    validateDomains(typed_config);

    std::vector<OnMatchFactoryCb<DataType>> match_children;
    match_children.reserve(typed_config.domain_matchers().size());

    auto root = std::make_shared<DomainNode<DataType>>();
    buildDomainTrie(typed_config, on_match_factory, match_children, root.get());
    auto children =
        std::make_shared<std::vector<OnMatchFactoryCb<DataType>>>(std::move(match_children));

    return [data_input, root, children, on_no_match]() {
      return std::make_unique<DomainTrieMatcher<DataType>>(
          data_input(), on_no_match ? absl::make_optional(on_no_match.value()()) : absl::nullopt,
          root->clone());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<xds::type::matcher::v3::ServerNameMatcher>();
  }

  std::string name() const override { return "envoy.matching.custom_matchers.domain_matcher"; }

private:
  void validateDomains(const xds::type::matcher::v3::ServerNameMatcher& config) const {
    absl::flat_hash_set<std::string> unique_domains;

    for (const auto& domain_matcher : config.domain_matchers()) {
      for (const auto& domain : domain_matcher.domains()) {
        if (!unique_domains.insert(domain).second) {
          throw EnvoyException(absl::StrCat("Duplicate domain in ServerNameMatcher: ", domain));
        }

        if (domain != "*") {
          bool is_wildcard = domain[0] == '*';
          if (is_wildcard &&
              (domain.size() < 2 || domain[1] != '.' || domain.find('*', 1) != std::string::npos)) {
            throw EnvoyException(absl::StrCat("Invalid wildcard domain format: ", domain));
          }
        }
      }
    }
  }

  void buildDomainTrie(const xds::type::matcher::v3::ServerNameMatcher& config,
                       OnMatchFactory<DataType>& on_match_factory,
                       std::vector<OnMatchFactoryCb<DataType>>& match_children,
                       DomainNode<DataType>* root) const {
    size_t matcher_index = 0;

    for (const auto& domain_matcher : config.domain_matchers()) {
      match_children.push_back(*on_match_factory.createOnMatch(domain_matcher.on_match()));
      const auto on_match_cb = match_children.back();
      auto on_match = std::make_shared<OnMatch<DataType>>(on_match_cb());

      for (const auto& domain : domain_matcher.domains()) {
        if (domain == "*") {
          root->is_wildcard_ = true;
          root->is_terminal_ = true;
          root->on_match_ = on_match;
          continue;
        }

        bool is_wildcard = domain[0] == '*';
        std::vector<std::string> parts;

        if (is_wildcard) {
          // For wildcard domains like "*.api.example.com", we want:
          // root -> "com" -> "example" -> "api" -> "*" (terminal)
          parts = DomainTrieMatcher<DataType>::splitAndReverseDomain(domain.substr(2));
          if (parts.empty()) {
            continue;
          }
        } else {
          parts = DomainTrieMatcher<DataType>::splitAndReverseDomain(domain);
        }

        DomainNode<DataType>* current = root;

        // Add all parts except the last one
        for (size_t i = 0; i < parts.size() - 1; i++) {
          const auto& part = parts[i];
          auto& next = current->children_[part];
          if (!next) {
            next = std::make_unique<DomainNode<DataType>>();
            next->index_ = ++matcher_index;
            next->domain_part_ = part;
          }
          current = next.get();
        }

        // Handle the last part differently for wildcards
        if (is_wildcard) {
          // Get the last concrete part
          const auto& last_part = parts.back();
          auto& last_node = current->children_[last_part];
          if (!last_node) {
            last_node = std::make_unique<DomainNode<DataType>>();
            last_node->index_ = ++matcher_index;
            last_node->domain_part_ = last_part;
          }
          current = last_node.get();

          // Add wildcard node
          auto& wildcard_node = current->children_["*"];
          if (!wildcard_node) {
            wildcard_node = std::make_unique<DomainNode<DataType>>();
            wildcard_node->index_ = ++matcher_index;
            wildcard_node->domain_part_ = "*";
          }
          wildcard_node->is_wildcard_ = true;
          wildcard_node->is_terminal_ = true;
          wildcard_node->on_match_ = on_match;
        } else {
          // Regular domain - set match on last node
          const auto& last_part = parts.back();
          auto& last_node = current->children_[last_part];
          if (!last_node) {
            last_node = std::make_unique<DomainNode<DataType>>();
            last_node->index_ = ++matcher_index;
            last_node->domain_part_ = last_part;
          }
          last_node->is_terminal_ = true;
          last_node->on_match_ = on_match;
        }
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
