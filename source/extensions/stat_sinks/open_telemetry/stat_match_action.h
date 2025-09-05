#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/stat_sinks/open_telemetry/v3/open_telemetry.pb.h"
#include "envoy/extensions/stat_sinks/open_telemetry/v3/open_telemetry.pb.validate.h"
#include "envoy/stats/stats.h"

#include "source/common/matcher/matcher.h"
#include "source/common/matcher/validation_visitor.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

struct ActionContext {};

class OnMatchAction
    : public Matcher::ActionBase<
          envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction> {
public:
  explicit OnMatchAction(
      const envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction& config)
      : config_(config) {}

  const envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction*
  config() const {
    return &config_;
  }

private:
  const envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction config_;
};

class ActionValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Stats::StatMatchingData> {
public:
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<Stats::StatMatchingData>&,
                                          absl::string_view) override {
    return absl::OkStatus();
  }
};

class ActionFactory : public Matcher::ActionFactory<ActionContext> {
public:
  Matcher::ActionConstSharedPtr
  createAction(const Protobuf::Message& config, ActionContext&,
               ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& action_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction&>(
        config, validation_visitor);
    return std::make_shared<OnMatchAction>(action_config);
  }

  std::string name() const override { return "otlp_metric_conversion_action_factory"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction>();
  }
};
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
