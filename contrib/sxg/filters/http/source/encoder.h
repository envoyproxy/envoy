#pragma once

#include "source/common/buffer/buffer_impl.h"
#include "source/common/config/datasource.h"

#include "contrib/sxg/filters/http/source/filter_config.h"
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
  virtual bool loadHeaders(Http::ResponseHeaderMap* headers) PURE;
  virtual bool loadContent(Buffer::Instance& data) PURE;
  virtual bool getEncodedResponse() PURE;
  virtual Buffer::BufferFragment* writeSxg() PURE;
};

using EncoderPtr = std::unique_ptr<Encoder>;

class EncoderImpl : public Encoder, Logger::Loggable<Logger::Id::filter> {
public:
  explicit EncoderImpl(const FilterConfigSharedPtr& config)
      : headers_(sxg_empty_header()), raw_response_(sxg_empty_raw_response()),
        signer_list_(sxg_empty_signer_list()), encoded_response_(sxg_empty_encoded_response()),
        config_(config) {}

  ~EncoderImpl() override;

  // Filter::Encoder
  void setOrigin(const std::string origin) override;
  void setUrl(const std::string url) override;
  bool loadHeaders(Http::ResponseHeaderMap* headers) override;
  bool loadSigner() override;
  bool loadContent(Buffer::Instance& data) override;
  bool getEncodedResponse() override;
  Buffer::BufferFragment* writeSxg() override;

private:
  friend class EncoderTest;

  sxg_header_t headers_;
  sxg_raw_response_t raw_response_;
  sxg_signer_list_t signer_list_;
  sxg_encoded_response_t encoded_response_;
  FilterConfigSharedPtr config_;
  std::string origin_;
  std::string url_;

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
