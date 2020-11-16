#pragma once

#include <memory>
#include <variant>

#include "envoy/config/common/matcher/v3/matcher.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/matcher/matcher.h"

#include "common/common/assert.h"
#include "common/config/utility.h"
#include "common/matcher/exact_map_matcher.h"
#include "common/matcher/field_matcher.h"
#include "common/matcher/list_matcher.h"
#include "common/matcher/value_input_matcher.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "external/envoy_api/envoy/config/core/v3/extension.pb.h"

namespace Envoy {
namespace Matcher {

struct MaybeMatchResult {
  const TypedExtensionConfigOpt result_;
  const bool final_;
};

// TODO(snowp): Make this a class that tracks the progress to speed up subsequent traversals.
template <class DataType>
static inline MaybeMatchResult evaluateMatch(MatchTree<DataType>& match_tree,
                                             const DataType& data) {
  const auto result = match_tree.match(data);
  if (!result.match_completed_) {
    return MaybeMatchResult{absl::nullopt, false};
  }

  if (!result.on_match_) {
    return {{}, true};
  }

  if (result.on_match_->matcher_) {
    return evaluateMatch(*result.on_match_->matcher_, data);
  }

  return MaybeMatchResult{*result.on_match_->action_, true};
}

/**
 * Recursively constructs a MatchTree from a protobuf configuration.
 */
template <class DataType> class MatchTreeFactory {
public:
  MatchTreeFactory(ProtobufMessage::ValidationVisitor& validation_visitor)
      : validation_visitor_(validation_visitor) {}

  MatchTreeSharedPtr<DataType> create(const envoy::config::common::matcher::v3::Matcher& config) {
    if (config.has_matcher_tree()) {
      return createTreeMatcher(config);
    } else if (config.has_matcher_list()) {
      return createListMatcher(config);
    } else {
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

private:
  MatchTreeSharedPtr<DataType>
  createListMatcher(const envoy::config::common::matcher::v3::Matcher& config) {
    auto list_matcher =
        std::make_shared<ListMatcher<DataType>>(createOnMatch(config.on_no_match()));

    for (const auto& matcher : config.matcher_list().matchers()) {
      list_matcher->addMatcher(createFieldMatcher(matcher.predicate()),
                               *createOnMatch(matcher.on_match()));
    }

    return list_matcher;
  }

  FieldMatcherPtr<DataType> createFieldMatcher(
      const envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate& field_predicate) {
    switch (field_predicate.match_type_case()) {
    case (envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::kSinglePredicate):

      return std::make_unique<SingleFieldMatcher<DataType>>(
          createDataInput(field_predicate.single_predicate().input()),
          createInputMatcher(field_predicate.single_predicate()));
    case (envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::kOrMatcher): {
      std::vector<FieldMatcherPtr<DataType>> sub_matchers;
      for (const auto& predicate : field_predicate.or_matcher().predicate()) {
        sub_matchers.emplace_back(createFieldMatcher(predicate));
      }

      return std::make_unique<AnyFieldMatcher<DataType>>(std::move(sub_matchers));
    }
    case (envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::kAndMatcher): {
      std::vector<FieldMatcherPtr<DataType>> sub_matchers;
      for (const auto& predicate : field_predicate.and_matcher().predicate()) {
        sub_matchers.emplace_back(createFieldMatcher(predicate));
      }

      return std::make_unique<AllFieldMatcher<DataType>>(std::move(sub_matchers));
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  MatchTreeSharedPtr<DataType>
  createTreeMatcher(const envoy::config::common::matcher::v3::Matcher& matcher) {
    auto multimap_matcher = std::make_shared<ExactMapMatcher<DataType>>(
        createDataInput(matcher.matcher_tree().input()), createOnMatch(matcher.on_no_match()));

    for (const auto& children : matcher.matcher_tree().exact_match_map().map()) {
      multimap_matcher->addChild(children.first, *MatchTreeFactory::createOnMatch(children.second));
    }

    return multimap_matcher;
  }
  absl::optional<OnMatch<DataType>>
  createOnMatch(const envoy::config::common::matcher::v3::Matcher::OnMatch& on_match) {
    if (on_match.has_matcher()) {
      return OnMatch<DataType>{{}, create(on_match.matcher())};
    } else if (on_match.has_action()) {
      return OnMatch<DataType>{on_match.action(), {}};
    }

    return absl::nullopt;
  }

  DataInputPtr<DataType>
  createDataInput(const envoy::config::core::v3::TypedExtensionConfig& config) {
    auto& factory = Config::Utility::getAndCheckFactory<DataInputFactory<DataType>>(config);
    ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
        config.typed_config(), validation_visitor_, factory);
    return factory.createDataInput(*message);
  }

  InputMatcherPtr createInputMatcher(
      const envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::SinglePredicate&
          predicate) {
    switch (predicate.matcher_case()) {
    case envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::SinglePredicate::
        kValueMatch:
      return std::make_unique<ValueInputMatcher>(predicate.value_match());
    case envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::SinglePredicate::
        kCustomMatch: {
      auto& factory =
          Config::Utility::getAndCheckFactory<InputMatcherFactory>(predicate.custom_match());
      ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
          predicate.custom_match().typed_config(), validation_visitor_, factory);
      return factory.createInputMatcher(*message);
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  ProtobufMessage::ValidationVisitor& validation_visitor_;
};
} // namespace Matcher
} // namespace Envoy
