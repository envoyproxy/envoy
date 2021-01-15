#pragma once

#include "common/common/base64.h"
#include "common/common/hex.h"

#include "test/test_common/utility.h"

#include "cpp2sky/config.pb.h"
#include "cpp2sky/propagation.h"
#include "cpp2sky/segment_context.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

using cpp2sky::createSegmentContextFactory;
using cpp2sky::createSpanContext;
using cpp2sky::CurrentSegmentSpanPtr;
using cpp2sky::SegmentContextPtr;
using cpp2sky::SpanContextPtr;
using cpp2sky::TracerConfig;

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

  static std::string createPropagatedSW8HeaderValue(bool do_sample, std::string seed) {
    TracerConfig config;
    config.set_service_name(seed + "#SERVICE");
    config.set_instance_name(seed + "#INSTANCE");
    auto segment_context_factory = createSegmentContextFactory(config);
    auto segment_context = segment_context_factory->create();

    auto span = segment_context->createCurrentSegmentRootSpan();
    span->startSpan(seed + "#OPERATION");
    span->setPeer(seed + "#ADDRESS");

    if (!do_sample) {
      span->setSkipAnalysis();
    }

    span->endSpan();

    return segment_context->createSW8HeaderValue(seed + "#ENDPOINT");
  }

  static SegmentContextPtr createSegmentContext(bool sampled, std::string seed,
                                                std::string prev_seed) {
    TracerConfig config;
    config.set_service_name(seed + "#SERVICE");
    config.set_instance_name(seed + "#INSTANCE");
    auto segment_context_factory = createSegmentContextFactory(config);

    SpanContextPtr previous_span_context;
    if (!prev_seed.empty()) {
      std::string header_value = createPropagatedSW8HeaderValue(sampled, prev_seed);
      previous_span_context = createSpanContext(header_value);
      ASSERT(previous_span_context);
    }

    SegmentContextPtr segment_context;
    if (previous_span_context) {
      segment_context = segment_context_factory->create(previous_span_context);
    } else {
      segment_context = segment_context_factory->create();
      if (!sampled) {
        segment_context->setSkipAnalysis();
      }
    }

    return segment_context;
  }

  static CurrentSegmentSpanPtr createSpanStore(SegmentContextPtr segment_context,
                                               CurrentSegmentSpanPtr parent_span_store,
                                               std::string seed, bool sample = true) {
    auto span_store = parent_span_store
                          ? segment_context->createCurrentSegmentSpan(parent_span_store)
                          : segment_context->createCurrentSegmentRootSpan();

    span_store->startSpan(seed + "#OPERATION");
    span_store->setPeer("0.0.0.0");
    span_store->addTag(seed + "#TAG_KEY_A", seed + "#TAG_VALUE_A");
    span_store->addTag(seed + "#TAG_KEY_B", seed + "#TAG_VALUE_B");
    span_store->addTag(seed + "#TAG_KEY_C", seed + "#TAG_VALUE_C");

    if (!sample) {
      span_store->setSkipAnalysis();
    }

    span_store->endSpan();

    return span_store;
  }
};

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
