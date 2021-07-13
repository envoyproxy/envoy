#pragma once

#include <functional>
#include <memory>
#include <variant>

#include "envoy/config/common/matcher/v3/matcher.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/matcher/matcher.h"

#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/matcher/exact_map_matcher.h"
#include "source/common/matcher/field_matcher.h"
#include "source/common/matcher/list_matcher.h"
#include "source/common/matcher/validation_visitor.h"
#include "source/common/matcher/value_input_matcher.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Matcher {

template <class ProtoType> class ActionBase : public Action {
public:
  ActionBase() : type_name_(ProtoType().GetTypeName()) {}

  absl::string_view typeUrl() const override { return type_name_; }

private:
  const std::string type_name_;
};

struct MaybeMatchResult {
  const ActionPtr result_;
  const MatchState match_state_;
};

// TODO(snowp): Make this a class that tracks the progress to speed up subsequent traversals.
template <class DataType>
static inline MaybeMatchResult evaluateMatch(MatchTree<DataType>& match_tree,
                                             const DataType& data) {
  const auto result = match_tree.match(data);
  if (result.match_state_ == MatchState::UnableToMatch) {
    return MaybeMatchResult{nullptr, MatchState::UnableToMatch};
  }

  if (!result.on_match_) {
    return {nullptr, MatchState::MatchComplete};
  }

  if (result.on_match_->matcher_) {
    return evaluateMatch(*result.on_match_->matcher_, data);
  }

  return MaybeMatchResult{result.on_match_->action_cb_(), MatchState::MatchComplete};
}

template <class DataType> using FieldMatcherFactoryCb = std::function<FieldMatcherPtr<DataType>()>;
template <class DataType>
using MatchTreeFactoryCb = std::function<std::unique_ptr<MatchTree<DataType>>()>;
template <class DataType> using OnMatchFactoryCb = std::function<OnMatch<DataType>()>;
template <class DataType> using DataInputFactoryCb = std::function<DataInputPtr<DataType>()>;

/**
 * Recursively constructs a MatchTree from a protobuf configuration.
 * @param DataType the type used as a source for DataInputs
 * @param ActionFactoryContext the context provided to Action factories
 */
