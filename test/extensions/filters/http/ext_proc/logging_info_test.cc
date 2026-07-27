#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using envoy::config::core::v3::TrafficDirection;
using Filters::Common::ProcessingEffect::Effect;

class ExtProcLoggingInfoTest : public ::testing::Test {
public:
  ExtProcLoggingInfoTest() : logging_info_(filter_metadata_) {}

  Protobuf::Struct filter_metadata_;
  ExtProcLoggingInfo logging_info_;
};

TEST_F(ExtProcLoggingInfoTest, ProcessingEffectsConst) {
  const auto& const_logging_info = logging_info_;

  // Exercise both directions
  EXPECT_EQ(Effect::None,
            const_logging_info.processingEffects(TrafficDirection::INBOUND).header_effect_);
  EXPECT_EQ(Effect::None,
            const_logging_info.processingEffects(TrafficDirection::OUTBOUND).header_effect_);
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
