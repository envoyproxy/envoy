#pragma once

#include "envoy/common/optref.h"
#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

#include "source/common/matcher/matcher.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "xds/type/matcher/v3/domain.pb.h"
#include "xds/type/matcher/v3/domain.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {

using ::Envoy::Matcher::DataInputGetResult;
using ::Envoy::Matcher::DataInputPtr;
using ::Envoy::Matcher::evaluateMatch;
using ::Envoy::Matcher::MatchState;
using ::Envoy::Matcher::MatchTree;
using ::Envoy::Matcher::OnMatch;

/**
 * A "compressed" trie node with individual domain parts as edge values. Example:
 *
 *            (P)
 *     `com`     `org`
 *      (P)       ()
 *              `envoy`
 *                (E)
 *
 * with `P` signifying the prefix match and `E` signifying the exact match,
 * represents the following patterns:
 * - `*` that matches any domain;
 * - `*.com` that matches any domain ending with `.com` but not `com`;
 * - `envoy.org` that matches the domain `envoy.org` exactly.
 */
template <class DataType> struct DomainNode {
  absl::flat_hash_map<std::string, DomainNode<DataType>> children_;
  // Matches the exact path from the root.
  OptRef<OnMatch<DataType>> exact_on_match_;
  // Matches the prefix for the path from the root.
  OptRef<OnMatch<DataType>> prefix_on_match_;
  // Closest parent node with a prefix match.
  OptRef<DomainNode<DataType>> parent_prefix_;

  // Insert an on-match for the parts of the domain by handling the last optional
  // wildcard symbol as a special non-part.
  void insert(absl::Span<const std::string> parts, OnMatch<DataType>& on_match) {
    if (parts.empty()) {
      ASSERT(!exact_on_match_);
      exact_on_match_ = makeOptRef(on_match);
    } else if (parts[0] == "*") {
      // Prefix wildcards are unique and wildcard must be the last part.
      ASSERT(!prefix_on_match_ && parts.size() == 1);
      prefix_on_match_ = makeOptRef(on_match);
    } else {
      DomainNode<DataType>& child = children_[parts[0]];
      return child.insert(parts.subspan(1), on_match);
    }
  }

  // Link parent prefix from child nodes.
  void link(OptRef<DomainNode<DataType>> parent_prefix) {
    parent_prefix_ = parent_prefix;
    if (prefix_on_match_) {
      parent_prefix = makeOptRef(*this);
    }
    for (auto& [_, child] : children_) {
      child.link(parent_prefix);
    }
  }

  // Find the deepest node matching the reversed parts of the domain.
  // Returns a node and a bool indicating whether the match is exact for this node.
  std::pair<DomainNode const*, bool> find(absl::Span<const std::string> parts) const {
    if (parts.empty()) {
      return std::make_pair(this, true);
    }
    auto it = children_.find(parts[0]);
    if (it != children_.end()) {
      return it->second.find(parts.subspan(1));
    }
    return std::make_pair(this, false);
  }
};

template <class DataType> struct DomainTree {
  DomainNode<DataType> root_;
  std::vector<OnMatch<DataType>> on_matches_;
};

/**
 * General utilities for domain name matching.
 */
class DomainMatcherUtility {
public:
  static void validateServerName(const std::string& server_name);
  static void duplicateServerNameError(const std::string& server_name);
};

/**
 * Implementation of a `sublinear` domain matcher using a trie.
 **/
