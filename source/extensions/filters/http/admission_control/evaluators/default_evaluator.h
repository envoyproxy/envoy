#pragma once

#include <vector>

#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"
#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.validate.h"

#include "extensions/filters/http/admission_control/evaluators/response_evaluator.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

class DefaultResponseEvaluator : public ResponseEvaluator {
public:
  DefaultResponseEvaluator(envoy::extensions::filters::http::admission_control::v3alpha::
                               AdmissionControl::DefaultEvaluationCriteria evaluation_criteria);
  // ResponseEvaluator
  bool isHttpSuccess(uint64_t code) const override;
  bool isGrpcSuccess(uint32_t status) const override;

private:
  std::vector<std::function<bool(uint64_t)>> http_success_fns_;
  std::vector<uint64_t> grpc_success_codes_;
};

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
