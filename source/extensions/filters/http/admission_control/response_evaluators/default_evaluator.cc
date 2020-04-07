namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdmissionControl {


DefaultResponseEvaluator::DefaultResponseEvaluator(
    AdmissionControlProto::DefaultSuccessCriteria success_criteria) {
  // HTTP status.
  if (success_criteria.http_status_size() != 0) {
    for (const auto& range : success_criteria.http_status()) {
      http_success_fns_.emplace_back([range](uint64_t status) {
        return range.start() <= status && status < range.end();
      });
    }
  } else {
    // We default to 2xx indicating success if unspecified.
    http_success_fns_.emplace_back([](uint64_t status){
      return 200 <= status && status < 300;
    });
  }

  // GRPC status.
  if (success_criteria.grpc_status_size() > 0) {
    for (const auto& status : success_criteria.grpc_status()) {
      grpc_success_codes_.emplace(enumToSignedInt(status.status()));
    }
  } else {
    grpc_success_codes_.emplace(enumToSignedInt(Grpc::Status::WellKnownGrpcStatus::Ok));
  }
}
bool DefaultResponseEvaluator::isGrpcSuccess(Grpc::Status::GrpcStatus status) const {
  // The HTTP status code is meaningless if the request has a GRPC content type.
  return grpc_success_codes_.count(enumToSignedInt(status)) > 0;
}

bool DefaultResponseEvaluator::isHttpSuccess(uint64_t code) const {
  for (const auto& fn : http_success_fns_) {
    if (fn(code)) {
      return true;
    }
  }
  return false;
}


} // namespace AdmissionControl
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
