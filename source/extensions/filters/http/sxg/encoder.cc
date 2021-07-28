#include "source/extensions/filters/http/sxg/encoder.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <chrono>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/headers.h"

#include "absl/strings/escaping.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SXG {

EncoderImpl::~EncoderImpl() {
  sxg_signer_list_release(&signer_list_);
  sxg_header_release(&headers_);
  sxg_encoded_response_release(&encoded_response_);
}

void EncoderImpl::setOrigin(const std::string origin) { origin_ = origin; };

void EncoderImpl::setUrl(const std::string url) { url_ = url; };

bool EncoderImpl::loadHeaders(Http::ResponseHeaderMap& headers) {
  // Pass response headers to the signed doc.
  const auto& filtered_headers = filteredResponseHeaders();
  bool retval = true;
  headers.iterate([this, filtered_headers,
                   &retval](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    const auto& header_key = header.key().getStringView();

    // filter out all headers that should not be encoded in the SXG document
    if (absl::StartsWith(header_key, ThreadSafeSingleton<Http::PrefixValue>::get().prefix())) {
      return Http::HeaderMap::Iterate::Continue;
    }
    if (should_encode_sxg_header_.get() == header_key) {
      return Http::HeaderMap::Iterate::Continue;
    }
    for (const auto& prefix_filter : header_prefix_filters_) {
      if (absl::StartsWith(header_key, prefix_filter)) {
        return Http::HeaderMap::Iterate::Continue;
      }
    }
    if (filtered_headers.find(header_key) != filtered_headers.end()) {
      return Http::HeaderMap::Iterate::Continue;
    }

    const auto header_value = header.value().getStringView();
    if (!sxg_header_append_string(std::string(header_key).c_str(),
                                  std::string(header_value).c_str(),
                                  const_cast<sxg_header_t*>(&headers_))) {
      retval = false;
      return Http::HeaderMap::Iterate::Break;
    }
    return Http::HeaderMap::Iterate::Continue;
  });

  return retval;
}

bool EncoderImpl::loadContent(Buffer::Instance& data, sxg_buffer_t* buf) {
  const size_t size = data.length();
  if (!sxg_buffer_resize(size, buf)) {
    return false;
  }

  data.copyOut(0, size, buf->data);

  return true;
}

constexpr uint64_t ONE_DAY_IN_SECONDS = 86400L;

bool EncoderImpl::loadSigner() {
  // backdate timestamp by 1 day, to account for clock skew
  const uint64_t date = getTimestamp() - ONE_DAY_IN_SECONDS;

  const uint64_t expires = date + static_cast<uint64_t>(duration_);
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

bool EncoderImpl::getEncodedResponse(Buffer::Instance& data) {
  sxg_raw_response_t raw = sxg_empty_raw_response();
  bool retval = true;
  if (!loadContent(data, &raw.payload)) {
    retval = false;
  }
  if (retval && !sxg_header_copy(&headers_, &raw.header)) {
    retval = false;
  }
  if (retval && !sxg_encode_response(mi_record_size_, &raw, &encoded_response_)) {
    retval = false;
  }
  sxg_raw_response_release(&raw);
  return retval;
}

Buffer::BufferFragment* EncoderImpl::writeSxg() {
  sxg_buffer_t result = sxg_empty_buffer();
  if (!sxg_generate(url_.c_str(), &signer_list_, &encoded_response_, &result)) {
    sxg_buffer_release(&result);
    return nullptr;
  }

  return new Buffer::BufferFragmentImpl(
      result.data, result.size, [result](const void*, size_t, const Buffer::BufferFragmentImpl*) {
        sxg_buffer_release(const_cast<sxg_buffer_t*>(&result));
      });
}

uint64_t EncoderImpl::getTimestamp() {
  const auto now = time_source_.systemTime();
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

const std::string EncoderImpl::getValidityUrl() const { return toAbsolute(validity_url_); }

const std::string EncoderImpl::getCborUrl(const std::string& cert_digest) const {
  return fmt::format("{}?d={}", toAbsolute(cbor_url_), cert_digest);
}

X509* EncoderImpl::loadX09Cert() {
  BIO* bio = BIO_new(BIO_s_mem());
  RELEASE_ASSERT(bio != nullptr, "");
  if (BIO_puts(bio, certificate_.c_str()) < 0) {
    BIO_vfree(bio);
    return nullptr;
  }

  X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_vfree(bio);

  return cert;
}

EVP_PKEY* EncoderImpl::loadPrivateKey() {
  BIO* bio = BIO_new(BIO_s_mem());
  RELEASE_ASSERT(bio != nullptr, "");
  if (BIO_puts(bio, private_key_.c_str()) < 0) {
    BIO_vfree(bio);
    return nullptr;
  }

  EVP_PKEY* pri_key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
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
