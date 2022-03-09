#include "contrib/sxg/filters/http/source/filter.h"

#include <string>

#include "envoy/http/codes.h"
#include "envoy/stats/scope.h"

#include "source/common/common/utility.h"
#include "source/common/http/headers.h"
#include "source/common/stats/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SXG {

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    accept_handle(Http::CustomHeaders::get().Accept);

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  ENVOY_LOG(debug, "sxg filter from decodeHeaders: {}", headers);
  if (headers.Host() && headers.Path() && clientAcceptSXG(headers)) {
    client_accept_sxg_ = true;
    headers.setReference(xCanAcceptSxgKey(), xCanAcceptSxgValue());
    auto origin = fmt::format("https://{}", headers.getHostValue());
    auto url = fmt::format("{}{}", origin, headers.getPathValue());
    encoder_->setOrigin(origin);
    encoder_->setUrl(url);
    config_->stats().total_client_can_accept_sxg_.inc();
  }
  return Http::FilterHeadersStatus::Continue;
}

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  ENVOY_LOG(debug, "sxg filter from Filter::encodeHeaders");

  if (client_accept_sxg_ && shouldEncodeSXG(headers)) {
    response_headers_ = &headers;
    should_encode_sxg_ = true;
    config_->stats().total_should_sign_.inc();
    return Http::FilterHeadersStatus::StopIteration;
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "sxg filter from encodeData end_stream: {}", end_stream);

  if (!should_encode_sxg_) {
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

void Filter::doSxg() {
  if (finished_) {
    return;
  }

  finished_ = true;

  encoder_callbacks_->modifyEncodingBuffer([this](Buffer::Instance& enc_buf) {
    config_->stats().total_signed_attempts_.inc();

    if (!encoder_->loadHeaders(response_headers_)) {
      config_->stats().total_signed_failed_.inc();
      return;
    }

    if (!encoder_->loadContent(enc_buf)) {
      config_->stats().total_signed_failed_.inc();
      return;
    }

    if (!encoder_->getEncodedResponse()) {
      config_->stats().total_signed_failed_.inc();
      return;
    }

    if (!encoder_->loadSigner()) {
      config_->stats().total_signed_failed_.inc();
      return;
    }

    auto output = encoder_->writeSxg();
    if (!output) {
      config_->stats().total_signed_failed_.inc();
      return;
    }

    // Make sure that the resulting SXG isn't too big before adding it to the encoding
    // buffer. Note that since the buffer fragment hasn't been added to the enc_buf
    // yet, we need to call done() directly.
    if (encoderBufferLimitReached(output->size() + 100)) {
      output->done();
      config_->stats().total_signed_failed_.inc();
      return;
    }

    enc_buf.drain(enc_buf.length());
    enc_buf.addBufferFragment(*output);

    response_headers_->setContentLength(enc_buf.length());
    response_headers_->setContentType(sxgContentType());

    config_->stats().total_signed_succeeded_.inc();
  });
}

void Filter::setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

bool Filter::clientAcceptSXG(const Http::RequestHeaderMap& headers) {
  const absl::string_view accept = headers.getInlineValue(accept_handle.handle());

  absl::string_view html_q_value = "0";
  absl::string_view sxg_q_value = "";
  // Client can accept signed exchange if accept header has:
  // a) application/signed-exchange
  // b) with appropriate version (v=b3)
  // c) q-value of signed exchange is >= that of text/html
  // from: https://web.dev/signed-exchanges/#best-practices
  for (const auto& token : StringUtil::splitToken(accept, ",")) {
    const auto& type = StringUtil::trim(StringUtil::cropRight(token, ";"));
    absl::string_view q_value = "1";
    absl::string_view version = "";

    const auto params = StringUtil::cropLeft(token, ";");
    for (const auto& param : StringUtil::splitToken(params, ";")) {
      if (absl::EqualsIgnoreCase("q", StringUtil::trim(StringUtil::cropRight(param, "=")))) {
        q_value = StringUtil::trim(StringUtil::cropLeft(param, "="));
      }
      if (absl::EqualsIgnoreCase("v", StringUtil::trim(StringUtil::cropRight(param, "=")))) {
        version = StringUtil::trim(StringUtil::cropLeft(param, "="));
      }
    }

    if (type == sxgContentTypeUnversioned() && version == acceptedSxgVersion()) {
      sxg_q_value = q_value;
    } else if (type == htmlContentType()) {
      html_q_value = q_value;
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

const std::string& Filter::acceptedSxgVersion() const { CONSTRUCT_ON_FIRST_USE(std::string, "b3"); }

const std::string& Filter::sxgContentType() const {
  CONSTRUCT_ON_FIRST_USE(std::string, "application/signed-exchange;v=b3");
}

} // namespace SXG
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
