#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/network/filter.h"
#include "envoy/server/factory_context.h"

#include "source/common/matcher/matcher.h"
#include "source/common/network/lc_trie.h"
#include "source/common/network/utility.h"

#include "xds/type/matcher/v3/ip.pb.h"
#include "xds/type/matcher/v3/ip.pb.validate.h"

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

template <class DataType> struct TrieNode {
  size_t index_;
  uint32_t prefix_len_;
  bool exclusive_;
  std::shared_ptr<OnMatch<DataType>> on_match_;

  friend bool operator==(const TrieNode<DataType>& lhs, const TrieNode<DataType>& rhs) {
    return lhs.index_ == rhs.index_ && lhs.prefix_len_ == rhs.prefix_len_ &&
           lhs.exclusive_ == rhs.exclusive_ && lhs.on_match_ == rhs.on_match_;
  }
  template <typename H>
  friend H AbslHashValue(H h, // NOLINT(readability-identifier-naming)
                         const TrieNode<DataType>& node) {
    return H::combine(std::move(h), node.index_, node.prefix_len_, node.exclusive_, node.on_match_);
  }
};

template <class DataType> struct TrieNodeComparator {
  inline bool operator()(const TrieNode<DataType>& lhs, const TrieNode<DataType>& rhs) const {
    if (lhs.prefix_len_ > rhs.prefix_len_) {
      return true;
    }
    if (lhs.prefix_len_ == rhs.prefix_len_ && lhs.index_ < rhs.index_) {
      return true;
    }
    return false;
  }
};

/**
 * Implementation of a `sublinear` LC-trie matcher.
 */
template <class DataType> class TrieMatcher : public MatchTree<DataType> {
public:
  TrieMatcher(DataInputPtr<DataType>&& data_input, absl::optional<OnMatch<DataType>> on_no_match,
              const std::shared_ptr<Network::LcTrie::LcTrie<TrieNode<DataType>>>& trie)
      : data_input_(std::move(data_input)), on_no_match_(std::move(on_no_match)), trie_(trie) {
    auto input_type = data_input_->dataInputType();
    if (input_type != Envoy::Matcher::DefaultMatchingDataType) {
      throw EnvoyException(
          absl::StrCat("Unsupported data input type: ", input_type,
                       ", currently only string type is supported in trie matcher"));
    }
  }

  typename MatchTree<DataType>::MatchResult match(const DataType& data) override {
    const auto input = data_input_->get(data);
    if (input.data_availability_ != DataInputGetResult::DataAvailability::AllDataAvailable) {
      return {MatchState::UnableToMatch, absl::nullopt};
    }
    if (absl::holds_alternative<absl::monostate>(input.data_)) {
      return {MatchState::MatchComplete, on_no_match_};
    }
    const Network::Address::InstanceConstSharedPtr addr =
        Network::Utility::parseInternetAddressNoThrow(absl::get<std::string>(input.data_));
    if (!addr) {
      return {MatchState::MatchComplete, on_no_match_};
    }
    auto values = trie_->getData(addr);
    // The candidates returned by the LC trie are not in any specific order, so we
    // sort them by the prefix length first (longest first), and the order of declaration second.
    std::sort(values.begin(), values.end(), TrieNodeComparator<DataType>());
    bool first = true;
    for (const auto& node : values) {
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

template <class DataType>
class TrieMatcherFactoryBase : public ::Envoy::Matcher::CustomMatcherFactory<DataType> {
public:
  ::Envoy::Matcher::MatchTreeFactoryCb<DataType>
  createCustomMatcherFactoryCb(const Protobuf::Message& config,
                               Server::Configuration::ServerFactoryContext& factory_context,
                               DataInputFactoryCb<DataType> data_input,
                               absl::optional<OnMatchFactoryCb<DataType>> on_no_match,
                               OnMatchFactory<DataType>& on_match_factory) override {
    const auto& typed_config =
        MessageUtil::downcastAndValidate<const xds::type::matcher::v3::IPMatcher&>(
            config, factory_context.messageValidationVisitor());
    std::vector<OnMatchFactoryCb<DataType>> match_children;
    match_children.reserve(typed_config.range_matchers().size());
    for (const auto& range_matcher : typed_config.range_matchers()) {
      match_children.push_back(*on_match_factory.createOnMatch(range_matcher.on_match()));
    }
    std::vector<std::pair<TrieNode<DataType>, std::vector<Network::Address::CidrRange>>> data;
    data.reserve(match_children.size());
    size_t i = 0;
    // Ranges might have variable prefix length so we cannot combine them into one node because
    // then the matched prefix length cannot be determined.
    for (const auto& range_matcher : typed_config.range_matchers()) {
      auto on_match = std::make_shared<OnMatch<DataType>>(match_children[i++]());
      for (const auto& range : range_matcher.ranges()) {
        TrieNode<DataType> node = {i, range.prefix_len().value(), range_matcher.exclusive(),
                                   on_match};
        data.push_back({node,
                        {THROW_OR_RETURN_VALUE(Network::Address::CidrRange::create(range),
                                               Network::Address::CidrRange)}});
      }
    }
    auto lc_trie = std::make_shared<Network::LcTrie::LcTrie<TrieNode<DataType>>>(data);
    return [data_input, lc_trie, on_no_match]() {
      return std::make_unique<TrieMatcher<DataType>>(
          data_input(), on_no_match ? absl::make_optional(on_no_match.value()()) : absl::nullopt,
          lc_trie);
    };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<xds::type::matcher::v3::IPMatcher>();
  }
  std::string name() const override { return "envoy.matching.custom_matchers.trie_matcher"; }
};

class NetworkTrieMatcherFactory : public TrieMatcherFactoryBase<Network::MatchingData> {};
class UdpNetworkTrieMatcherFactory : public TrieMatcherFactoryBase<Network::UdpMatchingData> {};
class HttpTrieMatcherFactory : public TrieMatcherFactoryBase<Http::HttpMatchingData> {};

} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
