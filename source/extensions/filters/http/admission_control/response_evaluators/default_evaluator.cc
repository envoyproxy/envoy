#include "extensions/filters/http/admission_control/response_evaluators/default_evaluator.h"

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
  if (evaluation_criteria.http_status_size() > 0) {
    for (const auto& range : evaluation_criteria.http_status()) {
      http_success_fns_.emplace_back(
          [range](int status) { return (range.start() <= status) && (status < range.end()); });
    }
  } else {
    // We default to all 5xx codes as request failures.
    http_success_fns_.emplace_back([](uint64_t status) { return status < 500; });
  }

  // GRPC status.
  if (evaluation_criteria.grpc_status_size() > 0) {
    for (const auto& status : evaluation_criteria.grpc_status()) {
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
  for (const auto& fn : http_success_fns_) {
    if (!fn(code)) {
      return false;
    }
  }

  return true;
}

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
