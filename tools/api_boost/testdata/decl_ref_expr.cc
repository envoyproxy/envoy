#include "envoy/config/overload/v2alpha/overload.pb.h"

#define API_NO_BOOST(x) x
#define BAR(x) x

using envoy::config::overload::v2alpha::Trigger;

class ThresholdTriggerImpl {
public:
  ThresholdTriggerImpl(const envoy::config::overload::v2alpha::Trigger& config) {
    switch (config.trigger_oneof_case()) {
    case envoy::config::overload::v2alpha::Trigger::kThreshold:
      break;
    default:
      break;
    }
    switch (config.trigger_oneof_case()) {
    case Trigger::kThreshold:
      break;
    default:
      break;
    }
    API_NO_BOOST(envoy::config::overload::v2alpha::Trigger) foo;
    BAR(API_NO_BOOST(envoy::config::overload::v2alpha::Trigger)) bar;
    BAR(envoy::config::overload::v2alpha::Trigger) baz;
  }
};
