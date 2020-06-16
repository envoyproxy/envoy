#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/extensions/filters/network/local_ratelimit/local_ratelimit_fuzz.pb.h"
#include "test/extensions/filters/network/local_ratelimit/local_ratelimit_fuzz.pb.validate.h"
#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.h"
#include "common/stats/isolated_store_impl.h"
#include "common/buffer/buffer_impl.h"
#include "extensions/filters/network/local_ratelimit/local_ratelimit.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;

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

DEFINE_PROTO_FUZZER(const envoy::extensions::filters::network::local_ratelimit::LocalRateLimitTestCase& input) {
  
  try {
    TestUtility::validate(input);
  } catch (const ProtoValidationException& e) {
    ENVOY_LOG_MISC(debug, "ProtoValidationException: {}", e.what());
    return;
  } catch (const ProtobufMessage::DeprecatedProtoFieldException& e) {
    ENVOY_LOG_MISC(debug, "DeprecatedProtoFieldException: {}", e.what());
    return;
  } 
  if(input.config().token_bucket().fill_interval().nanos()<0){
      ENVOY_LOG_MISC(debug, "Nanos should not be negative!");
      return;
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
  Event::MockTimer* fill_timer_=new Event::MockTimer(&dispatcher_);
  ConfigSharedPtr config_;
  ActiveFilter* active_filter;
 
  envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit proto_config = input.config();
  config_ = ::std::make_shared<Config>(proto_config, dispatcher_, stats_store_, runtime_);

  try{
    active_filter=new active_filter(config_);
  }catch (const EnvoyException e){
    ENVOY_LOG_MISC(debug, "EnvoyException in constructor of ActiveFilter: {}", e.what());
    return;
  }

  std::chrono::milliseconds fill_interval_(PROTOBUF_GET_MS_REQUIRED(proto_config.token_bucket(), fill_interval));

  for (const auto& action : input.actions()) {
    ENVOY_LOG_MISC(trace, "action {}", action.DebugString());

    switch (action.action_selector_case()) {
    case envoy::extensions::filters::network::local_ratelimit::Action::kOnData: {
      Buffer::OwnedImpl buffer(action.on_data().data());
      active_filter->filter_.onData(buffer, action.on_data().end_stream());
      break;
    }
    case envoy::extensions::filters::network::local_ratelimit::Action::kOnNewConnection: {
      active_filter->filter_.onNewConnection();
      break;
    }
    case envoy::extensions::filters::network::local_ratelimit::Action::kRefill:{
      EXPECT_CALL(*fill_timer_, enableTimer(fill_interval_, nullptr));
      fill_timer_->invokeCallback();
      break;
    }
    default:
      // Maybe nothing is set?
      break;
    }
  }
}

} // namespace LocalRateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
