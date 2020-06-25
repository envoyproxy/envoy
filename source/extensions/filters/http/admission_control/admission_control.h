#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/time.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/cleanup.h"
#include "common/grpc/common.h"
#include "common/grpc/status.h"
#include "common/http/codes.h"
#include "common/runtime/runtime_protos.h"

#include "extensions/filters/http/admission_control/evaluators/response_evaluator.h"
#include "extensions/filters/http/admission_control/thread_local_controller.h"
#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

/**
 * All stats for the admission control filter.
 */
#define ALL_ADMISSION_CONTROL_STATS(COUNTER) COUNTER(rq_rejected)

/**
 * Wrapper struct for admission control filter stats. @see stats_macros.h
 */
struct AdmissionControlStats {
  ALL_ADMISSION_CONTROL_STATS(GENERATE_COUNTER_STRUCT)
};

using AdmissionControlProto =
    envoy::extensions::filters::http::admission_control::v3alpha::AdmissionControl;

/**
 * Configuration for the admission control filter.
 */
class AdmissionControlFilterConfig {
public:
  AdmissionControlFilterConfig(const AdmissionControlProto& proto_config, Runtime::Loader& runtime,
                               Runtime::RandomGenerator& random, Stats::Scope& scope,
                               ThreadLocal::SlotPtr&& tls,
                               std::shared_ptr<ResponseEvaluator> response_evaluator);
  virtual ~AdmissionControlFilterConfig() = default;

  virtual ThreadLocalController& getController() const {
    return tls_->getTyped<ThreadLocalControllerImpl>();
  }

  Runtime::RandomGenerator& random() const { return random_; }
  bool filterEnabled() const { return admission_control_feature_.enabled(); }
  Stats::Scope& scope() const { return scope_; }
  double aggression() const;
  ResponseEvaluator& responseEvaluator() const { return *response_evaluator_; }

private:
  Runtime::RandomGenerator& random_;
  Stats::Scope& scope_;
  const ThreadLocal::SlotPtr tls_;
  Runtime::FeatureFlag admission_control_feature_;
  std::unique_ptr<Runtime::Double> aggression_;
  std::shared_ptr<ResponseEvaluator> response_evaluator_;
};

using AdmissionControlFilterConfigSharedPtr = std::shared_ptr<const AdmissionControlFilterConfig>;

/**
 * A filter that probabilistically rejects requests based on upstream success-rate.
 */
class AdmissionControlFilter : public Http::PassThroughFilter,
                               Logger::Loggable<Logger::Id::filter> {
public:
  AdmissionControlFilter(AdmissionControlFilterConfigSharedPtr config,
                         const std::string& stats_prefix);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

private:
  static AdmissionControlStats generateStats(Stats::Scope& scope, const std::string& prefix) {
    return {ALL_ADMISSION_CONTROL_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  bool shouldRejectRequest() const;

  void recordSuccess() { config_->getController().recordSuccess(); }

  void recordFailure() { config_->getController().recordFailure(); }

  const AdmissionControlFilterConfigSharedPtr config_;
  AdmissionControlStats stats_;
  bool expect_grpc_status_in_trailer_;

  // If false, the filter will forego recording a request success or failure during encoding.
  bool record_request_;
};

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
