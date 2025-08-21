#pragma once

#include "source/common/common/base64.h"
#include "source/common/common/hex.h"
#include "source/tracing_context_impl.h"

#include "test/test_common/utility.h"

#include "cpp2sky/config.pb.h"
#include "cpp2sky/propagation.h"
#include "cpp2sky/tracing_context.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

using cpp2sky::createSpanContext;
using cpp2sky::SpanContextSharedPtr;
using cpp2sky::TracerConfig;
using cpp2sky::TracingContextFactory;
using cpp2sky::TracingContextSharedPtr;
using cpp2sky::TracingSpanSharedPtr;

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

    auto tracing_context_factory = std::make_unique<TracingContextFactory>(config);
    auto tracing_context = tracing_context_factory->create();

    auto entry_span = tracing_context->createEntrySpan();
    auto span = tracing_context->createExitSpan(entry_span);
    span->startSpan(seed + "#OPERATION");
    span->setPeer(seed + "#ADDRESS");

    if (!do_sample) {
      span->setSkipAnalysis();
    }

    span->endSpan();
    entry_span->endSpan();

    return tracing_context->createSW8HeaderValue(seed + "#ENDPOINT").value();
  }

  static TracingContextSharedPtr createSegmentContext(bool sampled, std::string seed,
                                                      std::string prev_seed) {
    TracerConfig config;
    config.set_service_name(seed + "#SERVICE");
    config.set_instance_name(seed + "#INSTANCE");
    auto tracing_context_factory = std::make_unique<TracingContextFactory>(config);

    SpanContextSharedPtr previous_span_context;
    if (!prev_seed.empty()) {
      std::string header_value = createPropagatedSW8HeaderValue(sampled, prev_seed);
      previous_span_context = createSpanContext(header_value);
      ASSERT(previous_span_context);
    }

    TracingContextSharedPtr tracing_context;
    if (previous_span_context) {
      tracing_context = tracing_context_factory->create(previous_span_context);
    } else {
      tracing_context = tracing_context_factory->create();
      if (!sampled) {
        tracing_context->setSkipAnalysis();
      }
    }

    return tracing_context;
  }

  static TracingSpanSharedPtr createSpanStore(TracingContextSharedPtr tracing_context,
                                              TracingSpanSharedPtr parent_span_store,
                                              std::string seed, bool sample = true) {
    auto span_store = parent_span_store ? tracing_context->createExitSpan(parent_span_store)
                                        : tracing_context->createEntrySpan();

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