template <class DataType> class DomainMatcher : public MatchTree<DataType> {
public:
  DomainMatcher(DataInputPtr<DataType>&& data_input, absl::optional<OnMatch<DataType>> on_no_match,
                const std::shared_ptr<DomainTree<DataType>>& domain_tree)
      : data_input_(std::move(data_input)), on_no_match_(std::move(on_no_match)),
        domain_tree_(domain_tree) {}

  typename MatchTree<DataType>::MatchResult match(const DataType& data) override {
    const auto input = data_input_->get(data);
    if (input.data_availability_ != DataInputGetResult::DataAvailability::AllDataAvailable) {
      return {MatchState::UnableToMatch, absl::nullopt};
    }
    if (!input.data_) {
      return {MatchState::MatchComplete, on_no_match_};
    }
    std::vector<std::string> parts = absl::StrSplit(*input.data_, ".");
    std::reverse(parts.begin(), parts.end());
    // Traverse from the most specific match to the root and check for prefix matches.
    auto [node, exact] = domain_tree_->root_.find(parts);
    while (node) {
      OptRef<OnMatch<DataType>> on_match;
      if (exact) {
        on_match = node->exact_on_match_;
        exact = false;
      } else {
        on_match = node->prefix_on_match_;
      }
      if (on_match) {
        if (on_match->action_cb_) {
          return {MatchState::MatchComplete, OnMatch<DataType>{on_match->action_cb_, nullptr}};
        }
        auto matched = evaluateMatch(*on_match->matcher_, data);
        if (matched.match_state_ == MatchState::UnableToMatch) {
          return {MatchState::UnableToMatch, absl::nullopt};
        }
        if (matched.match_state_ == MatchState::MatchComplete && matched.result_) {
          return {MatchState::MatchComplete, OnMatch<DataType>{matched.result_, nullptr}};
        }
      }
      node = node->parent_prefix_.ptr();
    }
    return {MatchState::MatchComplete, on_no_match_};
  }

private:
  const DataInputPtr<DataType> data_input_;
  const absl::optional<OnMatch<DataType>> on_no_match_;
  const std::shared_ptr<DomainTree<DataType>> domain_tree_;
};

template <class DataType>
class DomainMatcherFactoryBase : public ::Envoy::Matcher::CustomMatcherFactory<DataType> {
public:
  ::Envoy::Matcher::MatchTreeFactoryCb<DataType> createCustomMatcherFactoryCb(
      const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& factory_context,
      ::Envoy::Matcher::DataInputFactoryCb<DataType> data_input,
      absl::optional<::Envoy::Matcher::OnMatchFactoryCb<DataType>> on_no_match,
      ::Envoy::Matcher::OnMatchFactory<DataType>& on_match_factory) override {
    const auto& typed_config =
        MessageUtil::downcastAndValidate<const xds::type::matcher::v3::ServerNameMatcher&>(
            config, factory_context.messageValidationVisitor());
    auto domain_tree = std::make_shared<DomainTree<DataType>>();
    // Ensures pointer stability when populating the vector.
    domain_tree->on_matches_.reserve(typed_config.domain_matchers().size());
    absl::flat_hash_map<std::string, std::reference_wrapper<OnMatch<DataType>>> server_names;
    for (const auto& domain_matcher : typed_config.domain_matchers()) {
      auto& on_match = domain_tree->on_matches_.emplace_back(
          on_match_factory.createOnMatch(domain_matcher.on_match()).value()());
      for (const auto& server_name : domain_matcher.domains()) {
        DomainMatcherUtility::validateServerName(server_name);
        auto [_, inserted] = server_names.try_emplace(absl::AsciiStrToLower(server_name), on_match);
        if (!inserted) {
          DomainMatcherUtility::duplicateServerNameError(server_name);
        }
      }
    }
    for (const auto& [server_name, on_match] : server_names) {
      std::vector<std::string> parts = absl::StrSplit(server_name, ".");
      std::reverse(parts.begin(), parts.end());
      domain_tree->root_.insert(parts, on_match);
    }
    domain_tree->root_.link(OptRef<DomainNode<DataType>>());
    return [data_input, on_no_match, domain_tree]() {
      return std::make_unique<DomainMatcher<DataType>>(
          data_input(), on_no_match ? absl::make_optional(on_no_match.value()()) : absl::nullopt,
          domain_tree);
    };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<xds::type::matcher::v3::ServerNameMatcher>();
  }
  std::string name() const override { return "envoy.matching.custom_matchers.domain_matcher"; }
};

} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
