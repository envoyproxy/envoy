#include "extensions/filters/http/aws_lambda/aws_lambda_filter.h"

#include <string>
#include <vector>

#include "envoy/upstream/upstream.h"

#include "common/common/fmt.h"
#include "common/common/hex.h"
#include "common/crypto/utility.h"
#include "common/http/headers.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/well_known_names.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsLambdaFilter {

namespace {

constexpr auto filter_metadata_key = "com.amazonaws.lambda";
constexpr auto egress_gateway_metadata_key = "egress_gateway";

void setHeaders(Http::RequestHeaderMap& headers, absl::string_view function_name) {
  headers.setMethod(Http::Headers::get().MethodValues.Post);
  headers.setPath(fmt::format("/2015-03-31/functions/{}/invocations", function_name));
  headers.setCopy(Http::LowerCaseString{"x-amz-invocation-type"}, "RequestResponse");
}

/**
 * Determines if the target cluster has the AWS Lambda metadata on it.
 */
bool isTargetClusterLambdaGateway(Upstream::ClusterInfo const& cluster_info) {
  using ProtobufWkt::Value;
  const auto& filter_metadata_map = cluster_info.metadata().filter_metadata();
  auto metadata_it = filter_metadata_map.find(filter_metadata_key);
  if (metadata_it == filter_metadata_map.end()) {
    return false;
  }

  auto egress_gateway_it = metadata_it->second.fields().find(egress_gateway_metadata_key);
  if (egress_gateway_it == metadata_it->second.fields().end()) {
    return false;
  }

  if (egress_gateway_it->second.kind_case() != Value::KindCase::kBoolValue) {
    return false;
  }

  return egress_gateway_it->second.bool_value();
}

} // namespace

Filter::Filter(const FilterSettings& settings,
               const std::shared_ptr<Extensions::Common::Aws::Signer>& sigv4_signer)
    : settings_(settings), sigv4_signer_(sigv4_signer) {}

absl::optional<Arn> Filter::calculateRouteArn() {
  if (!decoder_callbacks_->route() || !decoder_callbacks_->route()->routeEntry()) {
    return absl::nullopt;
  }
  const auto* route_entry = decoder_callbacks_->route()->routeEntry();
  const auto* settings = route_entry->mostSpecificPerFilterConfigTyped<FilterSettings>(
      HttpFilterNames::get().AwsLambda);
  if (!settings) {
    return absl::nullopt;
  }

  return parseArn(settings->arn());
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  if (!settings_.payloadPassthrough()) {
    skip_ = true;
    return Http::FilterHeadersStatus::Continue;
  }

  auto route_arn = calculateRouteArn();
  if (route_arn.has_value()) {
    auto cluster_info_ptr = decoder_callbacks_->clusterInfo();
    ASSERT(cluster_info_ptr);
    if (!isTargetClusterLambdaGateway(*cluster_info_ptr)) {
      skip_ = true;
      return Http::FilterHeadersStatus::Continue;
    }
    arn_.swap(route_arn);
  } else {
    arn_ = parseArn(settings_.arn());
    if (!arn_.has_value()) {
      ENVOY_LOG(error, "Unable to parse Lambda ARN {}.", settings_.arn());
      skip_ = true;
      return Http::FilterHeadersStatus::Continue;
    }
  }

  if (end_stream) {
    setHeaders(headers, arn_->functionName());
    sigv4_signer_->sign(headers);
    return Http::FilterHeadersStatus::Continue;
  }

  headers_ = &headers;
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  UNREFERENCED_PARAMETER(data);
  if (skip_) {
    return Http::FilterDataStatus::Continue;
  }

  if (end_stream) {
    setHeaders(*headers_, arn_->functionName());
    auto& hashing_util = Envoy::Common::Crypto::UtilitySingleton::get();
    const Buffer::Instance& decoding_buffer = *decoder_callbacks_->decodingBuffer();
    const auto hash = Hex::encode(hashing_util.getSha256Digest(decoding_buffer));
    sigv4_signer_->sign(*headers_, hash);
    return Http::FilterDataStatus::Continue;
  }
  return Http::FilterDataStatus::StopIterationAndBuffer;
}

absl::optional<Arn> parseArn(absl::string_view arn) {
  const std::vector<absl::string_view> parts = absl::StrSplit(arn, ':');
  constexpr auto min_arn_size = 7;
  if (parts.size() < min_arn_size) {
    return absl::nullopt;
  }

  if (parts[0] != "arn") {
    return absl::nullopt;
  }

  auto partition = parts[1];
  auto service = parts[2];
  auto region = parts[3];
  auto account_id = parts[4];
  auto resource_type = parts[5];
  auto function_name = parts[6];

  // If the ARN contains a function version/alias, then we want it to be part of the function name.
  // For example:
  // arn:aws:lambda:us-west-2:987654321:function:hello_envoy:v1
  if (parts.size() > min_arn_size) {
    std::string versioned_function_name = std::string(function_name);
    versioned_function_name.push_back(':');
    versioned_function_name += std::string(parts[7]);
    return Arn{partition, service, region, account_id, resource_type, versioned_function_name};
  }

  return Arn{partition, service, region, account_id, resource_type, function_name};
}

} // namespace AwsLambdaFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
