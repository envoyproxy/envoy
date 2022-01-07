#include "contrib/sxg/filters/http/source/encoder.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <chrono>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/headers.h"

#include "absl/strings/escaping.h"
#include "contrib/sxg/filters/http/source/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SXG {

EncoderImpl::~EncoderImpl() {
  sxg_header_release(&headers_);
  sxg_raw_response_release(&raw_response_);
  sxg_signer_list_release(&signer_list_);
  sxg_encoded_response_release(&encoded_response_);
}

void EncoderImpl::setOrigin(const std::string origin) { origin_ = origin; };

void EncoderImpl::setUrl(const std::string url) { url_ = url; };

bool EncoderImpl::loadHeaders(Http::ResponseHeaderMap* headers) {
  const auto& filtered_headers = filteredResponseHeaders();
  bool retval = true;
  headers->iterate([this, filtered_headers,
                    &retval](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    const auto& header_key = header.key().getStringView();

    // filter x-envoy-* headers
    if (absl::StartsWith(header_key, ThreadSafeSingleton<Http::PrefixValue>::get().prefix())) {
      return Http::HeaderMap::Iterate::Continue;
    }
    // filter out the header that we use as a flag to trigger encoding
    if (config_->shouldEncodeSXGHeader().get() == header_key) {
      return Http::HeaderMap::Iterate::Continue;
    }
    // filter out other headers by prefix
    for (const auto& prefix_filter : config_->headerPrefixFilters()) {
      if (absl::StartsWith(header_key, prefix_filter)) {
        return Http::HeaderMap::Iterate::Continue;
      }
    }
    // filter out headers that are not allowed to be encoded in the SXG document
    if (filtered_headers.find(header_key) != filtered_headers.end()) {
      return Http::HeaderMap::Iterate::Continue;
    }

    const auto header_value = header.value().getStringView();
    if (!sxg_header_append_string(std::string(header_key).c_str(),
                                  std::string(header_value).c_str(), &headers_)) {
      retval = false;
      return Http::HeaderMap::Iterate::Break;
    }
    return Http::HeaderMap::Iterate::Continue;
  });

  return retval;
}

bool EncoderImpl::loadContent(Buffer::Instance& data) {
  const size_t size = data.length();
  if (!sxg_buffer_resize(size, &raw_response_.payload)) {
    return false;
  }
  data.copyOut(0, size, raw_response_.payload.data);

  return true;
}

constexpr uint64_t ONE_DAY_IN_SECONDS = 86400L;

bool EncoderImpl::loadSigner() {
  // backdate timestamp by 1 day, to account for clock skew
  const uint64_t date = getTimestamp() - ONE_DAY_IN_SECONDS;

  const uint64_t expires = date + static_cast<uint64_t>(config_->duration());
  const auto validity_url = getValidityUrl();

  X509* cert = loadX09Cert();
  const auto cert_digest = generateCertDigest(cert);
  const auto cbor_url = getCborUrl(cert_digest);

  EVP_PKEY* pri_key = loadPrivateKey();

  const auto retval =
      cert && pri_key &&
      sxg_add_ecdsa_signer(sxgSigLabel().c_str(), date, expires, validity_url.c_str(), pri_key,
                           cert, cbor_url.c_str(), &signer_list_);

  if (cert) {
    X509_free(cert);
  }
  if (pri_key) {
    EVP_PKEY_free(pri_key);
  }
  return retval;
}

bool EncoderImpl::getEncodedResponse() {
  // Pass response headers to the response before encoding
  if (!sxg_header_copy(&headers_, &raw_response_.header)) {
    return false;
  }
  if (!sxg_encode_response(config_->miRecordSize(), &raw_response_, &encoded_response_)) {
    return false;
  }
  return true;
}

