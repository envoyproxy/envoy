#include <chrono>

#include "common/common/token_bucket_impl.h"
#include "common/protobuf/utility.h"

#include "test/common/common/token_bucket_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

namespace Envoy {

DEFINE_PROTO_FUZZER(const test::common::common::TokenBucketFuzzTestCase& input) {
  // Exit early if the protobuf input does not satisfy the constraints.
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  }

  Event::SimulatedTimeSystem time_system;
  // The fill interval will always be greater than 50ms because of the validations in
  // token_bucket.proto.
  double fill_interval = DurationUtil::durationToSeconds(input.config().fill_interval());
  double fill_rate = input.config().tokens_per_fill().value() / fill_interval;
  ENVOY_LOG_MISC(info, "Creating token bucket with max {} and fill rate {}",
                 input.config().max_tokens(), fill_rate);
  TokenBucketImpl token_bucket{input.config().max_tokens(), time_system, fill_rate};

  // Now perform the action trace.
  for (const auto& action : input.actions()) {
    ENVOY_LOG_MISC(trace, "Action {}", action.DebugString());
    switch (action.action_selector_case()) {
    case test::common::common::Action::kConsume: {
      const auto& consume = action.consume();
      uint64_t consumed = token_bucket.consume(consume.tokens(), consume.allow_partial());
      ENVOY_LOG_MISC(trace, "Consumed {} tokens", consumed);
      break;
    }
    case test::common::common::Action::kNextToken: {
      token_bucket.nextTokenAvailable();
      break;
    }
    case test::common::common::Action::kReset: {
      const auto& reset = action.reset();
      token_bucket.reset(reset.num_tokens());
      break;
    }
    case test::common::common::Action::kAdvanceTime: {
      const auto& advance_time = action.advance_time();
      ENVOY_LOG_MISC(trace, "Advancing {} milliseconds", advance_time.milliseconds());
      time_system.advanceTimeWait(std::chrono::milliseconds(advance_time.milliseconds()));
      break;
    }
    default:
      // Nothing to do.
      break;
    }
  }
}

} // namespace Envoy
