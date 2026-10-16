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
#include "opentelemetry/proto/common/v1/common.pb.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

struct ActionContext {
  // The attributes of the sink-wide resource, which resource conversions may inherit. Null if
  // there are none.
  const Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue>* resource_attributes{
      nullptr};
};

class ConversionAction
    : public Matcher::ActionBase<
          envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction> {
public:
  // The resource conversion of the action, resolved when the config is loaded.
  struct ResourceConversion {
    // The attributes that the resource starts with, sorted by key: the inherited sink-wide
    // attributes overridden by the static ones.
    std::vector<std::pair<std::string, std::string>> base_attributes;
    // Maps the name of each tag to move to the resource to the keys of the attributes it sets.
    absl::flat_hash_map<std::string, absl::InlinedVector<std::string, 1>> tag_to_attribute_keys;
  };

  ConversionAction(
      const envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction& config,
      const ActionContext& context);

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
