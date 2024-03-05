#pragma once

#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/secret/secret_provider_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "contrib/envoy/extensions/filters/http/sxg/v3alpha/sxg.pb.h"
#include "contrib/envoy/extensions/filters/http/sxg/v3alpha/sxg.pb.validate.h"

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
  SDSSecretReader(Secret::GenericSecretConfigProviderSharedPtr&& certificate_provider,
                  Secret::GenericSecretConfigProviderSharedPtr&& private_key_provider,
                  ThreadLocal::SlotAllocator& tls, Api::Api& api)
      : certificate_(std::move(certificate_provider), tls, api),
        private_key_(std::move(private_key_provider), tls, api) {}
  // SecretReader
  const std::string& certificate() const override { return certificate_.secret(); }
  const std::string& privateKey() const override { return private_key_.secret(); }

private:
  Secret::ThreadLocalGenericSecretProvider certificate_;
  Secret::ThreadLocalGenericSecretProvider private_key_;
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

  const std::string& certificate() const { return secret_reader_->certificate(); }
  const std::string& privateKey() const { return secret_reader_->privateKey(); }

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

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

} // namespace SXG
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
