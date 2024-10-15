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
#include "source/common/matcher/prefix_map_matcher.h"
#include "source/common/matcher/validation_visitor.h"
#include "source/common/matcher/value_input_matcher.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Matcher {

template <class ProtoType, class Base = Action> class ActionBase : public Base {
public:
  template <typename... Args> ActionBase(Args... args) : Base(args...) {}

  absl::string_view typeUrl() const override { return staticTypeUrl(); }

  static absl::string_view staticTypeUrl() {
    const static std::string typeUrl = ProtoType().GetTypeName();

    return typeUrl;
  }
};

struct MaybeMatchResult {
  const ActionFactoryCb result_;
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

  return MaybeMatchResult{result.on_match_->action_cb_, MatchState::MatchComplete};
}

template <class DataType> using FieldMatcherFactoryCb = std::function<FieldMatcherPtr<DataType>()>;

/**
 * A matcher that will always resolve to associated on_no_match. This is used when
 * the matcher is configured without a matcher, allowing for a tree that always resolves
 * to a specific OnMatch.
 */
template <class DataType> class AnyMatcher : public MatchTree<DataType> {
public:
  explicit AnyMatcher(absl::optional<OnMatch<DataType>> on_no_match)
      : on_no_match_(std::move(on_no_match)) {}

  typename MatchTree<DataType>::MatchResult match(const DataType&) override {
    return {MatchState::MatchComplete, on_no_match_};
  }
  const absl::optional<OnMatch<DataType>> on_no_match_;
};

/**
 * Constructs a data input function for a data type.
 **/
template <class DataType> class MatchInputFactory {
public:
  MatchInputFactory(ProtobufMessage::ValidationVisitor& validator,
                    MatchTreeValidationVisitor<DataType>& validation_visitor)
      : validator_(validator), validation_visitor_(validation_visitor) {}

  DataInputFactoryCb<DataType> createDataInput(const xds::core::v3::TypedExtensionConfig& config) {
    return createDataInputBase(config);
  }

  DataInputFactoryCb<DataType>
  createDataInput(const envoy::config::core::v3::TypedExtensionConfig& config) {
    return createDataInputBase(config);
  }

private:
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

  template <class TypedExtensionConfigType>
  DataInputFactoryCb<DataType> createDataInputBase(const TypedExtensionConfigType& config) {
    auto* factory = Config::Utility::getFactory<DataInputFactory<DataType>>(config);
    if (factory != nullptr) {
      validation_visitor_.validateDataInput(*factory, config.typed_config().type_url());

      ProtobufTypes::MessagePtr message =
          Config::Utility::translateAnyToFactoryConfig(config.typed_config(), validator_, *factory);
      auto data_input = factory->createDataInputFactoryCb(*message, validator_);
      return data_input;
    }

    // If the provided config doesn't match a typed input, assume that this is one of the common
    // inputs.
    auto& common_input_factory =
        Config::Utility::getAndCheckFactory<CommonProtocolInputFactory>(config);
    ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
        config.typed_config(), validator_, common_input_factory);
    auto common_input =
        common_input_factory.createCommonProtocolInputFactoryCb(*message, validator_);
    return
        [common_input]() { return std::make_unique<CommonProtocolInputWrapper>(common_input()); };
  }

  ProtobufMessage::ValidationVisitor& validator_;
  MatchTreeValidationVisitor<DataType>& validation_visitor_;
};

/**
 * Recursively constructs a MatchTree from a protobuf configuration.
 * @param DataType the type used as a source for DataInputs
 * @param ActionFactoryContext the context provided to Action factories
 */
template <class DataType, class ActionFactoryContext>
class MatchTreeFactory : public OnMatchFactory<DataType> {
public:
  MatchTreeFactory(ActionFactoryContext& context,
                   Server::Configuration::ServerFactoryContext& factory_context,
                   MatchTreeValidationVisitor<DataType>& validation_visitor)
      : action_factory_context_(context), server_factory_context_(factory_context),
        match_input_factory_(factory_context.messageValidationVisitor(), validation_visitor) {}

