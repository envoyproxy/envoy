#pragma once

#include "common/common/base64.h"
#include "common/common/hex.h"

#include "extensions/tracers/skywalking/skywalking_types.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

/*
 * A simple helper class for auxiliary testing. Contains some simple static functions, such as
 * encoding, generating random id, creating SpanContext, etc.
 */
class SkyWalkingTestHelper {
public:
  static std::string generateId(Random::RandomGenerator& random) {
    return absl::StrCat(Hex::uint64ToHex(random.random()), Hex::uint64ToHex(random.random()));
  }

  static std::string base64Encode(absl::string_view input) {
    return Base64::encode(input.data(), input.length());
  }

  static SegmentContextSharedPtr createSegmentContext(bool sampled, std::string seed,
                                                      std::string prev_seed,
                                                      Random::RandomGenerator& random) {
    SpanContextPtr previous_span_context;
    if (!prev_seed.empty()) {
      std::string header_value =
          fmt::format("{}-{}-{}-{}-{}-{}-{}-{}", sampled ? 1 : 0, base64Encode(generateId(random)),
                      base64Encode(generateId(random)), random.random(),
                      base64Encode(prev_seed + "#SERVICE"), base64Encode(prev_seed + "#INSTANCE"),
                      base64Encode(prev_seed + "#ENDPOINT"), base64Encode(prev_seed + "#ADDRESS"));

      Http::TestRequestHeaderMapImpl request_headers{{"sw8", header_value}};
      previous_span_context = SpanContext::spanContextFromRequest(request_headers);
      ASSERT(previous_span_context);
    }
    Tracing::Decision decision;
    decision.traced = sampled;
    decision.reason = Tracing::Reason::Sampling;

    auto segment_context =
        std::make_shared<SegmentContext>(std::move(previous_span_context), decision, random);

    segment_context->setService(seed + "#SERVICE");
    segment_context->setServiceInstance(seed + "#INSTANCE");

    return segment_context;
  }

  static SpanStore* createSpanStore(SegmentContext* segment_context, SpanStore* parent_span_store,
                                    std::string seed) {
    SpanStore* span_store = segment_context->createSpanStore(parent_span_store);

    span_store->setAsError(false);
    span_store->setOperation(seed + "#OPERATION");
    span_store->setPeerAddress("0.0.0.0");
    span_store->setStartTime(22222222);
    span_store->setEndTime(33333333);

    span_store->addTag(seed + "#TAG_KEY_A", seed + "#TAG_VALUE_A");
    span_store->addTag(seed + "#TAG_KEY_B", seed + "#TAG_VALUE_B");
    span_store->addTag(seed + "#TAG_KEY_C", seed + "#TAG_VALUE_C");
    return span_store;
  }
};

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
