#include "source/extensions/filters/http/sxg/filter.h"

#include <cstdlib>
#include <regex>
#include <string>
#include <chrono>

#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "envoy/server/filter_config.h"

#include "absl/strings/escaping.h"
#include "envoy/stats/scope.h"
#include "envoy/http/codes.h"
#include "source/common/common/utility.h"
#include "source/common/http/headers.h"
#include "source/common/stats/utility.h"

#include "libsxg.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SXG {

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    accept_handle(Http::CustomHeaders::get().Accept);

template <class T>
const std::vector<std::string> initializeHeaderPrefixFilters(const T& filters_proto) {
  std::vector<std::string> filters;
  filters.reserve(filters_proto.size());

  for (const auto& filter : filters_proto) {
    filters.emplace_back(filter);
  }

  return filters;
}

FilterConfig::FilterConfig(const envoy::extensions::filters::http::sxg::v3alpha::SXG& proto_config,
                           TimeSource& time_source, std::shared_ptr<SecretReader> secret_reader,
                           const std::string& stat_prefix, Stats::Scope& scope)
    : stats_(generateStats(stat_prefix + "sxg.", scope)),
      duration_(proto_config.has_duration() ? proto_config.duration().seconds() : 604800UL),
      cbor_url_(proto_config.cbor_url()), validity_url_(proto_config.validity_url()),
      mi_record_size_(proto_config.mi_record_size() ? proto_config.mi_record_size() : 4096L),
      client_can_accept_sxg_header_((proto_config.client_can_accept_sxg_header().length() > 0)
                                        ? proto_config.client_can_accept_sxg_header()
                                        : "x-envoy-client-can-accept-sxg"),
      should_encode_sxg_header_((proto_config.should_encode_sxg_header().length() > 0)
                                    ? proto_config.should_encode_sxg_header()
                                    : "x-envoy-should-encode-sxg"),
      header_prefix_filters_(initializeHeaderPrefixFilters(proto_config.header_prefix_filters())),
      time_source_(time_source), secret_reader_(secret_reader) {}

const std::regex& matchAcceptHeaderRegex() {
  // RegEx to detect Signed Exchange Accept Headers from:
  // https://web.dev/signed-exchanges/#best-practices
  CONSTRUCT_ON_FIRST_USE(std::regex, 
                         "(?:^|,)\\s*(application/signed-exchange|text/"
                         "html)\\s*(?:;v=([\\w]+))?\\s*(?:;q=([\\d\\.]+))?\\s*",
                         std::regex_constants::icase);
}

