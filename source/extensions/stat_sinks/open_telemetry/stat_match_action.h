#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/stat_sinks/open_telemetry/v3/open_telemetry.pb.h"
#include "envoy/extensions/stat_sinks/open_telemetry/v3/open_telemetry.pb.validate.h"
#include "envoy/stats/stats.h"

#include "source/common/matcher/matcher.h"
#include "source/common/matcher/validation_visitor.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

struct ActionContext {
  // The first error found in the config of an action.
  absl::Status creation_status;
};

class ConversionAction
    : public Matcher::ActionBase<
          envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction> {
public:
  // The resource conversion of the action, resolved when the config is loaded.
  struct ResourceConversion {
    // A stat tag that sets resource attributes.
    struct Tag {
      std::string name;
      // The keys of the resource attributes that the tag sets.
      absl::InlinedVector<std::string, 1> attribute_keys;
    };

    // The static attributes of the resource.
    std::vector<std::pair<std::string, std::string>> static_attributes;
    // The tags that set the other attributes of the resource, in the order in which they are first
    // listed. A stat is only reported under the resource if it has all of them, with non-empty
    // values.
    std::vector<Tag> tags;
    // Maps the name of each tag in `tags` to its index.
    absl::flat_hash_map<std::string, uint32_t> tag_indexes;
  };

  /**
   * @param config supplies the config of the action.
   * @param context supplies the context of the action, whose creation status is set if the config
   *        is invalid.
   */
  ConversionAction(
      const envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction& config,
      ActionContext& context);

  const envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction*
  config() const {
    return &config_;
  }

  // Returns the resource conversion of the action, or null if its metric is reported under the
  // sink-wide resource.
  const ResourceConversion* resourceConversion() const {
    return resource_conversion_.has_value() ? &*resource_conversion_ : nullptr;
  }

private:
  const envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction config_;
  std::optional<ResourceConversion> resource_conversion_;
};

class DropAction : public Matcher::ActionBase<
                       envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::DropAction> {
public:
  explicit DropAction(
      const envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::DropAction&) {}
};

class ActionValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Stats::StatMatchingData> {
public:
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<Stats::StatMatchingData>&,
                                          absl::string_view) override {
    return absl::OkStatus();
  }
};

class ConversionActionFactory : public Matcher::ActionFactory<ActionContext> {
public:
  Matcher::ActionConstSharedPtr
  createAction(const Protobuf::Message& config, ActionContext& context,
               ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& action_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction&>(
        config, validation_visitor);
    return std::make_shared<ConversionAction>(action_config, context);
  }

  std::string name() const override { return "otlp_metric_conversion_action_factory"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction>();
  }
};

class DropActionFactory : public Matcher::ActionFactory<ActionContext> {
public:
  Matcher::ActionConstSharedPtr
  createAction(const Protobuf::Message& config, ActionContext&,
               ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& action_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::DropAction&>(
        config, validation_visitor);
    return std::make_shared<DropAction>(action_config);
  }

  std::string name() const override { return "otlp_metric_drop_action_factory"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::DropAction>();
  }
};
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
