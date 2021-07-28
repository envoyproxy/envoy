#include "source/extensions/filters/http/sxg/filter.h"

#include <string>

#include "envoy/http/codes.h"
#include "envoy/server/filter_config.h"
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
      client_can_accept_sxg_header_(proto_config.client_can_accept_sxg_header().length() > 0
                                        ? proto_config.client_can_accept_sxg_header()
                                        : "x-client-can-accept-sxg"),
      should_encode_sxg_header_(proto_config.should_encode_sxg_header().length() > 0
                                    ? proto_config.should_encode_sxg_header()
                                    : "x-should-encode-sxg"),
      header_prefix_filters_(initializeHeaderPrefixFilters(proto_config.header_prefix_filters())),
      time_source_(time_source), secret_reader_(secret_reader) {}

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

Filter::Filter(const std::shared_ptr<FilterConfig>& config)
    : config_(config), encoder_(createEncoder(config)) {}

EncoderPtr Filter::createEncoder(const std::shared_ptr<FilterConfig>& config) {
  return std::make_unique<EncoderImpl>(
      config->certificate(), config->privateKey(), config->duration(), config->miRecordSize(),
      config->cborUrl(), config->validityUrl(), config->shouldEncodeSXGHeader(),
      config_->headerPrefixFilters(), config->timeSource());
}

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  ENVOY_LOG(debug, "sxg filter from Filter::encodeHeaders");

  if (client_accept_sxg_ && shouldEncodeSXG(headers)) {
    should_encode_sxg_ = true;
    response_headers_ = &headers;
    encoder_->loadHeaders(headers);
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

    if (!encoder_->getEncodedResponse(enc_buf)) {
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

    // Make sure that the resulting SXG isn't too big before adding it to the encoding buffer
    if (encoderBufferLimitReached(output->size() + 100)) {
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
