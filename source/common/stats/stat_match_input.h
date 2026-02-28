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

class StatTagValueInput : public Matcher::DataInput<Envoy::Stats::StatTagMatchingData> {
public:
  StatTagValueInput() = default;

  Matcher::DataInputGetResult get(const Envoy::Stats::StatTagMatchingData& data) const override {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            std::string(data.value())};
  }
};

class StatTagValueInputFactory
    : public Matcher::DataInputFactory<Envoy::Stats::StatTagMatchingData> {
public:
  std::string name() const override { return "stat_tag_value_input"; }

  Envoy::Matcher::DataInputFactoryCb<Envoy::Stats::StatTagMatchingData>
  createDataInputFactoryCb(const Envoy::Protobuf::Message&,
                           Envoy::ProtobufMessage::ValidationVisitor&) override {
    return [] { return std::make_unique<StatTagValueInput>(); };
  }

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::common_inputs::stats::v3::StatTagValueInput>();
  }
};

} // namespace Stats
} // namespace Envoy