template <class DataType, class ActionFactoryContext> class MatchTreeFactory {
public:
  MatchTreeFactory(ActionFactoryContext& context,
                   Server::Configuration::ServerFactoryContext& server_factory_context,
                   MatchTreeValidationVisitor<DataType>& validation_visitor)
      : action_factory_context_(context), server_factory_context_(server_factory_context),
        validation_visitor_(validation_visitor) {}

  MatchTreeFactoryCb<DataType> create(const envoy::config::common::matcher::v3::Matcher& config) {
    switch (config.matcher_type_case()) {
    case envoy::config::common::matcher::v3::Matcher::kMatcherTree:
      return createTreeMatcher(config);
    case envoy::config::common::matcher::v3::Matcher::kMatcherList:
      return createListMatcher(config);
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
      return nullptr;
    }
  }

private:
  MatchTreeFactoryCb<DataType>
  createListMatcher(const envoy::config::common::matcher::v3::Matcher& config) {
    std::vector<std::pair<FieldMatcherFactoryCb<DataType>, OnMatchFactoryCb<DataType>>>
        matcher_factories;
    matcher_factories.reserve(config.matcher_list().matchers().size());
    for (const auto& matcher : config.matcher_list().matchers()) {
      matcher_factories.push_back(std::make_pair(createFieldMatcher(matcher.predicate()),
                                                 *createOnMatch(matcher.on_match())));
    }

    auto on_no_match = createOnMatch(config.on_no_match());

    return [matcher_factories, on_no_match]() {
      auto list_matcher = std::make_unique<ListMatcher<DataType>>(
          on_no_match ? absl::make_optional((*on_no_match)()) : absl::nullopt);

      for (const auto& matcher : matcher_factories) {
        list_matcher->addMatcher(matcher.first(), matcher.second());
      }

      return list_matcher;
    };
  }

  template <class MatcherT>
  FieldMatcherFactoryCb<DataType> createAggregateFieldMatcherFactoryCb(
      const Protobuf::RepeatedPtrField<
          envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate>& predicates) {
    std::vector<FieldMatcherFactoryCb<DataType>> sub_matchers;
    for (const auto& predicate : predicates) {
      sub_matchers.emplace_back(createFieldMatcher(predicate));
    }

    return [sub_matchers]() {
      std::vector<FieldMatcherPtr<DataType>> matchers;
      matchers.reserve(sub_matchers.size());
      for (const auto& factory_cb : sub_matchers) {
        matchers.emplace_back(factory_cb());
      }

      return std::make_unique<MatcherT>(std::move(matchers));
    };
  }

  FieldMatcherFactoryCb<DataType> createFieldMatcher(
      const envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate& field_predicate) {
    switch (field_predicate.match_type_case()) {
    case (envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::kSinglePredicate): {
      auto data_input = createDataInput(field_predicate.single_predicate().input());
      auto input_matcher = createInputMatcher(field_predicate.single_predicate());

      return [data_input, input_matcher]() {
        return std::make_unique<SingleFieldMatcher<DataType>>(data_input(), input_matcher());
      };
    }
    case (envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::kOrMatcher):
      return createAggregateFieldMatcherFactoryCb<AnyFieldMatcher<DataType>>(
          field_predicate.or_matcher().predicate());
    case (envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::kAndMatcher):
      return createAggregateFieldMatcherFactoryCb<AllFieldMatcher<DataType>>(
          field_predicate.and_matcher().predicate());
    case (envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::kNotMatcher): {
      auto matcher_factory = createFieldMatcher(field_predicate.not_matcher());

      return [matcher_factory]() {
        return std::make_unique<NotFieldMatcher<DataType>>(matcher_factory());
      };
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  MatchTreeFactoryCb<DataType>
  createTreeMatcher(const envoy::config::common::matcher::v3::Matcher& matcher) {
    switch (matcher.matcher_tree().tree_type_case()) {
    case envoy::config::common::matcher::v3::Matcher_MatcherTree::kExactMatchMap: {
      std::vector<std::pair<std::string, OnMatchFactoryCb<DataType>>> match_children;
      match_children.reserve(matcher.matcher_tree().exact_match_map().map().size());

      for (const auto& children : matcher.matcher_tree().exact_match_map().map()) {
        match_children.push_back(
            std::make_pair(children.first, *MatchTreeFactory::createOnMatch(children.second)));
      }

      auto data_input = createDataInput(matcher.matcher_tree().input());
      auto on_no_match = createOnMatch(matcher.on_no_match());

      return [match_children, data_input, on_no_match]() {
        auto multimap_matcher = std::make_unique<ExactMapMatcher<DataType>>(
            data_input(), on_no_match ? absl::make_optional((*on_no_match)()) : absl::nullopt);
        for (const auto& children : match_children) {
          multimap_matcher->addChild(children.first, children.second());
        }
        return multimap_matcher;
      };
    }
    case envoy::config::common::matcher::v3::Matcher_MatcherTree::kPrefixMatchMap:
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    case envoy::config::common::matcher::v3::Matcher_MatcherTree::kCustomMatch:
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
  absl::optional<OnMatchFactoryCb<DataType>>
  createOnMatch(const envoy::config::common::matcher::v3::Matcher::OnMatch& on_match) {
    if (on_match.has_matcher()) {
      return [matcher_factory = create(on_match.matcher())]() {
        return OnMatch<DataType>{{}, matcher_factory()};
      };
    } else if (on_match.has_action()) {
      auto& factory = Config::Utility::getAndCheckFactory<ActionFactory<ActionFactoryContext>>(
          on_match.action());
      ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
          on_match.action().typed_config(), server_factory_context_.messageValidationVisitor(),
          factory);

      auto action_factory = factory.createActionFactoryCb(
          *message, action_factory_context_, server_factory_context_.messageValidationVisitor());
      return [action_factory] { return OnMatch<DataType>{action_factory, {}}; };
    }

    return absl::nullopt;
  }

  // Wrapper around a CommonProtocolInput that allows it to be used as a DataInput<DataType>.
  class CommonProtocolInputWrapper : public DataInput<DataType> {
  public:
    explicit CommonProtocolInputWrapper(CommonProtocolInputPtr&& common_protocol_input)
        : common_protocol_input_(std::move(common_protocol_input)) {}

    DataInputGetResult get(const DataType&) const override {
      return DataInputGetResult{DataInputGetResult::DataAvailability::AllDataAvailable,
                                common_protocol_input_->get()};
    }

  private:
    const CommonProtocolInputPtr common_protocol_input_;
  };

  DataInputFactoryCb<DataType>
  createDataInput(const envoy::config::core::v3::TypedExtensionConfig& config) {
    auto* factory = Config::Utility::getFactory<DataInputFactory<DataType>>(config);
    if (factory != nullptr) {
      validation_visitor_.validateDataInput(*factory, config.typed_config().type_url());

      ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
          config.typed_config(), server_factory_context_.messageValidationVisitor(), *factory);
      auto data_input = factory->createDataInputFactoryCb(
          *message, server_factory_context_.messageValidationVisitor());
      return data_input;
    }

    // If the provided config doesn't match a typed input, assume that this is one of the common
    // inputs.
    auto& common_input_factory =
        Config::Utility::getAndCheckFactory<CommonProtocolInputFactory>(config);
    ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
        config.typed_config(), server_factory_context_.messageValidationVisitor(),
        common_input_factory);
    auto common_input = common_input_factory.createCommonProtocolInputFactoryCb(
        *message, server_factory_context_.messageValidationVisitor());
    return
        [common_input]() { return std::make_unique<CommonProtocolInputWrapper>(common_input()); };
  }

  InputMatcherFactoryCb createInputMatcher(
      const envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::SinglePredicate&
          predicate) {
    switch (predicate.matcher_case()) {
    case envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::SinglePredicate::
        kValueMatch:
      return [value_match = predicate.value_match()]() {
        return std::make_unique<StringInputMatcher>(value_match);
      };
    case envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::SinglePredicate::
        kCustomMatch: {
      auto& factory =
          Config::Utility::getAndCheckFactory<InputMatcherFactory>(predicate.custom_match());
      ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
          predicate.custom_match().typed_config(),
          server_factory_context_.messageValidationVisitor(), factory);
      return factory.createInputMatcherFactoryCb(*message, server_factory_context_);
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  const std::string stats_prefix_;
  ActionFactoryContext& action_factory_context_;
  Server::Configuration::ServerFactoryContext& server_factory_context_;
  MatchTreeValidationVisitor<DataType>& validation_visitor_;
};
} // namespace Matcher
} // namespace Envoy
