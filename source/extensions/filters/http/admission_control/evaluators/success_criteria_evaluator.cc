#include "extensions/filters/http/admission_control/evaluators/success_criteria_evaluator.h"

#include <algorithm>

#include "envoy/common/exception.h"
#include "envoy/grpc/status.h"

#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {

SuccessCriteriaEvaluator::SuccessCriteriaEvaluator(const SuccessCriteria& success_criteria) {
  // HTTP status.
  if (success_criteria.has_http_criteria()) {
    for (const auto& range : success_criteria.http_criteria().http_success_status()) {
      if (!validHttpRange(range.start(), range.end())) {
        throw EnvoyException(
            fmt::format("invalid HTTP range: [{}, {})", range.start(), range.end()));
      }

      const auto start = static_cast<uint64_t>(range.start());
      const auto end = static_cast<uint64_t>(range.end());
      http_success_fns_.emplace_back(
          [start, end](uint64_t status) { return (start <= status) && (status < end); });
    }
  } else {
    // We default to all non-5xx codes as successes.
    http_success_fns_.emplace_back([](uint64_t status) { return status < 500; });
  }

  // GRPC status.
  if (success_criteria.has_grpc_criteria()) {
    for (const auto& status : success_criteria.grpc_criteria().grpc_success_status()) {
      if (status > 16) {
        throw EnvoyException(fmt::format("invalid gRPC code {}", status));
      }

      grpc_success_codes_.emplace_back(status);
    }
  } else {
    grpc_success_codes_ = {
        enumToInt(Grpc::Status::WellKnownGrpcStatus::AlreadyExists),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::Canceled),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::FailedPrecondition),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::InvalidArgument),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::NotFound),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::Ok),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::OutOfRange),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::PermissionDenied),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::Unauthenticated),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::Unimplemented),
        enumToInt(Grpc::Status::WellKnownGrpcStatus::Unknown),
    };
  }
}

bool SuccessCriteriaEvaluator::isGrpcSuccess(uint32_t status) const {
  return std::count(grpc_success_codes_.begin(), grpc_success_codes_.end(), status) > 0;
}

bool SuccessCriteriaEvaluator::isHttpSuccess(uint64_t code) const {
  return std::any_of(http_success_fns_.begin(), http_success_fns_.end(),
                     [code](auto fn) { return fn(code); });
}

} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
