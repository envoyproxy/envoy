#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

#include "source/common/matcher/matcher.h"
#include "source/common/network/interval_tree.h"

#include "xds/type/matcher/v3/range.pb.h"
#include "xds/type/matcher/v3/range.pb.validate.h"

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

/**
 * Implementation of a `sublinear` port range matcher.
 */
template <class DataType> class RangeMatcher : public MatchTree<DataType> {
public:
  RangeMatcher(
      DataInputPtr<DataType>&& data_input, absl::optional<OnMatch<DataType>> on_no_match,
      const std::shared_ptr<Network::IntervalTree::IntervalTree<OnMatch<DataType>, int32_t>>& tree)
      : data_input_(std::move(data_input)), on_no_match_(std::move(on_no_match)), tree_(tree) {}

  typename MatchTree<DataType>::MatchResult match(const DataType& data) override {
    const auto input = data_input_->get(data);
    if (input.data_availability_ != DataInputGetResult::DataAvailability::AllDataAvailable) {
      return {MatchState::UnableToMatch, absl::nullopt};
    }
    if (!input.data_) {
      return {MatchState::MatchComplete, on_no_match_};
    }
    int32_t port;
    if (!absl::SimpleAtoi(*input.data_, &port)) {
      return {MatchState::MatchComplete, on_no_match_};
    }
    auto values = tree_->getData(port);
    for (const auto on_match : values) {
      if (on_match.action_cb_) {
        return {MatchState::MatchComplete, OnMatch<DataType>{on_match.action_cb_, nullptr}};
      }
      auto matched = evaluateMatch(*on_match.matcher_, data);
      if (matched.match_state_ == MatchState::UnableToMatch) {
        return {MatchState::UnableToMatch, absl::nullopt};
      }
      if (matched.match_state_ == MatchState::MatchComplete && matched.result_) {
        return {MatchState::MatchComplete, OnMatch<DataType>{matched.result_, nullptr}};
      }
    }
    return {MatchState::MatchComplete, on_no_match_};
  }

private:
  const DataInputPtr<DataType> data_input_;
  const absl::optional<OnMatch<DataType>> on_no_match_;
  std::shared_ptr<Network::IntervalTree::IntervalTree<OnMatch<DataType>, int32_t>> tree_;
};

template <class DataType>
class RangeMatcherFactoryBase : public ::Envoy::Matcher::CustomMatcherFactory<DataType> {
public:
  ::Envoy::Matcher::MatchTreeFactoryCb<DataType>
  createCustomMatcherFactoryCb(const Protobuf::Message& config,
                               Server::Configuration::ServerFactoryContext& factory_context,
                               DataInputFactoryCb<DataType> data_input,
                               absl::optional<OnMatchFactoryCb<DataType>> on_no_match,
                               OnMatchFactory<DataType>& on_match_factory) override {
    const auto& typed_config =
        MessageUtil::downcastAndValidate<const xds::type::matcher::v3::Int32RangeMatcher&>(
            config, factory_context.messageValidationVisitor());
    std::vector<std::tuple<OnMatch<DataType>, int32_t, int32_t>> data;
    data.reserve(typed_config.range_matchers().size());
    for (const auto& range_matcher : typed_config.range_matchers()) {
      auto on_match = on_match_factory.createOnMatch(range_matcher.on_match()).value()();
      for (const auto& range : range_matcher.ranges()) {
        data.emplace_back(on_match, range.start(), range.end());
      }
    }
    auto tree =
        std::make_shared<Network::IntervalTree::IntervalTree<OnMatch<DataType>, int32_t>>(data);
    return [data_input, tree, on_no_match]() {
      return std::make_unique<RangeMatcher<DataType>>(
          data_input(), on_no_match ? absl::make_optional(on_no_match.value()()) : absl::nullopt,
          tree);
    };
  };
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<xds::type::matcher::v3::Int32RangeMatcher>();
  }
  std::string name() const override { return "range-matcher"; }
};

} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
