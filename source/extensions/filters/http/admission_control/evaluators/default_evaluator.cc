#include "extensions/filters/http/admission_control/evaluators/default_evaluator.h"

#include <algorithm>

#include "envoy/grpc/status.h"

#include "common/common/enum_to_int.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

DefaultResponseEvaluator::DefaultResponseEvaluator(
    envoy::extensions::filters::http::admission_control::v3alpha::AdmissionControl::
        DefaultEvaluationCriteria evaluation_criteria) {
  // HTTP status.
  if (evaluation_criteria.http_success_status_size() > 0) {
    for (const auto& range : evaluation_criteria.http_success_status()) {
      http_success_fns_.emplace_back([range](uint64_t status) {
        return (static_cast<uint64_t>(range.start()) <= status) &&
               (status < static_cast<uint64_t>(range.end()));
      });
    }
  } else {
    // We default to all 5xx codes as request failures.
    http_success_fns_.emplace_back([](uint64_t status) { return status < 500; });
  }

  // GRPC status.
  if (evaluation_criteria.grpc_success_status_size() > 0) {
    for (const auto& status : evaluation_criteria.grpc_success_status()) {
      grpc_success_codes_.emplace_back(status);
    }
  } else {
    grpc_success_codes_ = decltype(grpc_success_codes_)({
        enumToInt(Grpc::Status::WellKnownGrpcStatus::Ok),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::Canceled),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::Unknown),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::InvalidArgument),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::NotFound),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::AlreadyExists),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::Unauthenticated),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::FailedPrecondition),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::OutOfRange),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::Unimplemented),
    });
  }
}

bool DefaultResponseEvaluator::isGrpcSuccess(uint32_t status) const {
  return std::count(grpc_success_codes_.begin(), grpc_success_codes_.end(), status) > 0;
}

bool DefaultResponseEvaluator::isHttpSuccess(uint64_t code) const {
  return std::any_of(http_success_fns_.begin(), http_success_fns_.end(),
                     [code](auto fn) { return fn(code); });
}

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
