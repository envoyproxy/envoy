#include "envoy/api/v2/cds.pb.h"
#include "envoy/config/overload/v2alpha/overload.pb.h"

class ThresholdTriggerImpl {
public:
  ThresholdTriggerImpl(const envoy::config::overload::v2alpha::ThresholdTrigger& /*config*/) {}
  void someMethod(envoy::api::v2::Cluster_LbPolicy) {}

  const envoy::config::overload::v2alpha::Trigger::TriggerOneofCase case_{};
};
