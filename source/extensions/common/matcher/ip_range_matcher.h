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
using ::Envoy::Matcher::MatchResult;
using ::Envoy::Matcher::MatchTree;
using ::Envoy::Matcher::OnMatch;
using ::Envoy::Matcher::OnMatchFactory;
using ::Envoy::Matcher::OnMatchFactoryCb;
using ::Envoy::Matcher::SkippedMatchCb;

template <class DataType> struct IpRangeNode {
  size_t index_;
  uint32_t prefix_len_;
  bool exclusive_;
  std::shared_ptr<OnMatch<DataType>> on_match_;

  friend bool operator==(const IpRangeNode<DataType>& lhs, const IpRangeNode<DataType>& rhs) {
    return lhs.index_ == rhs.index_ && lhs.prefix_len_ == rhs.prefix_len_ &&
           lhs.exclusive_ == rhs.exclusive_ && lhs.on_match_ == rhs.on_match_;
  }
  template <typename H>
  friend H AbslHashValue(H h, // NOLINT(readability-identifier-naming)
                         const IpRangeNode<DataType>& node) {
    return H::combine(std::move(h), node.index_, node.prefix_len_, node.exclusive_, node.on_match_);
  }
};

template <class DataType> struct IpRangeNodeComparator {
  inline bool operator()(const IpRangeNode<DataType>& lhs, const IpRangeNode<DataType>& rhs) const {
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
 * Implementation of a `sublinear` LC-trie matcher for IP ranges.
 */
template <class DataType> class IpRangeMatcher : public MatchTree<DataType> {
public:
  IpRangeMatcher(DataInputPtr<DataType>&& data_input, absl::optional<OnMatch<DataType>> on_no_match,
                 const std::shared_ptr<Network::LcTrie::LcTrie<IpRangeNode<DataType>>>& trie)
      : data_input_(std::move(data_input)), on_no_match_(std::move(on_no_match)), trie_(trie) {
    auto input_type = data_input_->dataInputType();
    if (input_type != Envoy::Matcher::DefaultMatchingDataType) {
      throw EnvoyException(
          absl::StrCat("Unsupported data input type: ", input_type,
                       ", currently only string type is supported in IP range matcher"));
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
    const Network::Address::InstanceConstSharedPtr addr =
        Network::Utility::parseInternetAddressNoThrow(absl::get<std::string>(input.data_));
    if (!addr) {
      return MatchTree<DataType>::handleRecursionAndSkips(on_no_match_, data, skipped_match_cb);
    }
    auto values = trie_->getData(addr);
    // The candidates returned by the LC trie are not in any specific order, so we
    // sort them by the prefix length first (longest first), and the order of declaration second.
    std::sort(values.begin(), values.end(), IpRangeNodeComparator<DataType>());
    bool first = true;
    for (const auto& node : values) {
      if (!first && node.exclusive_) {
        continue;
      }
      // handleRecursionAndSkips should only return match-failure, no-match, or an action cb.
      MatchResult processed_match =
          MatchTree<DataType>::handleRecursionAndSkips(*node.on_match_, data, skipped_match_cb);

      if (processed_match.isMatch() || processed_match.isInsufficientData()) {
        return processed_match;
      }
      // No-match isn't definitive, so continue checking nodes.
      if (first) {
        first = false;
      }
    }
    return MatchTree<DataType>::handleRecursionAndSkips(on_no_match_, data, skipped_match_cb);
  }

private:
  const DataInputPtr<DataType> data_input_;
  const absl::optional<OnMatch<DataType>> on_no_match_;
  std::shared_ptr<Network::LcTrie::LcTrie<IpRangeNode<DataType>>> trie_;
};

template <class DataType>
class IpRangeMatcherFactoryBase : public ::Envoy::Matcher::CustomMatcherFactory<DataType> {
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
    std::vector<std::pair<IpRangeNode<DataType>, std::vector<Network::Address::CidrRange>>> data;
    data.reserve(match_children.size());
    size_t i = 0;
    // Ranges might have variable prefix length so we cannot combine them into one node because
    // then the matched prefix length cannot be determined.
    for (const auto& range_matcher : typed_config.range_matchers()) {
      auto on_match = std::make_shared<OnMatch<DataType>>(match_children[i++]());
      for (const auto& range : range_matcher.ranges()) {
        IpRangeNode<DataType> node = {i, range.prefix_len().value(), range_matcher.exclusive(),
                                      on_match};
        data.push_back({node,
                        {THROW_OR_RETURN_VALUE(Network::Address::CidrRange::create(range),
                                               Network::Address::CidrRange)}});
      }
    }
    auto lc_trie = std::make_shared<Network::LcTrie::LcTrie<IpRangeNode<DataType>>>(data);
    return [data_input, lc_trie, on_no_match]() {
      return std::make_unique<IpRangeMatcher<DataType>>(
          data_input(), on_no_match ? absl::make_optional(on_no_match.value()()) : absl::nullopt,
          lc_trie);
    };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<xds::type::matcher::v3::IPMatcher>();
  }
  std::string name() const override { return "envoy.matching.custom_matchers.ip_range_matcher"; }
};

class NetworkIpRangeMatcherFactory : public IpRangeMatcherFactoryBase<Network::MatchingData> {};
class UdpNetworkIpRangeMatcherFactory : public IpRangeMatcherFactoryBase<Network::UdpMatchingData> {
};
class HttpIpRangeMatcherFactory : public IpRangeMatcherFactoryBase<Http::HttpMatchingData> {};

} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
