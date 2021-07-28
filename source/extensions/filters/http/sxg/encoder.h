#pragma once

#include "source/common/buffer/buffer_impl.h"
#include "source/common/config/datasource.h"

#include "libsxg.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SXG {

/**
 * Helper type to facilitate comparing an absl::string_view key to a std::string.
 */
struct StringCmp {
  using IsTransparent = void;
  bool operator()(absl::string_view a, absl::string_view b) const { return a < b; }
};

class Encoder {
public:
  virtual ~Encoder() = default;

  virtual void setOrigin(const std::string origin) PURE;
  virtual void setUrl(const std::string url) PURE;
  virtual bool loadSigner() PURE;
  virtual bool loadHeaders(Http::ResponseHeaderMap& headers) PURE;
  virtual bool loadContent(Buffer::Instance& data, sxg_buffer_t* buf) PURE;
  virtual bool getEncodedResponse(Buffer::Instance& data) PURE;
  virtual Buffer::BufferFragment* writeSxg() PURE;
};

using EncoderPtr = std::unique_ptr<Encoder>;

class EncoderImpl : public Encoder, Logger::Loggable<Logger::Id::filter> {
public:
  explicit EncoderImpl(std::string certificate, std::string private_key, long duration,
                       long mi_record_size, std::string cbor_url, std::string validity_url,
                       const Http::LowerCaseString& should_encode_sxg_header,
                       const std::vector<std::string>& header_prefix_filters,
                       TimeSource& time_source)
      : signer_list_(sxg_empty_signer_list()), headers_(sxg_empty_header()),
        encoded_response_(sxg_empty_encoded_response()), certificate_(certificate),
        private_key_(private_key), duration_(duration), mi_record_size_(mi_record_size),
        cbor_url_(cbor_url), validity_url_(validity_url),
        should_encode_sxg_header_(should_encode_sxg_header),
        header_prefix_filters_(header_prefix_filters), time_source_(time_source) {}

  ~EncoderImpl();

  // Filter::Encoder
  void setOrigin(const std::string origin) override;
  void setUrl(const std::string url) override;
  bool loadHeaders(Http::ResponseHeaderMap& headers) override;
  bool loadSigner() override;
  bool loadContent(Buffer::Instance& data, sxg_buffer_t* buf) override;
  Buffer::BufferFragment* writeSxg() override;
  bool getEncodedResponse(Buffer::Instance& data) override;

private:
  sxg_signer_list_t signer_list_;
  sxg_header_t headers_;
  sxg_encoded_response_t encoded_response_;

  const std::string certificate_;
  const std::string private_key_;
  const long duration_;
  const long mi_record_size_;
  const std::string cbor_url_;
  const std::string validity_url_;
  const Http::LowerCaseString should_encode_sxg_header_;
  const std::vector<std::string> header_prefix_filters_;

  std::string origin_;
  std::string url_;
  TimeSource& time_source_;

  uint64_t getTimestamp();
  const std::string toAbsolute(const std::string& url_or_relative_path) const;
  const std::string getCborUrl(const std::string& cert_digest) const;
  const std::string getValidityUrl() const;

  X509* loadX09Cert();
  EVP_PKEY* loadPrivateKey();
  const std::string& sxgSigLabel() const;
  const std::string generateCertDigest(X509* cert) const;

  using HeaderFilterSet = std::set<absl::string_view, StringCmp>;
  const HeaderFilterSet& filteredResponseHeaders() const;
};

} // namespace SXG
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
