#include "envoy/config/overload/v2alpha/overload.pb.h"

using envoy::config::overload::v2alpha::ThresholdTrigger;
using ::envoy::config::overload::v2alpha::Trigger;
using SomePtrAlias = std::unique_ptr<envoy::config::overload::v2alpha::ThresholdTrigger>;

class ThresholdTriggerImpl {
public:
  ThresholdTriggerImpl(const ThresholdTrigger& /*config*/) {}
  ThresholdTriggerImpl(SomePtrAlias /*config*/) {}
};
