#include "contrib/checksum/filters/http/source/checksum_filter.h"

#include <openssl/mem.h>

#include "source/common/common/assert.h"
#include "source/common/common/hex.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ChecksumFilter {

static std::vector<ChecksumFilterConfig::ChecksumMatcher> buildMatchers(
    const envoy::extensions::filters::http::checksum::v3alpha::ChecksumConfig& proto_config,
    Server::Configuration::CommonFactoryContext& context) {
  std::vector<ChecksumFilterConfig::ChecksumMatcher> matchers;
  for (const auto& checksum : proto_config.checksums()) {
    std::vector<uint8_t> bytes = Hex::decode(checksum.sha256());
    matchers.emplace_back(std::make_unique<Matchers::PathMatcher>(checksum.path_matcher(), context),
                          Sha256Checksum{bytes.begin(), bytes.end()});
  }
  return matchers;
}

ChecksumFilterConfig::ChecksumFilterConfig(
    const envoy::extensions::filters::http::checksum::v3alpha::ChecksumConfig& proto_config,
    Server::Configuration::CommonFactoryContext& context)
    : matchers_(buildMatchers(proto_config, context)),
      reject_unmatched_(proto_config.reject_unmatched()) {}

OptRef<const Sha256Checksum> ChecksumFilterConfig::expectedChecksum(absl::string_view path) {
  for (const auto& m : matchers_) {
    if (m.first->match(path)) {
      return m.second;
    }
  }
  return absl::nullopt;
}

ChecksumFilter::ChecksumFilter(ChecksumFilterConfigSharedPtr config) : config_(config) {}

Http::FilterHeadersStatus ChecksumFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                        bool /*end_stream*/) {
  expected_checksum_ = config_->expectedChecksum(headers.Path()->value().getStringView());
  if (!expected_checksum_.has_value() && config_->rejectUnmatched()) {
    decoder_callbacks_->sendLocalReply(Http::Code::Forbidden, "No checksum for path", nullptr,
                                       absl::nullopt, "no_checksum_for_path");
    return Http::FilterHeadersStatus::StopIteration;
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus ChecksumFilter::encodeHeaders(Http::ResponseHeaderMap& /*headers*/,
                                                        bool end_stream) {
  if (end_stream && expected_checksum_.has_value()) {
    encoder_callbacks_->sendLocalReply(Http::Code::Forbidden,
                                       "Expected checksum has value but response has no body",
                                       nullptr, absl::nullopt, "checksum_but_no_body");
    return Http::FilterHeadersStatus::StopIteration;
  }
  SHA256_Init(&sha_);
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus ChecksumFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (!expected_checksum_.has_value()) {
    return Http::FilterDataStatus::Continue;
  }
  for (const auto& slice : data.getRawSlices()) {
    SHA256_Update(&sha_, slice.mem_, slice.len_);
  }
  if (end_stream && !checksumMatched()) {
    encoder_callbacks_->sendLocalReply(Http::Code::Forbidden, "Mismatched checksum", nullptr,
                                       absl::nullopt, "mismatched_checksum");
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus ChecksumFilter::encodeTrailers(Http::ResponseTrailerMap&) {
  if (!expected_checksum_.has_value() || checksumMatched()) {
    return Http::FilterTrailersStatus::Continue;
  }
  encoder_callbacks_->sendLocalReply(Http::Code::Forbidden, "Mismatched checksum", nullptr,
                                     absl::nullopt, "mismatched_checksum");
  return Http::FilterTrailersStatus::StopIteration;
}

bool ChecksumFilter::checksumMatched() {
  ASSERT(expected_checksum_.has_value());
  uint8_t checksum_buffer[SHA256_DIGEST_LENGTH];
  SHA256_Final(checksum_buffer, &sha_);
  OPENSSL_cleanse(&sha_, sizeof(sha_));
  return absl::string_view{reinterpret_cast<const char*>(&(*expected_checksum_)[0]),
                           expected_checksum_->size()} ==
         absl::string_view{reinterpret_cast<const char*>(checksum_buffer), sizeof(checksum_buffer)};
}

} // namespace ChecksumFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
