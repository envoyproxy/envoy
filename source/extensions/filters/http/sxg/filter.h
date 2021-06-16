#pragma once

#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "envoy/extensions/filters/http/sxg/v3alpha/sxg.pb.h"

#include "source/common/config/datasource.h"

#include "libsxg.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SXG {

#define ALL_SXG_STATS(COUNTER)                                                                     \
  COUNTER(total_client_can_accept_sxg)                                                             \
  COUNTER(total_should_sign)                                                                       \
  COUNTER(total_exceeded_max_payload_size)                                                         \
  COUNTER(total_signed_attempts)                                                                   \
  COUNTER(total_signed_succeeded)                                                                  \
  COUNTER(total_signed_failed)

struct SignedExchangeStats {
  ALL_SXG_STATS(GENERATE_COUNTER_STRUCT)
};

// Helper class used to fetch secrets (usually from SDS).
class SecretReader {
public:
  virtual ~SecretReader() = default;
  virtual const std::string& certificate() const PURE;
  virtual const std::string& privateKey() const PURE;
};

class SDSSecretReader : public SecretReader {
public:
  SDSSecretReader(Secret::GenericSecretConfigProviderSharedPtr certificate_provider,
                  Secret::GenericSecretConfigProviderSharedPtr private_key_provider, Api::Api& api)
      : update_callback_client_(readAndWatchSecret(certificate_, certificate_provider, api)),
        update_callback_token_(readAndWatchSecret(private_key_, private_key_provider, api)) {}

  // SecretReader
  const std::string& certificate() const override { return certificate_; }
  const std::string& privateKey() const override { return private_key_; }

private:
  Envoy::Common::CallbackHandlePtr
  readAndWatchSecret(std::string& value,
                     Secret::GenericSecretConfigProviderSharedPtr& secret_provider, Api::Api& api) {
    const auto* secret = secret_provider->secret();
    if (secret != nullptr) {
      value = Config::DataSource::read(secret->secret(), true, api);
    }

    return secret_provider->addUpdateCallback([secret_provider, &api, &value]() {
      const auto* secret = secret_provider->secret();
      if (secret != nullptr) {
        value = Config::DataSource::read(secret->secret(), true, api);
      }
    });
  }

  std::string certificate_;
  std::string private_key_;

  Envoy::Common::CallbackHandlePtr update_callback_client_;
  Envoy::Common::CallbackHandlePtr update_callback_token_;
};

class FilterConfig : public Logger::Loggable<Logger::Id::filter> {
public:
  FilterConfig(const envoy::extensions::filters::http::sxg::v3alpha::SXG& proto_config,
               TimeSource& time_source, std::shared_ptr<SecretReader> secret_reader,
               const std::string& stat_prefix, Stats::Scope&);
  ~FilterConfig() = default;

  const SignedExchangeStats stats() { return stats_; };

  long duration() const { return duration_; };
  long miRecordSize() const { return mi_record_size_; };
  const std::string& cborUrl() const { return cbor_url_; };
  const std::string& validityUrl() const { return validity_url_; };
  TimeSource& timeSource() { return time_source_; };
  const Http::LowerCaseString& clientCanAcceptSXGHeader() { return client_can_accept_sxg_header_; }
  const Http::LowerCaseString& shouldEncodeSXGHeader() { return should_encode_sxg_header_; }
  const std::vector<std::string>& headerPrefixFilters() { return header_prefix_filters_; }

  const std::string certificate() const { return secret_reader_->certificate(); }
  const std::string privateKey() const { return secret_reader_->privateKey(); }

private:
  static SignedExchangeStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return SignedExchangeStats{ALL_SXG_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
  }

  SignedExchangeStats stats_;

  const long duration_;
  const std::string cbor_url_;
  const std::string validity_url_;
  const long mi_record_size_;
  const Http::LowerCaseString client_can_accept_sxg_header_;
  const Http::LowerCaseString should_encode_sxg_header_;
  const std::vector<std::string> header_prefix_filters_;

  TimeSource& time_source_;
  const std::shared_ptr<SecretReader> secret_reader_;
  const std::string certificate_identifier_;
  const std::string private_key_identifier_;
};

typedef std::shared_ptr<FilterConfig> FilterConfigSharedPtr;

/**
 * Helper type to facilitate comparing an absl::string_view key to a std::string.
 */
struct StringCmp {
  using is_transparent = void;
  bool operator()(absl::string_view a, absl::string_view b) const { return a < b; }
};

/**
 * Transaction flow:
 * 1. check accept request header for whether client can accept sxg
 * 2. check x-pinterest-should-encode-sxg from response headers
 * 3. if both true, buffer response body until stream end and then run through the libsxg encoder
 * thingy
 *
 */
class Filter : public Http::StreamFilter, public Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const std::shared_ptr<FilterConfig>& config) : config_(config) {}
  ~Filter() = default;

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks&) override;

  // Http::StreamEncodeFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::ResponseHeaderMap&) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& metadata_map) override;
  virtual void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;

private:
  friend class FilterTest;

  bool client_accept_sxg_{false};
  bool should_encode_sxg_{false};
  std::string origin_;
  std::string url_;
  std::shared_ptr<FilterConfig> config_;
  Http::ResponseHeaderMap* response_headers_;
  uint64_t data_total_{0};
  bool finished_{false};

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_;

  bool loadSigner(const uint64_t, sxg_signer_list_t*);
  bool loadContent(Buffer::Instance& data, sxg_buffer_t* buf);
  bool getEncodedResponse(Buffer::Instance& data, sxg_encoded_response_t* encoded);
  bool writeSxg(const sxg_signer_list_t* signers, const std::string url,
                const sxg_encoded_response_t* encoded, sxg_buffer_t* output);

  void doSxg();

  X509* loadX09Cert();
  EVP_PKEY* loadPrivateKey();
  const std::string toAbsolute(const std::string& url_or_relative_path) const;
  const absl::string_view urlStripQueryFragment(absl::string_view path) const;
  const std::string generateCertDigest(X509* cert) const;
  const std::string getCborUrl(const std::string& cert_digest) const;
  const std::string getValidityUrl() const;
  uint64_t getTimestamp();

  bool clientAcceptSXG(const Http::RequestHeaderMap& headers);
  bool shouldEncodeSXG(const Http::ResponseHeaderMap& headers);
  bool encoderBufferLimitReached(uint64_t buffer_length);
  const Http::LowerCaseString& xCanAcceptSxgKey() const;
  const std::string& xCanAcceptSxgValue() const;
  const Http::LowerCaseString& xShouldEncodeSxgKey() const;
  const std::string& htmlContentType() const;
  const std::string& sxgContentTypeUnversioned() const;
  const std::string& sxgSigLabel() const;
  const std::string& acceptedSxgVersion() const;
  const std::string& sxgContentType() const;

  using HeaderFilterSet = std::set<std::string, StringCmp>;
  const HeaderFilterSet& filteredResponseHeaders() const;
};

} // namespace SXG
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
