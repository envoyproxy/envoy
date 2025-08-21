#include "source/extensions/filters/http/aws_request_signing/aws_request_signing_filter.h"

#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.h"

#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/utility.h"

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
  auto& config = getConfig();

  const auto& host_rewrite = config.hostRewrite();
  const bool use_unsigned_payload = config.useUnsignedPayload();

  if (!host_rewrite.empty()) {
    headers.setHost(host_rewrite);
  }

  request_headers_ = &headers;

  if (!use_unsigned_payload && !end_stream) {
    return Http::FilterHeadersStatus::StopIteration;
  }

  ENVOY_LOG(debug, "aws request signing from decodeHeaders use_unsigned_payload: {}",
            use_unsigned_payload);

  auto completion_cb = Envoy::CancelWrapper::cancelWrapped(
      [this, &config]() {
        continueDecodeHeaders(config);
        decoder_callbacks_->continueDecoding();
      },
      &cancel_callback_);

  if (config.signer().addCallbackIfCredentialsPending(
          [&dispatcher = decoder_callbacks_->dispatcher(),
           completion_cb = std::move(completion_cb)]() {
            dispatcher.post([cb = std::move(completion_cb)]() mutable { cb(); });
          }) == false) {
    // We're not pending credentials, so sign immediately
    continueDecodeHeaders(config);
    return Http::FilterHeadersStatus::Continue;
  } else {
    // Leave and let our callback handle the rest of the processing
    return Http::FilterHeadersStatus::StopIteration;
  }
}

void Filter::continueDecodeHeaders(FilterConfig& config) {
  absl::Status status;
  if (config.useUnsignedPayload()) {
    status = config.signer().signUnsignedPayload(*request_headers_);

  } else {
    status = config.signer().signEmptyPayload(*request_headers_);
  }
  addSigningStats(config, status);
}

void Filter::addSigningStats(FilterConfig& config, absl::Status status) const {
  if (status.ok()) {
    config.stats().signing_added_.inc();
  } else {
    ENVOY_LOG(debug, "signing failed: {}", status.message());
    config.stats().signing_failed_.inc();
  }
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  auto& config = getConfig();

  if (config.useUnsignedPayload()) {
    return Http::FilterDataStatus::Continue;
  }

  if (!end_stream) {
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

  decoder_callbacks_->addDecodedData(data, false);

  const Buffer::Instance& decoding_buffer = *decoder_callbacks_->decodingBuffer();

  auto& hashing_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const std::string hash = Hex::encode(hashing_util.getSha256Digest(decoding_buffer));

  ENVOY_LOG(debug, "aws request signing from decodeData");
  ASSERT(request_headers_ != nullptr);

  auto completion_cb = Envoy::CancelWrapper::cancelWrapped(
      [this, &config, hash]() {
        continueDecodeData(config, hash);
        decoder_callbacks_->continueDecoding();
      },
      &cancel_callback_);

  if (config.signer().addCallbackIfCredentialsPending(
          [&dispatcher = decoder_callbacks_->dispatcher(),
           completion_cb = std::move(completion_cb)]() {
            dispatcher.post([cb = std::move(completion_cb)]() mutable { cb(); });
          }) == false) {
    // We're not pending credentials, so sign immediately
    continueDecodeData(config, hash);
    return Http::FilterDataStatus::Continue;
  } else {
    // Leave and let our callback handle the rest of the processing
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }
}

void Filter::continueDecodeData(FilterConfig& config, const std::string hash) {
  auto status = config.signer().sign(*request_headers_, hash);

  addSigningStats(config, status);
  addSigningPayloadStats(config, status);
}

void Filter::addSigningPayloadStats(FilterConfig& config, absl::Status status) const {
  if (status.ok()) {
    config.stats().payload_signing_added_.inc();
  } else {
    ENVOY_LOG(debug, "payload signing failed: {}", status.message());
    config.stats().payload_signing_failed_.inc();
  }
}

FilterConfig& Filter::getConfig() const {
  auto* config = const_cast<FilterConfig*>(
      Http::Utility::resolveMostSpecificPerFilterConfig<FilterConfig>(decoder_callbacks_));
  if (config) {
    return *config;
  }
  return *config_;
}

} // namespace AwsRequestSigningFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
