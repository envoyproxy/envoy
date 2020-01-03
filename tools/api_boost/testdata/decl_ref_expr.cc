#include "envoy/config/overload/v2alpha/overload.pb.h"

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
  }
};
