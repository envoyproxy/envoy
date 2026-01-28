#pragma once

#include "envoy/extensions/matching/common_inputs/stats/v3/stats.pb.h"
#include "envoy/extensions/matching/common_inputs/stats/v3/stats.pb.validate.h"
#include "envoy/stats/stats.h"

#include "source/common/protobuf/utility.h"
#include "source/common/stats/stats_matcher_impl.h"

namespace Envoy {
namespace Stats {

class StatFullNameMatchInput : public Matcher::DataInput<Envoy::Stats::StatMatchingData> {
public:
  Matcher::DataInputGetResult get(const Envoy::Stats::StatMatchingData& data) const override {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, data.fullName()};
  }
};

class StatFullNameMatchInputFactory
    : public Matcher::DataInputFactory<Envoy::Stats::StatMatchingData> {
public:
  std::string name() const override { return "stat_full_name_match_input"; }

  Envoy::Matcher::DataInputFactoryCb<Envoy::Stats::StatMatchingData>
  createDataInputFactoryCb(const Envoy::Protobuf::Message&,
                           Envoy::ProtobufMessage::ValidationVisitor&) override {
    return [] { return std::make_unique<StatFullNameMatchInput>(); };
  }

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::common_inputs::stats::v3::StatFullNameMatchInput>();
  }
};

class StatTagValueInput : public Matcher::DataInput<Envoy::Stats::StatMatchingData> {
public:
  StatTagValueInput(const std::string& name) : name_(name) {}

  Matcher::DataInputGetResult get(const Envoy::Stats::StatMatchingData& data) const override {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, data.tagValue(name_)};
  }

private:
  const std::string name_;
};

class StatTagValueInputFactory : public Matcher::DataInputFactory<Envoy::Stats::StatMatchingData> {
public:
  std::string name() const override { return "stat_tag_value_input"; }

  Envoy::Matcher::DataInputFactoryCb<Envoy::Stats::StatMatchingData>
  createDataInputFactoryCb(const Envoy::Protobuf::Message& config,
                           Envoy::ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& input_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::matching::common_inputs::stats::v3::StatTagValueInput&>(
        config, validation_visitor);
    return [name = input_config.tag_name()] { return std::make_unique<StatTagValueInput>(name); };
  }

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::common_inputs::stats::v3::StatTagValueInput>();
  }
};

} // namespace Stats
} // namespace Envoy
