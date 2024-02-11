#pragma once

#include <openssl/sha.h>

#include <memory>
#include <string>
#include <vector>

#include "envoy/http/filter.h"

#include "source/common/common/matchers.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "contrib/envoy/extensions/filters/http/checksum/v3alpha/checksum.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ChecksumFilter {

using Sha256Checksum = absl::FixedArray<uint8_t, 32>;

/**
 * Configuration for the checksum filter.
 */
class ChecksumFilterConfig {
public:
  using ChecksumMatcher = std::pair<std::unique_ptr<Matchers::PathMatcher>, Sha256Checksum>;

  ChecksumFilterConfig(
      const envoy::extensions::filters::http::checksum::v3alpha::ChecksumConfig& proto_config);

  bool rejectUnmatched() const { return reject_unmatched_; }
  // Returns nullopt on no match.
  OptRef<const Sha256Checksum> expectedChecksum(absl::string_view path);

private:
  const std::vector<ChecksumMatcher> matchers_;
  const bool reject_unmatched_;
};

using ChecksumFilterConfigSharedPtr = std::shared_ptr<ChecksumFilterConfig>;

/**
 * A filter that is capable of checksuming an entire request before dispatching it upstream.
 */
class ChecksumFilter : public Http::PassThroughFilter {
public:
  ChecksumFilter(ChecksumFilterConfigSharedPtr config);

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  void onDestroy() override {}

private:
  bool checksumMatched();
  ChecksumFilterConfigSharedPtr config_;
  OptRef<const Sha256Checksum> expected_checksum_;
  SHA256_CTX sha_;
};

} // namespace ChecksumFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
