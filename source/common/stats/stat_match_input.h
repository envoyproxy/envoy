#pragma once

#include "envoy/extensions/matching/common_inputs/stats/v3/stats.pb.h"
#include "envoy/stats/stats.h"

#include "source/common/stats/stats_matcher_impl.h"

namespace Envoy {
namespace Stats {

class StatFullNameMatchInput : public Matcher::DataInput<Stats::StatMatchingData> {
public:
  Matcher::DataInputGetResult get(const Stats::StatMatchingData& data) const override {
    return Matcher::DataInputGetResult::CreateString(data.fullName());
  }
};

class StatFullNameMatchInputFactory : public Matcher::DataInputFactory<Stats::StatMatchingData> {
public:
  std::string name() const override { return "stat_full_name_match_input"; }

  Matcher::DataInputFactoryCb<Stats::StatMatchingData>
  createDataInputFactoryCb(const Protobuf::Message&, ProtobufMessage::ValidationVisitor&) override {
    return [] { return std::make_unique<StatFullNameMatchInput>(); };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::matching::common_inputs::stats::v3::StatFullNameMatchInput>();
  }
};

} // namespace Stats
} // namespace Envoy
