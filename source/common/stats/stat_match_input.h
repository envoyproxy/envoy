#pragma once


#include "envoy/type/matcher/v3/stat_input.pb.h"
#include "envoy/stats/stats.h"
#include "source/common/stats/stats_matcher_impl.h"

namespace Envoy {
namespace Stats {

class StatFullNameMatchInput
    : public Matcher::DataInput<Envoy::Stats::StatMatchingData> {
 public:
  Matcher::DataInputGetResult get(
      const Envoy::Stats::StatMatchingData& data) const override {
    return {Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            data.full_name()};
  }
};

class StatFullNameMatchInputFactory
    : public Matcher::DataInputFactory<Envoy::Stats::StatMatchingData> {
 public:
  std::string name() const override { return "stat_full_name_match_input"; }

  Envoy::Matcher::DataInputFactoryCb<Envoy::Stats::StatMatchingData>
  createDataInputFactoryCb(
      const Envoy::Protobuf::Message&,
      Envoy::ProtobufMessage::ValidationVisitor&) override {
    return [] { return std::make_unique<StatFullNameMatchInput>(); };
  }

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::type::matcher::v3::StatFullNameMatchInput>();
  }
};

}  // namespace Stats
}  // namespace Envoy