  // TODO(snowp): Remove this type parameter once we only have one Matcher proto.
  template <class MatcherType> MatchTreeFactoryCb<DataType> create(const MatcherType& config) {
    switch (config.matcher_type_case()) {
    case MatcherType::kMatcherTree:
      return createTreeMatcher(config);
    case MatcherType::kMatcherList:
      return createListMatcher(config);
    case MatcherType::MATCHER_TYPE_NOT_SET:
      return createAnyMatcher(config);
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  absl::optional<OnMatchFactoryCb<DataType>>
  createOnMatch(const xds::type::matcher::v3::Matcher::OnMatch& on_match) override {
    return createOnMatchBase(on_match);
  }

  absl::optional<OnMatchFactoryCb<DataType>>
  createOnMatch(const envoy::config::common::matcher::v3::Matcher::OnMatch& on_match) override {
    return createOnMatchBase(on_match);
  }

private:
  template <class MatcherType>
  MatchTreeFactoryCb<DataType> createAnyMatcher(const MatcherType& config) {
    auto on_no_match = createOnMatch(config.on_no_match());

    return [on_no_match]() {
      return std::make_unique<AnyMatcher<DataType>>(
          on_no_match ? absl::make_optional((*on_no_match)()) : absl::nullopt);
    };
  }
  template <class MatcherType>
  MatchTreeFactoryCb<DataType> createListMatcher(const MatcherType& config) {
    std::vector<std::pair<FieldMatcherFactoryCb<DataType>, OnMatchFactoryCb<DataType>>>
        matcher_factories;
    matcher_factories.reserve(config.matcher_list().matchers().size());
    for (const auto& matcher : config.matcher_list().matchers()) {
      matcher_factories.push_back(std::make_pair(
          createFieldMatcher<typename MatcherType::MatcherList::Predicate>(matcher.predicate()),
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

  template <class MatcherT, class PredicateType, class FieldPredicateType>
  FieldMatcherFactoryCb<DataType> createAggregateFieldMatcherFactoryCb(
      const Protobuf::RepeatedPtrField<FieldPredicateType>& predicates) {
    std::vector<FieldMatcherFactoryCb<DataType>> sub_matchers;
    for (const auto& predicate : predicates) {
      sub_matchers.emplace_back(createFieldMatcher<PredicateType>(predicate));
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

  template <class PredicateType, class FieldMatcherType>
  FieldMatcherFactoryCb<DataType> createFieldMatcher(const FieldMatcherType& field_predicate) {
    switch (field_predicate.match_type_case()) {
    case (PredicateType::kSinglePredicate): {
      auto data_input =
          match_input_factory_.createDataInput(field_predicate.single_predicate().input());
      auto input_matcher = createInputMatcher(field_predicate.single_predicate());

      return [data_input, input_matcher]() {
        return THROW_OR_RETURN_VALUE(
            SingleFieldMatcher<DataType>::create(data_input(), input_matcher()),
            std::unique_ptr<SingleFieldMatcher<DataType>>);
      };
    }
    case (PredicateType::kOrMatcher):
      return createAggregateFieldMatcherFactoryCb<AnyFieldMatcher<DataType>, PredicateType>(
          field_predicate.or_matcher().predicate());
    case (PredicateType::kAndMatcher):
      return createAggregateFieldMatcherFactoryCb<AllFieldMatcher<DataType>, PredicateType>(
          field_predicate.and_matcher().predicate());
    case (PredicateType::kNotMatcher): {
      auto matcher_factory = createFieldMatcher<PredicateType>(field_predicate.not_matcher());

      return [matcher_factory]() {
        return std::make_unique<NotFieldMatcher<DataType>>(matcher_factory());
      };
    }
    case PredicateType::MATCH_TYPE_NOT_SET:
      PANIC_DUE_TO_PROTO_UNSET;
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  template <class MatcherType>
  MatchTreeFactoryCb<DataType> createTreeMatcher(const MatcherType& matcher) {
    auto data_input = match_input_factory_.createDataInput(matcher.matcher_tree().input());
    auto on_no_match = createOnMatch(matcher.on_no_match());

    switch (matcher.matcher_tree().tree_type_case()) {
    case MatcherType::MatcherTree::kExactMatchMap: {
      return createMapMatcher<ExactMapMatcher>(matcher.matcher_tree().exact_match_map(), data_input,
                                               on_no_match, &ExactMapMatcher<DataType>::create);
    }
    case MatcherType::MatcherTree::kPrefixMatchMap: {
      return createMapMatcher<PrefixMapMatcher>(matcher.matcher_tree().prefix_match_map(),
                                                data_input, on_no_match,
                                                &PrefixMapMatcher<DataType>::create);
    }
    case MatcherType::MatcherTree::TREE_TYPE_NOT_SET:
      PANIC("unexpected matcher type");
    case MatcherType::MatcherTree::kCustomMatch: {
      auto& factory = Config::Utility::getAndCheckFactory<CustomMatcherFactory<DataType>>(
          matcher.matcher_tree().custom_match());
      ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
          matcher.matcher_tree().custom_match().typed_config(),
          server_factory_context_.messageValidationVisitor(), factory);
      return factory.createCustomMatcherFactoryCb(*message, server_factory_context_, data_input,
                                                  on_no_match, *this);
    }
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  using MapCreationFunction = std::function<absl::StatusOr<std::unique_ptr<MapMatcher<DataType>>>(
      DataInputPtr<DataType>&& data_input, absl::optional<OnMatch<DataType>> on_no_match)>;

  template <template <class> class MapMatcherType, class MapType>
  MatchTreeFactoryCb<DataType>
  createMapMatcher(const MapType& map, DataInputFactoryCb<DataType> data_input,
                   absl::optional<OnMatchFactoryCb<DataType>>& on_no_match,
                   MapCreationFunction creation_function) {
    std::vector<std::pair<std::string, OnMatchFactoryCb<DataType>>> match_children;
    match_children.reserve(map.map().size());

    for (const auto& children : map.map()) {
      match_children.push_back(
          std::make_pair(children.first, *MatchTreeFactory::createOnMatch(children.second)));
    }

    return [match_children, data_input, on_no_match, creation_function]() {
      auto matcher_or_error = creation_function(
          data_input(), on_no_match ? absl::make_optional((*on_no_match)()) : absl::nullopt);
      THROW_IF_NOT_OK(matcher_or_error.status());
      auto multimap_matcher = std::move(*matcher_or_error);
      for (const auto& children : match_children) {
        multimap_matcher->addChild(children.first, children.second());
      }
      return multimap_matcher;
    };
  }

  template <class OnMatchType>
  absl::optional<OnMatchFactoryCb<DataType>> createOnMatchBase(const OnMatchType& on_match) {
    if (on_match.has_matcher()) {
      return [matcher_factory = std::move(create(on_match.matcher()))]() {
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

  template <class SinglePredicateType>
  InputMatcherFactoryCb createInputMatcher(const SinglePredicateType& predicate) {
    switch (predicate.matcher_case()) {
    case SinglePredicateType::kValueMatch:
      return [&context = server_factory_context_, value_match = predicate.value_match()]() {
        return std::make_unique<StringInputMatcher<std::decay_t<decltype(value_match)>>>(
            value_match, context);
      };
    case SinglePredicateType::kCustomMatch: {
      auto& factory =
          Config::Utility::getAndCheckFactory<InputMatcherFactory>(predicate.custom_match());
      ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
          predicate.custom_match().typed_config(),
          server_factory_context_.messageValidationVisitor(), factory);
      return factory.createInputMatcherFactoryCb(*message, server_factory_context_);
    }
    case SinglePredicateType::MATCHER_NOT_SET:
      PANIC_DUE_TO_PROTO_UNSET;
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  const std::string stats_prefix_;
  ActionFactoryContext& action_factory_context_;
  Server::Configuration::ServerFactoryContext& server_factory_context_;
  MatchInputFactory<DataType> match_input_factory_;
};
} // namespace Matcher
} // namespace Envoy