void Filter::onDestroy() {
}

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  ENVOY_LOG(debug, "sxg filter from decodeHeaders: {}", headers);
  if (headers.Host() && headers.Path() && clientAcceptSXG(headers)) {
    client_accept_sxg_ = true;
    headers.setReference(xCanAcceptSxgKey(), xCanAcceptSxgValue());
    origin_ = fmt::format("https://{}", headers.getHostValue());
    url_ = fmt::format("{}{}", origin_, headers.getPathValue());
    config_->stats().total_client_can_accept_sxg_.inc();
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

Http::FilterHeadersStatus Filter::encode100ContinueHeaders(Http::ResponseHeaderMap&) {
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  ENVOY_LOG(debug, "sxg filter from Filter::encodeHeaders");

  if (client_accept_sxg_ && shouldEncodeSXG(headers)) {
    should_encode_sxg_ = true;
    response_headers_ = &headers;
    config_->stats().total_should_sign_.inc();
    return Http::FilterHeadersStatus::StopIteration;
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "sxg filter from encodeData end_stream: {}", end_stream);

  if (!should_encode_sxg_) {
    // Nothing to do.
    return Http::FilterDataStatus::Continue;
  }

  data_total_ += data.length();
  if (encoderBufferLimitReached(data_total_)) {
    should_encode_sxg_ = false;
    return Http::FilterDataStatus::Continue;
  }

  encoder_callbacks_->addEncodedData(data, false);

  if (!end_stream) {
    // We need to know the size of the response in order to generate the SXG, so we wait.
    return Http::FilterDataStatus::StopIterationAndBuffer;
  }

  doSxg();
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::encodeTrailers(Http::ResponseTrailerMap&) {
  if (should_encode_sxg_) {
    doSxg();
  }
  return Http::FilterTrailersStatus::Continue;
}

Http::FilterMetadataStatus Filter::encodeMetadata(Http::MetadataMap&) {
  return Http::FilterMetadataStatus::Continue;
}

constexpr uint64_t ONE_DAY_IN_SECONDS = 86400L;

void Filter::doSxg() {
  if (finished_) {
    return;
  }

  finished_ = true;
  encoder_callbacks_->modifyEncodingBuffer([this](Buffer::Instance& enc_buf) {
    config_->stats().total_signed_attempts_.inc();

    sxg_encoded_response_t encoded = sxg_empty_encoded_response();
    if (!getEncodedResponse(enc_buf, &encoded)) {
      sxg_encoded_response_release(&encoded);
      config_->stats().total_signed_failed_.inc();
      return;
    }

    // backdate timestamp by 1 day, to account for clock skew
    const uint64_t date = getTimestamp() - ONE_DAY_IN_SECONDS;

    sxg_signer_list_t signers = sxg_empty_signer_list();
    if (!loadSigner(date, &signers)) {
      sxg_signer_list_release(&signers);
      sxg_encoded_response_release(&encoded);
      config_->stats().total_signed_failed_.inc();
      return;
    }

    sxg_buffer_t result = sxg_empty_buffer();
    if (!writeSxg(&signers, url_, &encoded, &result)) {
      sxg_signer_list_release(&signers);
      sxg_encoded_response_release(&encoded);
      sxg_buffer_release(&result);
      config_->stats().total_signed_failed_.inc();
      return;
    }

    sxg_signer_list_release(&signers);
    sxg_encoded_response_release(&encoded);

    // Make sure that the resulting SXG isn't too big before adding it to the encoding buffer
    if (encoderBufferLimitReached(result.size + 100)) {
      config_->stats().total_signed_failed_.inc();
      sxg_buffer_release(&result);
      return;
    }

    const absl::string_view output(reinterpret_cast<char*>(result.data), result.size);

    enc_buf.drain(enc_buf.length());
    enc_buf.add(output); // TODO(rgs): can we move this and avoid the copy?

    response_headers_->setContentLength(output.length());
    response_headers_->setContentType(sxgContentType());

    sxg_buffer_release(&result);

    config_->stats().total_signed_succeeded_.inc();
  });
}

void Filter::setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

bool Filter::clientAcceptSXG(const Http::RequestHeaderMap& headers) {
  const absl::string_view accept = headers.getInlineValue(accept_handle.handle());

  std::string html_q_value = "0";
  std::string sxg_q_value = "";
  auto begin = std::regex_iterator<absl::string_view::const_iterator>(accept.begin(), accept.end(),
                                                                      matchAcceptHeaderRegex());
  auto end = std::regex_iterator<absl::string_view::const_iterator>();
  // Respond with Signed Exchange if accept header has:
  // a) application/signed-exchange
  // b) with appropriate version (v=b3)
  // c) q-value of signed exchange is >= that of text/html
  // from: https://web.dev/signed-exchanges/#best-practices
  for (auto i = begin; i != end; ++i) {
    std::match_results<absl::string_view::const_iterator> m = *i;
    if (m.size() != 4) {
      continue;
    }
    // m[1]: main content type (text/html, application/signed-exchange)
    // m[2]: v parameter (v=b3 for valid signed exchange version)
    // m[3]: q value (note that this defaults to 1 if not present)
    if (m[1].str().compare(htmlContentType()) == 0) {
      html_q_value = m[3].matched ? m[3].str() : "1";
    } else if (m[1].str().compare(sxgContentTypeUnversioned()) == 0 && m[2].matched &&
               m[2].str().compare(acceptedSxgVersion()) == 0) {
      sxg_q_value = m[3].matched ? m[3].str() : "1";
    }
  }

  return sxg_q_value.compare(html_q_value) >= 0;
}

bool Filter::shouldEncodeSXG(const Http::ResponseHeaderMap& headers) {
  if (!(headers.Status() && headers.getStatusValue() == "200")) {
    return false;
  }

  const auto x_should_encode_sxg_header = headers.get(xShouldEncodeSxgKey());
  return !x_should_encode_sxg_header.empty();
}

const std::string Filter::toAbsolute(const std::string& url_or_relative_path) const {
  if (url_or_relative_path.size() > 0 && url_or_relative_path[0] == '/') {
    return origin_ + url_or_relative_path;
  } else {
    return url_or_relative_path;
  }
}

const uint8_t CERT_DIGEST_BYTES = 8;

const std::string Filter::generateCertDigest(X509* cert) const {
  uint8_t out[EVP_MAX_MD_SIZE];
  unsigned out_len;
  if (!(X509_digest(cert, EVP_sha256(), out, &out_len) && out_len >= CERT_DIGEST_BYTES)) {
    return "";
  }

  return absl::BytesToHexString(
      absl::string_view(reinterpret_cast<const char*>(out), CERT_DIGEST_BYTES));
}

const std::string Filter::getValidityUrl() const { return toAbsolute(config_->validityUrl()); }

const std::string Filter::getCborUrl(const std::string& cert_digest) const {
  return fmt::format("{}?d={}", toAbsolute(config_->cborUrl()), cert_digest);
}

uint64_t Filter::getTimestamp() {
  const auto now = config_->timeSource().systemTime();
  const auto ts = std::abs(static_cast<int>(
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()));

  return ts;
}

X509* Filter::loadX09Cert() {
  const std::string cert_str = config_->certificate();
  BIO* bio = BIO_new(BIO_s_mem());
  if (!bio) {
    return NULL;
  }
  if (BIO_puts(bio, cert_str.c_str()) < 0) {
    BIO_vfree(bio);
    return NULL;
  }

  X509* cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
  BIO_vfree(bio);

  return cert;
}

EVP_PKEY* Filter::loadPrivateKey() {
  const std::string pri_key_str = config_->privateKey();
  BIO* bio = BIO_new(BIO_s_mem());
  if (!bio) {
    return NULL;
  }
  if (BIO_puts(bio, pri_key_str.c_str()) < 0) {
    BIO_vfree(bio);
    return NULL;
  }

  EVP_PKEY* pri_key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
  BIO_vfree(bio);

  return pri_key;
}

bool Filter::loadSigner(uint64_t date, sxg_signer_list_t* signers) {
  const uint64_t expires = date + static_cast<uint64_t>(config_->duration());
  const auto validity_url = getValidityUrl();

  X509* cert = loadX09Cert();
  const auto cert_digest = generateCertDigest(cert);
  const auto cbor_url = getCborUrl(cert_digest);

  EVP_PKEY* pri_key = loadPrivateKey();

  const auto retval =
      cert && pri_key &&
      sxg_add_ecdsa_signer(sxgSigLabel().c_str(), date, expires, validity_url.c_str(), pri_key,
                           cert, cbor_url.c_str(), signers);

  if (cert) {
    X509_free(cert);
  }
  if (pri_key) {
    EVP_PKEY_free(pri_key);
  }
  return retval;
}

bool Filter::loadContent(Buffer::Instance& data, sxg_buffer_t* buf) {
  const size_t size = data.length();
  if (!sxg_buffer_resize(size, buf)) {
    return false;
  }

  data.copyOut(0, size, buf->data);

  return true;
}

bool Filter::getEncodedResponse(Buffer::Instance& data, sxg_encoded_response_t* encoded) {
  sxg_raw_response_t raw = sxg_empty_raw_response();
  bool retval = true;
  if (!loadContent(data, &raw.payload)) {
    retval = false;
  }
  if (retval) {
    // Pass response headers to the signed doc.
    const auto& filtered_headers = filteredResponseHeaders();
    const auto& should_encode_sxg_header = xShouldEncodeSxgKey().get();
    const auto& prefix_filters = config_->headerPrefixFilters();
    response_headers_->iterate(
        [filtered_headers, should_encode_sxg_header, prefix_filters, &raw,
         &retval](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
          const auto header_key = header.key().getStringView();

          // filter out all headers that should not be encoded in the SXG document
          if (absl::StartsWith(header_key, "x-envoy-")) {
            return Http::HeaderMap::Iterate::Continue;
          }
          if (should_encode_sxg_header == header_key) {
            return Http::HeaderMap::Iterate::Continue;
          }
          for (const auto& prefix_filter : prefix_filters) {
            if (absl::StartsWith(header_key, prefix_filter)) {
              return Http::HeaderMap::Iterate::Continue;
            }
          }
          if (filtered_headers.find(header_key) != filtered_headers.end()) {
            return Http::HeaderMap::Iterate::Continue;
          }

          const auto header_value = header.value().getStringView();
          if (!sxg_header_append_string(std::string(header_key).c_str(),
                                        std::string(header_value).c_str(), &raw.header)) {
            retval = false;
            return Http::HeaderMap::Iterate::Break;
          }
          return Http::HeaderMap::Iterate::Continue;
        });
  }
  if (retval && !sxg_encode_response(config_->miRecordSize(), &raw, encoded)) {
    retval = false;
  }
  sxg_raw_response_release(&raw);
  return retval;
}

bool Filter::writeSxg(const sxg_signer_list_t* signers, const std::string url,
                      const sxg_encoded_response_t* encoded, sxg_buffer_t* result) {
  return sxg_generate(url.c_str(), signers, encoded, result);
}

bool Filter::encoderBufferLimitReached(uint64_t buffer_length) {
  const auto limit = encoder_callbacks_->encoderBufferLimit();
  const auto header_size = response_headers_->byteSize();

  ENVOY_LOG(debug,
            "Envoy::Extensions::HttpFilters::SXG::Filter::encoderBufferLimitReached limit: {}, "
            "header_size: {} buffer_length: {}",
            limit, header_size, buffer_length);

  // note that a value of 0 indicates that no limits are enforced
  if (limit && header_size + buffer_length > limit) {
    config_->stats().total_exceeded_max_payload_size_.inc();
    return true;
  }
  return false;
}

const Http::LowerCaseString& Filter::xCanAcceptSxgKey() const {
  return config_->clientCanAcceptSXGHeader();
}

const std::string& Filter::xCanAcceptSxgValue() const {
  CONSTRUCT_ON_FIRST_USE(std::string, "true");
}

const Http::LowerCaseString& Filter::xShouldEncodeSxgKey() const {
  return config_->shouldEncodeSXGHeader();
}

const std::string& Filter::htmlContentType() const {
  CONSTRUCT_ON_FIRST_USE(std::string, "text/html");
}

const std::string& Filter::sxgContentTypeUnversioned() const {
  CONSTRUCT_ON_FIRST_USE(std::string, "application/signed-exchange");
}

const std::string& Filter::sxgSigLabel() const { CONSTRUCT_ON_FIRST_USE(std::string, "label"); }

const std::string& Filter::acceptedSxgVersion() const { CONSTRUCT_ON_FIRST_USE(std::string, "b3"); }

const std::string& Filter::sxgContentType() const {
  CONSTRUCT_ON_FIRST_USE(std::string, "application/signed-exchange;v=b3");
}

const Filter::HeaderFilterSet& Filter::filteredResponseHeaders() const {
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
