#include "extensions/filters/common/fault/fault_config.h"

#include "envoy/extensions/filters/common/fault/v3/fault.pb.h"
#include "envoy/extensions/filters/http/fault/v3/fault.pb.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Fault {

envoy::type::v3::FractionalPercent
HeaderPercentageProvider::percentage(const Http::RequestHeaderMap* request_headers) const {
  const auto header = request_headers->get(header_name_);
  if (header == nullptr) {
    return percentage_;
  }

  uint32_t header_numerator;
  if (!absl::SimpleAtoi(header->value().getStringView(), &header_numerator)) {
    return percentage_;
  }

  envoy::type::v3::FractionalPercent result;
  result.set_numerator(std::min(header_numerator, percentage_.numerator()));
  result.set_denominator(percentage_.denominator());
  return result;
}

FaultAbortConfig::FaultAbortConfig(
    const envoy::extensions::filters::http::fault::v3::FaultAbort& abort_config) {
  switch (abort_config.error_type_case()) {
  case envoy::extensions::filters::http::fault::v3::FaultAbort::ErrorTypeCase::kHttpStatus:
    provider_ =
        std::make_unique<FixedAbortProvider>(static_cast<Http::Code>(abort_config.http_status()),
                                             absl::nullopt, abort_config.percentage());
    break;
  case envoy::extensions::filters::http::fault::v3::FaultAbort::ErrorTypeCase::kGrpcStatus:
    provider_ = std::make_unique<FixedAbortProvider>(
        absl::nullopt, static_cast<Grpc::Status::GrpcStatus>(abort_config.grpc_status()),
        abort_config.percentage());
    break;
  case envoy::extensions::filters::http::fault::v3::FaultAbort::ErrorTypeCase::kHeaderAbort:
    provider_ = std::make_unique<HeaderAbortProvider>(abort_config.percentage());
    break;
  case envoy::extensions::filters::http::fault::v3::FaultAbort::ErrorTypeCase::ERROR_TYPE_NOT_SET:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

absl::optional<Http::Code> FaultAbortConfig::HeaderAbortProvider::httpStatusCode(
    const Http::RequestHeaderMap* request_headers) const {
  absl::optional<Http::Code> ret = absl::nullopt;
  auto header = request_headers->get(Filters::Common::Fault::HeaderNames::get().AbortRequest);
  if (header == nullptr) {
    return ret;
  }

  uint64_t code;
  if (!absl::SimpleAtoi(header->value().getStringView(), &code)) {
    return ret;
  }

  if (code >= 200 && code < 600) {
    ret = static_cast<Http::Code>(code);
  }

  return ret;
}

absl::optional<Grpc::Status::GrpcStatus> FaultAbortConfig::HeaderAbortProvider::grpcStatusCode(
    const Http::RequestHeaderMap* request_headers) const {
  auto header = request_headers->get(Filters::Common::Fault::HeaderNames::get().AbortGrpcRequest);
  if (header == nullptr) {
    return absl::nullopt;
  }

  uint64_t code;
  if (!absl::SimpleAtoi(header->value().getStringView(), &code)) {
    return absl::nullopt;
  }

  return static_cast<Grpc::Status::GrpcStatus>(code);
}

envoy::type::v3::FractionalPercent FaultAbortConfig::HeaderAbortProvider::percentage(
    const Http::RequestHeaderMap* request_headers) const {
  // if the abort fault contains http status header, then use http status request percentage.
  if (request_headers->get(Filters::Common::Fault::HeaderNames::get().AbortRequest) != nullptr) {
    return http_header_percentage_provider_.percentage(request_headers);
  }

  return grpc_header_percentage_provider_.percentage(request_headers);
}

FaultDelayConfig::FaultDelayConfig(
    const envoy::extensions::filters::common::fault::v3::FaultDelay& delay_config) {
  switch (delay_config.fault_delay_secifier_case()) {
  case envoy::extensions::filters::common::fault::v3::FaultDelay::FaultDelaySecifierCase::
      kFixedDelay:
    provider_ = std::make_unique<FixedDelayProvider>(
        std::chrono::milliseconds(PROTOBUF_GET_MS_REQUIRED(delay_config, fixed_delay)),
        delay_config.percentage());
    break;
  case envoy::extensions::filters::common::fault::v3::FaultDelay::FaultDelaySecifierCase::
      kHeaderDelay:
    provider_ = std::make_unique<HeaderDelayProvider>(delay_config.percentage());
    break;
  case envoy::extensions::filters::common::fault::v3::FaultDelay::FaultDelaySecifierCase::
      FAULT_DELAY_SECIFIER_NOT_SET:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

absl::optional<std::chrono::milliseconds> FaultDelayConfig::HeaderDelayProvider::duration(
    const Http::RequestHeaderMap* request_headers) const {
  const auto header = request_headers->get(HeaderNames::get().DelayRequest);
  if (header == nullptr) {
    return absl::nullopt;
  }

  uint64_t value;
  if (!absl::SimpleAtoi(header->value().getStringView(), &value)) {
    return absl::nullopt;
  }

  return std::chrono::milliseconds(value);
}

FaultRateLimitConfig::FaultRateLimitConfig(
    const envoy::extensions::filters::common::fault::v3::FaultRateLimit& rate_limit_config) {
  switch (rate_limit_config.limit_type_case()) {
  case envoy::extensions::filters::common::fault::v3::FaultRateLimit::LimitTypeCase::kFixedLimit:
    provider_ = std::make_unique<FixedRateLimitProvider>(
        rate_limit_config.fixed_limit().limit_kbps(), rate_limit_config.percentage());
    break;
  case envoy::extensions::filters::common::fault::v3::FaultRateLimit::LimitTypeCase::kHeaderLimit:
    provider_ = std::make_unique<HeaderRateLimitProvider>(rate_limit_config.percentage());
    break;
  case envoy::extensions::filters::common::fault::v3::FaultRateLimit::LimitTypeCase::
      LIMIT_TYPE_NOT_SET:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

absl::optional<uint64_t> FaultRateLimitConfig::HeaderRateLimitProvider::rateKbps(
    const Http::RequestHeaderMap* request_headers) const {
  const auto header = request_headers->get(HeaderNames::get().ThroughputResponse);
  if (header == nullptr) {
    return absl::nullopt;
  }

  uint64_t value;
  if (!absl::SimpleAtoi(header->value().getStringView(), &value)) {
    return absl::nullopt;
  }

  if (value == 0) {
    return absl::nullopt;
  }

  return value;
}

} // namespace Fault
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
