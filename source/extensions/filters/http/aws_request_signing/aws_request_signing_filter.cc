#include "source/extensions/filters/http/aws_request_signing/aws_request_signing_filter.h"

#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.h"

#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsRequestSigningFilter {

FilterConfigImpl::FilterConfigImpl(Extensions::Common::Aws::SignerPtr&& signer,
                                   const std::string& stats_prefix, Stats::Scope& scope,
                                   const std::string& host_rewrite, bool use_unsigned_payload)
    : signer_(std::move(signer)), stats_(Filter::generateStats(stats_prefix, scope)),
      host_rewrite_(host_rewrite), use_unsigned_payload_{use_unsigned_payload} {}

Filter::Filter(const std::shared_ptr<FilterConfig>& config) : config_(config) {}

Extensions::Common::Aws::Signer& FilterConfigImpl::signer() { return *signer_; }

FilterStats& FilterConfigImpl::stats() { return stats_; }

const std::string& FilterConfigImpl::hostRewrite() const { return host_rewrite_; }
bool FilterConfigImpl::useUnsignedPayload() const { return use_unsigned_payload_; }

FilterStats Filter::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + "aws_request_signing.";
  return {ALL_AWS_REQUEST_SIGNING_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  const auto& host_rewrite = config_->hostRewrite();
  const bool use_unsigned_payload = config_->useUnsignedPayload();

  if (!host_rewrite.empty()) {
    headers.setHost(host_rewrite);
  }

  if (!use_unsigned_payload && !end_stream) {
    request_headers_ = &headers;
    return Http::FilterHeadersStatus::StopIteration;
  }

  try {
    ENVOY_LOG(debug, "aws request signing from decodeHeaders use_unsigned_payload: {}",
              use_unsigned_payload);
    if (use_unsigned_payload) {
      config_->signer().signUnsignedPayload(headers);
    } else {
      config_->signer().signEmptyPayload(headers);
    }
    config_->stats().signing_added_.inc();
  } catch (const EnvoyException& e) {
    // TODO: sign should not throw to avoid exceptions in the request path
    ENVOY_LOG(debug, "signing failed: {}", e.what());
    config_->stats().signing_failed_.inc();
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (config_->useUnsignedPayload()) {
    return Http::FilterDataStatus::Continue;
  }

  if (!end_stream) {
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

  decoder_callbacks_->addDecodedData(data, false);

  const Buffer::Instance& decoding_buffer = *decoder_callbacks_->decodingBuffer();

  auto& hashing_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const std::string hash = Hex::encode(hashing_util.getSha256Digest(decoding_buffer));

  try {
    ENVOY_LOG(debug, "aws request signing from decodeData");
    ASSERT(request_headers_ != nullptr);
    config_->signer().sign(*request_headers_, hash);
    config_->stats().signing_added_.inc();
    config_->stats().payload_signing_added_.inc();
  } catch (const EnvoyException& e) {
    // TODO: sign should not throw to avoid exceptions in the request path
    ENVOY_LOG(debug, "signing failed: {}", e.what());
    config_->stats().signing_failed_.inc();
    config_->stats().payload_signing_failed_.inc();
  }

  return Http::FilterDataStatus::Continue;
}

} // namespace AwsRequestSigningFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
