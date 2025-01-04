#include "source/extensions/filters/http/aws_request_signing/aws_request_signing_filter.h"

#include <memory>

#include "envoy/extensions/filters/http/aws_request_signing/v3/aws_request_signing.pb.h"

#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsRequestSigningFilter {

FilterConfigImpl::FilterConfigImpl(Extensions::Common::Aws::SignerPtr&& signer,
                                   Common::Aws::CredentialsProviderSharedPtr credentials_provider,
                                   const std::string& stats_prefix, Stats::Scope& scope,
                                   const std::string& host_rewrite, bool use_unsigned_payload)
    : signer_(std::move(signer)), credentials_provider_(credentials_provider),
      stats_(Filter::generateStats(stats_prefix, scope)), host_rewrite_(host_rewrite),
      use_unsigned_payload_(use_unsigned_payload) {}

Filter::Filter(const std::shared_ptr<FilterConfig>& config) : config_(config) {}

Extensions::Common::Aws::Signer& FilterConfigImpl::signer() { return *signer_; }

Envoy::Extensions::Common::Aws::CredentialsProviderSharedPtr
FilterConfigImpl::credentialsProvider() {
  return credentials_provider_;
}

FilterStats& FilterConfigImpl::stats() { return stats_; }

const std::string& FilterConfigImpl::hostRewrite() const { return host_rewrite_; }
bool FilterConfigImpl::useUnsignedPayload() const { return use_unsigned_payload_; }

FilterStats Filter::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + "aws_request_signing.";
  return {ALL_AWS_REQUEST_SIGNING_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

Http::FilterHeadersStatus
Filter::decodeHeadersCredentialsAvailable(Envoy::Extensions::Common::Aws::Credentials credentials) {
  ENVOY_LOG(debug, "aws request signing decodeHeadersCredentialsAvailable, {}",
            credentials.accessKeyId().value());
  auto& config = getConfig();

  const auto& host_rewrite = config.hostRewrite();
  const bool use_unsigned_payload = config.useUnsignedPayload();

  absl::Status status;

  if (!host_rewrite.empty()) {
    request_headers_->setHost(host_rewrite);
  }

  ENVOY_LOG(debug, "aws request signing from decodeHeaders use_unsigned_payload: {}",
            use_unsigned_payload);
  if (use_unsigned_payload) {
    status = config.signer().signUnsignedPayload(*request_headers_, credentials);
  } else {
    status = config.signer().signEmptyPayload(*request_headers_, credentials);
  }
  if (status.ok()) {
    config.stats().signing_added_.inc();
  } else {
    ENVOY_LOG(debug, "signing failed: {}", status.message());
    config.stats().signing_failed_.inc();
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) {
  auto& config = getConfig();

  request_headers_ = &headers;

  if (!config.useUnsignedPayload() && !end_stream) {
    return Http::FilterHeadersStatus::StopIteration;
  }

  // If we are pending credentials, send the decodeHeadersCredentialsAvailable callback for when
  // they become available, and stop iteration.
  auto completion_cb = Envoy::CancelWrapper::cancelWrapped(
      [this](Envoy::Extensions::Common::Aws::Credentials credentials) {
        decodeHeadersCredentialsAvailable(credentials);
      },
      &cancel_callback_);

  if (config.credentialsProvider()->credentialsPending(
          [&dispatcher = decoder_callbacks_->dispatcher(),
           completion_cb = std::move(completion_cb)](
              Envoy::Extensions::Common::Aws::Credentials credentials) mutable {
            dispatcher.post([creds = std::move(credentials),
                             cb = std::move(completion_cb)]() mutable { cb(creds); });
          })) {
    ENVOY_LOG_MISC(debug, "Credentials are pending");
    return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
  } else {
    ENVOY_LOG_MISC(debug, "Credentials are not pending");
  }
  return decodeHeadersCredentialsAvailable(config.credentialsProvider()->getCredentials());
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

  // If we are pending credentials, send the decodeDataCredentialsAvailable callback for when they
  // become available, and stop iteration.
  if (config.credentialsProvider()->credentialsPending(Envoy::CancelWrapper::cancelWrapped(

          [this, &dispatcher = decoder_callbacks_->dispatcher()](
              Envoy::Extensions::Common::Aws::Credentials credentials) {
            dispatcher.post(
                [this, credentials]() { this->decodeDataCredentialsAvailable(credentials); });
          },
          &cancel_callback_))) {
    ENVOY_LOG_MISC(debug, "Credentials are pending");
    return Http::FilterDataStatus::StopIterationAndBuffer;
  } else {
    ENVOY_LOG_MISC(debug, "Credentials are not pending");
  }
  return decodeDataCredentialsAvailable(config.credentialsProvider()->getCredentials());
}

Http::FilterDataStatus
Filter::decodeDataCredentialsAvailable(Envoy::Extensions::Common::Aws::Credentials credentials) {

  ENVOY_LOG(debug, "aws request signing decodeHeadersCredentialsAvailable, {}",
            credentials.accessKeyId().value());

  const Buffer::Instance& decoding_buffer = *decoder_callbacks_->decodingBuffer();
  auto& config = getConfig();

  auto& hashing_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const std::string hash = Hex::encode(hashing_util.getSha256Digest(decoding_buffer));

  ENVOY_LOG(debug, "aws request signing from decodeData");
  ASSERT(request_headers_ != nullptr);
  auto status = config.signer().sign(*request_headers_, credentials, hash);
  if (status.ok()) {
    config.stats().signing_added_.inc();
    config.stats().payload_signing_added_.inc();
  } else {
    ENVOY_LOG(debug, "signing failed: {}", status.message());
    config.stats().signing_failed_.inc();
    config.stats().payload_signing_failed_.inc();
  }

  return Http::FilterDataStatus::Continue;
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
