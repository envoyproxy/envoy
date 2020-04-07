#include "envoy/extensions/filters/http/admission_control/v3alpha/admission_control.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

class DefaultResponseEvaluator : public ResponseEvaluator {
public:
  DefaultResponseEvaluator(AdmissionControlProto::DefaultSuccessCriteria success_criteria);
  virtual bool isHttpSuccess(uint64_t code) const override;
  virtual bool isGrpcSuccess(uint32_t status) const override;

private:
  std::vector<std::function<bool(uint64_t)>> http_success_fns_;
  std::unordered_set<uint64_t> grpc_success_codes_;
};

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
