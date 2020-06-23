#include "envoy/common/exception.h"
#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/network/local_ratelimit/local_ratelimit.h"

#include "test/extensions/filters/network/local_ratelimit/local_ratelimit_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LocalRateLimitFilter {
struct ActiveFilter {
  ActiveFilter(const ConfigSharedPtr& config) : filter_(config) {
    filter_.initializeReadFilterCallbacks(read_filter_callbacks_);
  }

  NiceMock<Network::MockReadFilterCallbacks> read_filter_callbacks_;
  Filter filter_;
};

DEFINE_PROTO_FUZZER(
    const envoy::extensions::filters::network::local_ratelimit::LocalRateLimitTestCase& input) {

  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  } catch (const ProtobufMessage::DeprecatedProtoFieldException& e) {
    ENVOY_LOG_MISC(debug, "DeprecatedProtoFieldException: {}", e.what());
    return;
  }
  if (input.config().token_bucket().fill_interval().nanos() < 0) {
    // TODO:
    // protoc-gen-validate has an issue on type "Duration" which may generate interval with seconds
    // > 0 while "nanos" < 0. And negative "nanos" will cause validation inside the filter to fail.
    // see https://github.com/envoyproxy/protoc-gen-validate/issues/348 for detail.
    ENVOY_LOG_MISC(debug, "In fill_interval, nanos should not be negative!");
    return;
  }
  static NiceMock<Event::MockDispatcher> dispatcher;
  Stats::IsolatedStoreImpl stats_store;
  static NiceMock<Runtime::MockLoader> runtime;
  Event::MockTimer* fill_timer = new Event::MockTimer(&dispatcher);
  envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit proto_config =
      input.config();
  ConfigSharedPtr config = nullptr;
  try {
    config = std::make_shared<Config>(proto_config, dispatcher, stats_store, runtime);
  } catch (EnvoyException e) {
    ENVOY_LOG_MISC(debug, "EnvoyException in config's constructor: {}", e.what());
    return;
  }

  ActiveFilter active_filter(config);
  std::chrono::milliseconds fill_interval(
      PROTOBUF_GET_MS_REQUIRED(proto_config.token_bucket(), fill_interval));

  for (const auto& action : input.actions()) {
    ENVOY_LOG_MISC(trace, "action {}", action.DebugString());

    switch (action.action_selector_case()) {
    case envoy::extensions::filters::network::local_ratelimit::Action::kOnData: {
      Buffer::OwnedImpl buffer(action.on_data().data());
      active_filter.filter_.onData(buffer, action.on_data().end_stream());
      break;
    }
    case envoy::extensions::filters::network::local_ratelimit::Action::kOnNewConnection: {
      active_filter.filter_.onNewConnection();
      break;
    }
    case envoy::extensions::filters::network::local_ratelimit::Action::kRefill: {
      EXPECT_CALL(*fill_timer, enableTimer(fill_interval, nullptr));
      fill_timer->invokeCallback();
      break;
    }
    default:
      // Unhandled actions
      PANIC("A case is missing for an action");
    }
  }
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
  // Silence clang-tidy here because it thinks there is a memory leak for "fill_timer"
  // However, ownership of each MockTimer instance is transferred to the (caller of) dispatcher's
  // createTimer_(), so to avoid destructing it twice, the MockTimer must have been dynamically
  // allocated and must not be deleted by it's creator. See test/mocks/event/mocks.cc for detail.
} // namespace LocalRateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
