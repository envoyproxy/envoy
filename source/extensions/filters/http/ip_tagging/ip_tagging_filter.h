#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/config/filter/http/ip_tagging/v2/ip_tagging.pb.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"

#include "common/network/cidr_range.h"
#include "common/network/lc_trie.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

/**
 * Type of requests the filter should apply to.
 */
enum class FilterRequestType { INTERNAL, EXTERNAL, BOTH };

/**
 * Configuration for the HTTP IP Tagging filter.
 */
class IpTaggingFilterConfig {
public:
  IpTaggingFilterConfig(const envoy::config::filter::http::ip_tagging::v2::IPTagging& config,
                        const std::string& stat_prefix, Stats::Scope& scope,
                        Runtime::Loader& runtime)
      : request_type_(requestTypeEnum(config.request_type())), scope_(scope), runtime_(runtime),
        stats_prefix_(stat_prefix + "ip_tagging.") {

    // Once loading IP tags from a file system is supported, the restriction on the size
    // of the set should be removed and observability into what tags are loaded needs
    // to be implemented.
    // TODO(ccaraman): Remove size check once file system support is implemented.
    // Work is tracked by issue https://github.com/envoyproxy/envoy/issues/2695.
    if (config.ip_tags().empty()) {
      throw EnvoyException("HTTP IP Tagging Filter requires ip_tags to be specified.");
    }

    // TODO(ccaraman): Reduce the amount of copies operations performed to build the
    // IP tag data set and passing it to the LcTrie constructor.
    std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>> tag_data;
    for (const auto& ip_tag : config.ip_tags()) {
      std::pair<std::string, std::vector<Network::Address::CidrRange>> ip_tag_pair;
      ip_tag_pair.first = ip_tag.ip_tag_name();

      std::vector<Network::Address::CidrRange> cidr_set;
      for (const envoy::api::v2::core::CidrRange& entry : ip_tag.ip_list()) {

        // Currently, CidrRange::create doesn't guarantee that the CidrRanges are valid.
        Network::Address::CidrRange cidr_entry = Network::Address::CidrRange::create(entry);
        if (cidr_entry.isValid()) {
          cidr_set.emplace_back(cidr_entry);
        } else {
          throw EnvoyException(
              fmt::format("invalid ip/mask combo '{}/{}' (format is <ip>/<# mask bits>)",
                          entry.address_prefix(), entry.prefix_len().value()));
        }
      }
      ip_tag_pair.second = cidr_set;
      tag_data.emplace_back(ip_tag_pair);
    }
    trie_.reset(new Network::LcTrie::LcTrie<std::string>(tag_data));
  }

  Runtime::Loader& runtime() { return runtime_; }
  Stats::Scope& scope() { return scope_; }
  FilterRequestType requestType() const { return request_type_; }
  const Network::LcTrie::LcTrie<std::string>& trie() const { return *trie_; }
  const std::string& statsPrefix() const { return stats_prefix_; }

private:
  static FilterRequestType requestTypeEnum(
      envoy::config::filter::http::ip_tagging::v2::IPTagging::RequestType request_type) {
    switch (request_type) {
    case envoy::config::filter::http::ip_tagging::v2::IPTagging_RequestType_BOTH:
      return FilterRequestType::BOTH;
    case envoy::config::filter::http::ip_tagging::v2::IPTagging_RequestType_INTERNAL:
      return FilterRequestType::INTERNAL;
    case envoy::config::filter::http::ip_tagging::v2::IPTagging_RequestType_EXTERNAL:
      return FilterRequestType::EXTERNAL;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  const FilterRequestType request_type_;
  Stats::Scope& scope_;
  Runtime::Loader& runtime_;
  const std::string stats_prefix_;
  std::unique_ptr<Network::LcTrie::LcTrie<std::string>> trie_;
};

typedef std::shared_ptr<IpTaggingFilterConfig> IpTaggingFilterConfigSharedPtr;

/**
 * A filter that gets all tags associated with a request's downstream remote address and
 * sets a header `x-envoy-ip-tags` with those values.
 */
class IpTaggingFilter : public Http::StreamDecoderFilter {
public:
  IpTaggingFilter(IpTaggingFilterConfigSharedPtr config);
  ~IpTaggingFilter();

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

private:
  IpTaggingFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
};

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
