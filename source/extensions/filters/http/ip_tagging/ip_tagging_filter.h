#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/common/exception.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.h"
#include "envoy/filesystem/watcher.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"

#include "common/common/thread.h"
#include "common/network/cidr_range.h"
#include "common/network/lc_trie.h"
#include "common/stats/symbol_table_impl.h"

#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

/**
 * Class that represents the IP tag values from the file
 */
class ValueSet {

public:
  ValueSet() = default;

  ~ValueSet();

private:
  // todo
};

using IpTagFileProto = envoy::extensions::filters::http::ip_tagging::v3::IPTagging::IPTagFile;

/**
 * Coordinates with the Filesystem::Watcher and when that reports a change, load up
 * the change and updates it's internal settings.
 */
class ValueSetWatcher {
private:
  class Registry;

public:
  static std::shared_ptr<ValueSetWatcher>
  create(Server::Configuration::FactoryContext& factory_context, std::string filename);

  ValueSetWatcher(Server::Configuration::FactoryContext& factory_context,
                  Event::Dispatcher& dispatcher, Api::Api& api, std::string filename);

  ValueSetWatcher(Server::Configuration::FactoryContext& factory_context, std::string filename)
      : ValueSetWatcher(factory_context, factory_context.dispatcher(), factory_context.api(),
                        std::move(filename)) {}

  ~ValueSetWatcher();

  std::shared_ptr<const ValueSet> get() const;
  const std::string& filename() { return filename_; }

private:
  Envoy::ProtobufMessage::ValidationVisitor& protoValidator() const {
    return factory_context_.messageValidationVisitor();
  }

  void fileExtension_(std::string filename_) {
    if (absl::EndsWith(filename_, MessageUtil::FileExtensions::get().Yaml)) {
      extension_ = "Yaml";
    } else if (absl::EndsWith(filename_, MessageUtil::FileExtensions::get().Json)) {
      extension_ = "Json";
    } else {
      extension_ = "Unknown";
    }
  }

  void maybeUpdate_(bool force = false);
  void update_(absl::string_view content, std::uint64_t hash);
  std::shared_ptr<ValueSet> fileContentsAsValueSet_(absl::string_view contents) const;
  std::shared_ptr<const ValueSet> values_;

  Api::Api& api_;
  std::string filename_;
  static std::string extension_;
  std::uint64_t content_hash_ = 0;
  std::unique_ptr<Filesystem::Watcher> watcher_;
  Server::Configuration::FactoryContext& factory_context_;
  Registry* registry_ = nullptr; // Set by registry.
};

/**
 * The purpose of the registry is to create a single watcher for a file.
 */
class ValueSetWatcher::Registry {
private:
  using map_type = absl::flat_hash_map<absl::string_view, std::weak_ptr<ValueSetWatcher>,
                                       absl::Hash<absl::string_view>>;

public:
  std::shared_ptr<ValueSetWatcher>
  getOrCreate(Server::Configuration::FactoryContext& factory_context, std::string filename);
  void remove(ValueSetWatcher& watcher) noexcept;

  static Registry& singleton();

private:
  mutable Thread::MutexBasicLockable mtx_;
  map_type map_ ABSL_GUARDED_BY(mtx_);
};

/**
 * Type of requests the filter should apply to.
 */
enum class FilterRequestType { INTERNAL, EXTERNAL, BOTH };

/**
 * Configuration for the HTTP IP Tagging filter.
 */
class IpTaggingFilterConfig {
public:
  IpTaggingFilterConfig(const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& config,
                        const std::string& stat_prefix, Stats::Scope& scope,
                        Runtime::Loader& runtime,
                        Envoy::Server::Configuration::FactoryContext& factory_context);

  Runtime::Loader& runtime() { return runtime_; }
  FilterRequestType requestType() const { return request_type_; }
  const Network::LcTrie::LcTrie<std::string>& trie() const { return *trie_; }

  void incHit(absl::string_view tag) {
    incCounter(stat_name_set_->getBuiltin(absl::StrCat(tag, ".hit"), unknown_tag_));
  }
  void incNoHit() { incCounter(no_hit_); }
  void incTotal() { incCounter(total_); }

  std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>
  IpTaggingFilterSetTagData(
      const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& config);

private:
  static FilterRequestType requestTypeEnum(
      envoy::extensions::filters::http::ip_tagging::v3::IPTagging::RequestType request_type) {
    switch (request_type) {
    case envoy::extensions::filters::http::ip_tagging::v3::IPTagging::BOTH:
      return FilterRequestType::BOTH;
    case envoy::extensions::filters::http::ip_tagging::v3::IPTagging::INTERNAL:
      return FilterRequestType::INTERNAL;
    case envoy::extensions::filters::http::ip_tagging::v3::IPTagging::EXTERNAL:
      return FilterRequestType::EXTERNAL;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  void incCounter(Stats::StatName name);

  std::shared_ptr<const ValueSetWatcher> watcher_;
  const FilterRequestType request_type_;
  Stats::Scope& scope_;
  Runtime::Loader& runtime_;
  Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName stats_prefix_;
  const Stats::StatName no_hit_;
  const Stats::StatName total_;
  const Stats::StatName unknown_tag_;
  std::unique_ptr<Network::LcTrie::LcTrie<std::string>> trie_;
};

using IpTaggingFilterConfigSharedPtr = std::shared_ptr<IpTaggingFilterConfig>;

/**
 * A filter that gets all tags associated with a request's downstream remote address and
 * sets a header `x-envoy-ip-tags` with those values.
 */
class IpTaggingFilter : public Http::StreamDecoderFilter {
public:
  IpTaggingFilter(IpTaggingFilterConfigSharedPtr config);
  ~IpTaggingFilter() override;

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

private:
  std::shared_ptr<const ValueSetWatcher> watcher_;
  IpTaggingFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
};

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
