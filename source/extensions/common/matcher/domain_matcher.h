#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/network/filter.h"
#include "envoy/server/factory_context.h"

#include "source/common/matcher/matcher.h"

#include "xds/type/matcher/v3/domain.pb.h"
#include "xds/type/matcher/v3/domain.pb.validate.h"

#include "absl/strings/match.h"
#include "absl/strings/ascii.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {

namespace {
bool isWildcardServerName(const std::string& name) {
  return absl::StartsWith(name, "*.") || name == "*";
}

/**
 * A "compressed" trie node with domain parts as values.
 */
class DomainNode {
private:
  absl::flat_hash_map<std::string, std::unique_ptr<DomainNode>> children_;
  absl::optional<OnMatch<DataType>> exact_on_match_;
  absl::optional<OnMatch<DataType>> prefix_on_match_;
};
} // namespace

using ::Envoy::Matcher::DataInputFactoryCb;
using ::Envoy::Matcher::DataInputGetResult;
using ::Envoy::Matcher::DataInputPtr;
using ::Envoy::Matcher::evaluateMatch;
using ::Envoy::Matcher::MatchState;
using ::Envoy::Matcher::MatchTree;
using ::Envoy::Matcher::OnMatch;
using ::Envoy::Matcher::OnMatchFactory;
using ::Envoy::Matcher::OnMatchFactoryCb;

/**
 * Implementation of a `sublinear` domain matcher.
template <class DataType>
class TrieMatcher : public MatchTree<DataType> {
public:
  TrieMatcher(DataInputPtr<DataType>&& data_input, absl::optional<OnMatch<DataType>> on_no_match,
              const std::shared_ptr<Network::LcTrie::LcTrie<TrieNode<DataType>>>& trie)
      : data_input_(std::move(data_input)), on_no_match_(std::move(on_no_match)), trie_(trie) {}

  typename MatchTree<DataType>::MatchResult match(const DataType& data) override {
    const auto input = data_input_->get(data);
    if (input.data_availability_ != DataInputGetResult::DataAvailability::AllDataAvailable) {
      return {MatchState::UnableToMatch, absl::nullopt};
    }
    if (!input.data_) {
      return {MatchState::MatchComplete, on_no_match_};
    }
    const Network::Address::InstanceConstSharedPtr addr =
        Network::Utility::parseInternetAddressNoThrow(*input.data_);
    if (!addr) {
      return {MatchState::MatchComplete, on_no_match_};
    }
    auto values = trie_->getData(addr);
    // The candidates returned by the LC trie are not in any specific order, so we
    // sort them by the prefix length first (longest first), and the order of declaration second.
    std::sort(values.begin(), values.end(), TrieNodeComparator<DataType>());
    bool first = true;
    for (const auto node : values) {
      if (!first && node.exclusive_) {
        continue;
      }
      if (node.on_match_->action_cb_) {
        return {MatchState::MatchComplete, OnMatch<DataType>{node.on_match_->action_cb_, nullptr}};
      }
      // Resume any subtree matching to preserve backtracking progress.
      auto matched = evaluateMatch(*node.on_match_->matcher_, data);
      if (matched.match_state_ == MatchState::UnableToMatch) {
        return {MatchState::UnableToMatch, absl::nullopt};
      }
      if (matched.match_state_ == MatchState::MatchComplete && matched.result_) {
        return {MatchState::MatchComplete, OnMatch<DataType>{matched.result_, nullptr}};
      }
      if (first) {
        first = false;
      }
    }
    return {MatchState::MatchComplete, on_no_match_};
  }

private:
  const DataInputPtr<DataType> data_input_;
  const absl::optional<OnMatch<DataType>> on_no_match_;
  std::shared_ptr<Network::LcTrie::LcTrie<TrieNode<DataType>>> trie_;
};
 */

template <class DataType>
class DomainMatcherFactoryBase : public ::Envoy::Matcher::CustomMatcherFactory<DataType> {
public:
  ::Envoy::Matcher::MatchTreeFactoryCb<DataType>
  createCustomMatcherFactoryCb(const Protobuf::Message& config,
                               Server::Configuration::ServerFactoryContext& factory_context,
                               DataInputFactoryCb<DataType> data_input,
                               absl::optional<OnMatchFactoryCb<DataType>> on_no_match,
                               OnMatchFactory<DataType>& on_match_factory) override {
    const auto& typed_config =
        MessageUtil::downcastAndValidate<const xds::type::matcher::v3::DomainMatcher&>(
            config, factory_context.messageValidationVisitor());
    absl::flat_hash_map<std::string, OnMatch<DataType>> server_names;
    for (const auto& domain_matcher : typed_config.domain_matchers()) {
      OnMatch<DataType> on_match = *on_match_factory.createOnMatch(domain_matcher.on_match()).value()();
      if (server_name.find('*') != std::string::npos && !isWildcardServerName(server_name)) {
        throw EnvoyException(fmt::format("invalid domain wildcard: {}", server_name));
      }
      for (const auto& server_name : domain_matcher.domains()) {
        // Reject internationalized domains to avoid ambiguity with case sensitivity.
        if (!absl::ascii_isascii(server_name)) {
          throw EnvoyException(fmt::format("non-ASCII domains are not supported: {}", server_name));
        }
        auto [_, inserted] = server_names.try_emplace(ascii::AsciiStrToLower(server_name), on_match);
        if (!inserted) {
          throw EnvoyException(fmt::format("duplicate domain: {}", server_name));
        }
      }
    }
    return [data_input, on_no_match]() {
      return std::make_unique<DomainMatcher<DataType>>(
          data_input(), on_no_match ? absl::make_optional(on_no_match.value()()) : absl::nullopt);
    };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<xds::type::matcher::v3::DomainMatcher>();
  }
  std::string name() const override { return "envoy.matching.custom_matchers.domain_matcher"; }
};

} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
