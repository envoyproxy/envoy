#include "source/extensions/filters/http/aws_lambda/aws_lambda_filter.h"

#include <string>
#include <vector>

#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/upstream/upstream.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/base64.h"
#include "source/common/common/fmt.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/singleton/const_singleton.h"
#include "source/extensions/filters/http/aws_lambda/request_response.pb.validate.h"

#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsLambdaFilter {

class LambdaFilterNameValues {
public:
  Http::LowerCaseString InvocationTypeHeader{std::string{"x-amz-invocation-type"}};
  Http::LowerCaseString FunctionErrorHeader{std::string{"x-amz-function-error"}};
};

using LambdaFilterNames = ConstSingleton<LambdaFilterNameValues>;

namespace {

constexpr auto filter_metadata_key = "com.amazonaws.lambda";
constexpr auto egress_gateway_metadata_key = "egress_gateway";

void setLambdaHeaders(Http::RequestHeaderMap& headers, const absl::optional<Arn>& arn,
                      InvocationMode mode, const std::string& host_rewrite) {
  headers.setMethod(Http::Headers::get().MethodValues.Post);
  headers.setPath(fmt::format("/2015-03-31/functions/{}/invocations", arn->arn()));
  if (mode == InvocationMode::Synchronous) {
    headers.setReference(LambdaFilterNames::get().InvocationTypeHeader, "RequestResponse");
  } else {
    headers.setReference(LambdaFilterNames::get().InvocationTypeHeader, "Event");
  }
  if (!host_rewrite.empty()) {
    headers.setHost(host_rewrite);
  }
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

bool isContentTypeTextual(const Http::RequestOrResponseHeaderMap& headers) {
  // If transfer-encoding is anything other than 'identity' (i.e. chunked, compress, deflate or
  // gzip) then we want to base64-encode the response body regardless of the content-type value.
  if (auto encoding_header = headers.TransferEncoding()) {
    if (!absl::EqualsIgnoreCase(encoding_header->value().getStringView(),
                                Http::Headers::get().TransferEncodingValues.Identity)) {
      return false;
    }
  }

  // If we don't know the content-type, then we can't make any assumptions.
  if (!headers.ContentType()) {
    return false;
  }

  const Http::LowerCaseString content_type_value{std::string(headers.getContentTypeValue())};
  if (content_type_value.get() == Http::Headers::get().ContentTypeValues.Json) {
    return true;
  }

  if (content_type_value.get() == "application/javascript") {
    return true;
  }

  if (content_type_value.get() == "application/xml") {
    return true;
  }

  if (absl::StartsWith(content_type_value.get(), "text/")) {
    return true;
  }

  return false;
}

} // namespace

// TODO(nbaws) Implement Sigv4a support
Filter::Filter(const FilterSettingsSharedPtr& settings, const FilterStats& stats, bool is_upstream)
    : settings_(settings), stats_(stats), is_upstream_(is_upstream) {}

FilterSettings& Filter::getSettings() {
  auto* settings = const_cast<FilterSettings*>(
      Http::Utility::resolveMostSpecificPerFilterConfig<FilterSettings>(decoder_callbacks_));
  if (settings) {
    return *settings;
  }
  return *settings_;
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  if (!is_upstream_) {
    auto cluster_info_ptr = decoder_callbacks_->clusterInfo();
    if (!cluster_info_ptr || !isTargetClusterLambdaGateway(*cluster_info_ptr)) {
      skip_ = true;
      ENVOY_LOG(trace, "Target cluster does not have the Lambda metadata. Moving on.");
      return Http::FilterHeadersStatus::Continue;
    }
  }

  auto& settings = getSettings();

  if (!end_stream) {
    request_headers_ = &headers;
    return Http::FilterHeadersStatus::StopIteration;
  }

  if (settings.payloadPassthrough()) {
    setLambdaHeaders(headers, settings.arn(), settings.invocationMode(), settings.hostRewrite());
    auto status = settings.signer().signEmptyPayload(headers, settings.arn().region());
    if (!status.ok()) {
      ENVOY_LOG(debug, "signing failed: {}", status.message());
    }

    return Http::FilterHeadersStatus::Continue;
  }

  Buffer::OwnedImpl json_buf;
  jsonizeRequest(headers, nullptr, json_buf);
  // We must call setLambdaHeaders *after* the JSON transformation of the request. That way we
  // reflect the actual incoming request headers instead of the overwritten ones.
  setLambdaHeaders(headers, settings.arn(), settings.invocationMode(), settings.hostRewrite());
  headers.setContentLength(json_buf.length());
  headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  auto& hashing_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto hash = Hex::encode(hashing_util.getSha256Digest(json_buf));

  auto status = settings.signer().sign(headers, hash, settings.arn().region());
  if (!status.ok()) {
    ENVOY_LOG(debug, "signing failed: {}", status.message());
  }
  decoder_callbacks_->addDecodedData(json_buf, false);
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool end_stream) {
  if (skip_ || end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Check for errors returned by Lambda.
  // If we detect an error, we skip the encodeData step to hand the error back to the user as is.
  // Errors can be in the form of HTTP status code or x-amz-function-error header
  const auto http_status = Http::Utility::getResponseStatus(headers);
  if (http_status >= 300) {
    skip_ = true;
    return Http::FilterHeadersStatus::Continue;
  }

  // Just the existence of this header means we have an error, so skip.
  if (!headers.get(LambdaFilterNames::get().FunctionErrorHeader).empty()) {
    skip_ = true;
    return Http::FilterHeadersStatus::Continue;
  }

  response_headers_ = &headers;
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (skip_) {
    return Http::FilterDataStatus::Continue;
  }

  if (!end_stream) {
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

  auto& hashing_util = Envoy::Common::Crypto::UtilitySingleton::get();
  decoder_callbacks_->addDecodedData(data, false);

  const Buffer::Instance& decoding_buffer = *decoder_callbacks_->decodingBuffer();

  auto& settings = getSettings();

  if (!settings.payloadPassthrough()) {
    decoder_callbacks_->modifyDecodingBuffer([this](Buffer::Instance& dec_buf) {
      Buffer::OwnedImpl json_buf;
      jsonizeRequest(*request_headers_, &dec_buf, json_buf);
      // effectively swap(data, json_buf)
      dec_buf.drain(dec_buf.length());
      dec_buf.move(json_buf);
    });
    request_headers_->setContentLength(decoding_buffer.length());
    request_headers_->setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  }

  setLambdaHeaders(*request_headers_, settings.arn(), settings.invocationMode(),
                   settings.hostRewrite());
  const auto hash = Hex::encode(hashing_util.getSha256Digest(decoding_buffer));

  auto status = settings.signer().sign(*request_headers_, hash, settings.arn().region());
  if (!status.ok()) {
    ENVOY_LOG(debug, "signing failed: {}", status.message());
  }
  stats().upstream_rq_payload_size_.recordValue(decoding_buffer.length());
  return Http::FilterDataStatus::Continue;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  auto& settings = getSettings();
  if (skip_ || settings.payloadPassthrough() ||
      settings.invocationMode() == InvocationMode::Asynchronous) {
    return Http::FilterDataStatus::Continue;
  }

  if (!end_stream) {
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

  ENVOY_LOG(trace, "Tranforming JSON payload to HTTP response.");
  encoder_callbacks_->addEncodedData(data, false);
  const Buffer::Instance& encoding_buffer = *encoder_callbacks_->encodingBuffer();
  encoder_callbacks_->modifyEncodingBuffer([this](Buffer::Instance& enc_buf) {
    Buffer::OwnedImpl body;
    dejsonizeResponse(*response_headers_, enc_buf, body);
    enc_buf.drain(enc_buf.length());
    enc_buf.move(body);
  });
  response_headers_->setContentLength(encoding_buffer.length());
  return Http::FilterDataStatus::Continue;
}

void Filter::jsonizeRequest(Http::RequestHeaderMap const& headers, const Buffer::Instance* body,
                            Buffer::Instance& out) const {
  using source::extensions::filters::http::aws_lambda::Request;
  Request json_req;
  if (headers.Path()) {
    json_req.set_raw_path(std::string(headers.getPathValue()));
  }

  if (headers.Method()) {
    json_req.set_method(std::string(headers.getMethodValue()));
  }

  // Wrap the headers
  headers.iterate([&json_req](const Http::HeaderEntry& entry) -> Http::HeaderMap::Iterate {
    // ignore H2 pseudo-headers
    if (absl::StartsWith(entry.key().getStringView(), ":")) {
      return Http::HeaderMap::Iterate::Continue;
    }
    std::string name = std::string(entry.key().getStringView());
    auto it = json_req.mutable_headers()->find(name);
    if (it == json_req.headers().end()) {
      json_req.mutable_headers()->insert({name, std::string(entry.value().getStringView())});
    } else {
      // Coalesce headers with multiple values
      it->second += fmt::format(",{}", entry.value().getStringView());
    }
    return Http::HeaderMap::Iterate::Continue;
  });

  // Wrap the Query String
  if (headers.Path()) {
    auto queryParams = Http::Utility::QueryParamsMulti::parseQueryString(headers.getPathValue());
    for (const auto& kv_pair : queryParams.data()) {
      json_req.mutable_query_string_parameters()->insert({kv_pair.first, kv_pair.second[0]});
    }
  }

  // Wrap the body
  if (body) {
    if (isContentTypeTextual(headers)) {
      json_req.set_body(body->toString());
      json_req.set_is_base64_encoded(false);
    } else {
      json_req.set_body(Base64::encode(*body, body->length()));
      json_req.set_is_base64_encoded(true);
    }
  }

  MessageUtil::validate(json_req, ProtobufMessage::getStrictValidationVisitor());
  const std::string json_data = MessageUtil::getJsonStringFromMessageOrError(
      json_req, false /* pretty_print  */, true /* always_print_primitive_fields */);
  out.add(json_data);
}

void Filter::dejsonizeResponse(Http::ResponseHeaderMap& headers, const Buffer::Instance& json_buf,
                               Buffer::Instance& body) {
  using source::extensions::filters::http::aws_lambda::Response;
  Response json_resp;
  TRY_NEEDS_AUDIT {
    MessageUtil::loadFromJson(json_buf.toString(), json_resp,
                              ProtobufMessage::getNullValidationVisitor());
  }
  END_TRY catch (EnvoyException& ex) {
    // We would only get here if all of the following are true:
    // 1- Passthrough is set to false
    // 2- Lambda returned a 200 OK
    // 3- There was no x-amz-function-error header
    // 4- The body contains invalid JSON
    headers.setStatus(static_cast<int>(Http::Code::InternalServerError));
    stats().server_error_.inc();
    return;
  }

  // Use JSON as the default content-type. If the response headers have a different content-type
  // set, that will be used instead.
  headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);

  for (auto&& kv : json_resp.headers()) {
    // ignore H2 pseudo-headers (if any)
    if (kv.first[0] == ':') {
      continue;
    }
    headers.setCopy(Http::LowerCaseString(kv.first), kv.second);
  }

  for (auto&& cookie : json_resp.cookies()) {
    headers.addReferenceKey(Http::Headers::get().SetCookie, cookie);
  }

  if (json_resp.status_code() != 0) {
    headers.setStatus(json_resp.status_code());
  }
  if (!json_resp.body().empty()) {
    if (json_resp.is_base64_encoded()) {
      body.add(Base64::decode(json_resp.body()));
    } else {
      body.add(json_resp.body());
    }
  }
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
    return Arn{arn, partition, service, region, account_id, resource_type, versioned_function_name};
  }

  return Arn{arn, partition, service, region, account_id, resource_type, function_name};
}

FilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + "aws_lambda.";
  return {ALL_AWS_LAMBDA_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                      POOL_HISTOGRAM_PREFIX(scope, final_prefix))};
}

} // namespace AwsLambdaFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
