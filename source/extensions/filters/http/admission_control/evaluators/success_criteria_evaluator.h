#pragma once

#include <vector>

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.validate.h"

#include "extensions/filters/http/admission_control/evaluators/response_evaluator.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

class SuccessCriteriaEvaluator : public ResponseEvaluator {
public:
  using SuccessCriteria = envoy::extensions::filters::http::admission_control::v3alpha::
      AdmissionControl::SuccessCriteria;
  SuccessCriteriaEvaluator(const SuccessCriteria& evaluation_criteria);
  // ResponseEvaluator
  bool isHttpSuccess(uint64_t code) const override;
  bool isGrpcSuccess(uint32_t status) const override;

private:
  bool validHttpRange(const int32_t start, const int32_t end) const {
    return start <= end && start < 600 && start >= 100 && end <= 600 && end >= 100;
  }

  std::vector<std::function<bool(uint64_t)>> http_success_fns_;
  std::vector<uint32_t> grpc_success_codes_;
};

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