Buffer::BufferFragment* EncoderImpl::writeSxg() {
  sxg_buffer_t result = sxg_empty_buffer();
  if (!sxg_generate(url_.c_str(), &signer_list_, &encoded_response_, &result)) {
    sxg_buffer_release(&result);
    return nullptr;
  }

  return new Buffer::BufferFragmentImpl(
      result.data, result.size,
      [result](const void*, size_t, const Buffer::BufferFragmentImpl* this_fragment) {
        // Capture of result by value passes a const, but sxg_buffer_release does not accept
        // a const buffer_t*, so we have to cast it back. This is OK since the important
        // operation performed by sxg_buffer_release is to release the data buffer.
        sxg_buffer_release(const_cast<sxg_buffer_t*>(&result));
        delete this_fragment;
      });
}

uint64_t EncoderImpl::getTimestamp() {
  const auto now = config_->timeSource().systemTime();
  const auto ts = std::abs(static_cast<int>(
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()));

  return ts;
}

const std::string EncoderImpl::toAbsolute(const std::string& url_or_relative_path) const {
  if (!url_or_relative_path.empty() && url_or_relative_path[0] == '/') {
    return origin_ + url_or_relative_path;
  } else {
    return url_or_relative_path;
  }
}

const std::string EncoderImpl::getValidityUrl() const { return toAbsolute(config_->validityUrl()); }

const std::string EncoderImpl::getCborUrl(const std::string& cert_digest) const {
  return fmt::format("{}?d={}", toAbsolute(config_->cborUrl()), cert_digest);
}

X509* EncoderImpl::loadX09Cert() {
  X509* cert = nullptr;
  BIO* bio = BIO_new(BIO_s_mem());
  RELEASE_ASSERT(bio != nullptr, "");

  if (BIO_puts(bio, config_->certificate().c_str()) >= 0) {
    cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  }

  BIO_vfree(bio);
  return cert;
}

EVP_PKEY* EncoderImpl::loadPrivateKey() {
  EVP_PKEY* pri_key = nullptr;
  BIO* bio = BIO_new(BIO_s_mem());
  RELEASE_ASSERT(bio != nullptr, "");

  if (BIO_puts(bio, config_->privateKey().c_str()) >= 0) {
    pri_key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
  }

  BIO_vfree(bio);
  return pri_key;
}

const uint8_t CERT_DIGEST_BYTES = 8;

const std::string EncoderImpl::generateCertDigest(X509* cert) const {
  uint8_t out[EVP_MAX_MD_SIZE];
  unsigned out_len;
  if (!(X509_digest(cert, EVP_sha256(), out, &out_len) && out_len >= CERT_DIGEST_BYTES)) {
    return "";
  }

  return absl::BytesToHexString(
      absl::string_view(reinterpret_cast<const char*>(out), CERT_DIGEST_BYTES));
}

const std::string& EncoderImpl::sxgSigLabel() const {
  // this is currently ignored, so an arbitrary string is safe to use
  CONSTRUCT_ON_FIRST_USE(std::string, "label");
}

const EncoderImpl::HeaderFilterSet& EncoderImpl::filteredResponseHeaders() const {
  CONSTRUCT_ON_FIRST_USE(
      HeaderFilterSet,
      {
          // handled by libsxg, or explicitly by this filter
          ":status",
          // hop-by-hop headers, see:
          // https://tools.ietf.org/id/draft-yasskin-http-origin-signed-responses-05.html#uncached-headers
          "connection",
          "keep-alive",
          "proxy-connection",
          "trailer",
          "transfer-encoding",
          "upgrade",
          // Stateful headers, see:
          // https://tools.ietf.org/id/draft-yasskin-http-origin-signed-responses-05.html#stateful-headers
          // and blocked in http://crrev.com/c/958945.
          "authentication-control",
          "authentication-info",
          "clear-site-data",
          "optional-www-authenticate",
          "proxy-authenticate",
          "proxy-authentication-info",
          "public-key-pins",
          "sec-websocket-accept",
          "set-cookie",
          "set-cookie2",
          "setprofile",
          "strict-transport-security",
          "www-authenticate",
          // other stateful headers
          "vary",
          "cache-control",
      });
}

} // namespace SXG
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
