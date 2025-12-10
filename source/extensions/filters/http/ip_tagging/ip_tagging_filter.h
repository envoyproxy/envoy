#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/optref.h"
#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/thread_synchronizer.h"
#include "source/common/config/datasource.h"
#include "source/common/network/cidr_range.h"
#include "source/common/network/lc_trie.h"
#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

using IPTagsProto = envoy::extensions::filters::http::ip_tagging::v3::IPTagging::IPTags;
using LcTrieSharedPtr = std::shared_ptr<Network::LcTrie::LcTrie<std::string>>;

/**
 * This class is responsible for loading and parsing of ip tags (both inline and file based)
 * as well as for periodic refresh of ip tags (file based).
 */
class IpTagsLoader : public Logger::Loggable<Logger::Id::ip_tagging>,
                     public std::enable_shared_from_this<IpTagsLoader> {
public:
  IpTagsLoader(Api::Api& api, ProtobufMessage::ValidationVisitor& validation_visitor,
               Stats::Scope& scope);

  /**
   * Loads file based ip tags from a data source and parses them into a trie structure.
   * @param ip_tags_datasource file based data source to load ip tags from.
   * @param dispatcher The dispatcher for the thread used by a data source provider.
   * @param tls The thread local slot allocator used by a data source provider.
   * @return Valid LcTrieSharedPtr if loading succeeded or error status otherwise.
   */
  absl::StatusOr<LcTrieSharedPtr>
  loadTags(const envoy::config::core::v3::DataSource& ip_tags_datasource,
           Event::Dispatcher& dispatcher, ThreadLocal::SlotAllocator& tls);

  /**
   * Performs periodic refresh of file based ip tags via data source.
   * @param new_data New data from data source which is used to refresh the ip tags structure.
   * @return Valid LcTrieSharedPtr if loading succeeded or error status otherwise.
   */
  absl::StatusOr<LcTrieSharedPtr> getTags();

  /**
   * Parses ip tags in a proto format into a trie structure.
   * @param ip_tags Collection of ip tags in proto format.
   * @return Valid LcTrieSharedPtr if parsing succeeded or error status otherwise.
   */
  absl::StatusOr<LcTrieSharedPtr>
  parseIpTagsAsProto(const Protobuf::RepeatedPtrField<
                     envoy::extensions::filters::http::ip_tagging::v3::IPTagging::IPTag>& ip_tags);

  void incIpTagsReloadSuccess() { incCounter(stat_name_set_->getBuiltin("success", unknown_tag_)); }

private:
  Api::Api& api_;
  Envoy::Config::DataSource::DataSourceProviderPtr<Network::LcTrie::LcTrie<std::string>>
      data_source_provider_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Stats::ScopeSharedPtr scope_;
  Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName stats_prefix_;
  const Stats::StatName unknown_tag_;

  void incCounter(Stats::StatName name);
};

/**
 * This class owns ip tags trie structure for a configured absolute file path and provides access to
 * the ip tags data. It also performs periodic refresh of ip tags data.
 */
class IpTagsProvider : public Logger::Loggable<Logger::Id::ip_tagging> {
public:
  IpTagsProvider(const envoy::config::core::v3::DataSource& ip_tags_datasource,
                 Event::Dispatcher& main_dispatcher, Api::Api& api,
                 ProtobufMessage::ValidationVisitor& validation_visitor,
                 ThreadLocal::SlotAllocator& tls, Stats::Scope& scope,
                 Singleton::InstanceSharedPtr owner, absl::Status& creation_status);

  ~IpTagsProvider();

  /**
   * Getter method for ip tags trie structure.
   * @return Valid LcTrieSharedPtr or absl::Status if failed to get tags data.
   */
  absl::StatusOr<LcTrieSharedPtr> ipTags();

private:
  IpTagsLoader tags_loader_;
  LcTrieSharedPtr tags_;
  // A shared_ptr to keep the provider singleton alive as long as any of its providers are in use.
  const Singleton::InstanceSharedPtr owner_;
};

using IpTagsProviderSharedPtr = std::shared_ptr<IpTagsProvider>;

/**
 * A singleton for file based loading of ip tags and looking up parsed trie data structures with Ip
 * tags. When given equivalent file paths to the Ip tags, the singleton returns pointers to the same
 * trie structure.
 */
class IpTagsRegistrySingleton : public Envoy::Singleton::Instance {
public:
  IpTagsRegistrySingleton() {}

  absl::StatusOr<std::shared_ptr<IpTagsProvider>>
  getOrCreateProvider(const envoy::config::core::v3::DataSource& ip_tags_datasource, Api::Api& api,
                      ProtobufMessage::ValidationVisitor& validation_visitor,
                      ThreadLocal::SlotAllocator& tls, Event::Dispatcher& main_dispatcher,
                      Stats::Scope& scope, std::shared_ptr<IpTagsRegistrySingleton> singleton);

private:
  absl::Mutex mu_;
  // Each provider stores shared_ptrs to this singleton, which keeps the singleton
  // from being destroyed unless it's no longer keeping track of any providers.
  // Each entry in this map consists of a key (hash of an absolute file path to ip tags file)
  // and and value (instance of `IpTagsProvider` that owns ip tags data).
  absl::flat_hash_map<size_t, std::weak_ptr<IpTagsProvider>> ip_tags_registry_ ABSL_GUARDED_BY(mu_);
};

/**
 * Type of requests the filter should apply to.
 */
enum class FilterRequestType { INTERNAL, EXTERNAL, BOTH };

/**
 * Configuration for the HTTP IP Tagging filter.
 */
class IpTaggingFilterConfig : public Logger::Loggable<Logger::Id::ip_tagging> {
public:
  using HeaderAction =
      envoy::extensions::filters::http::ip_tagging::v3::IPTagging::IpTagHeader::HeaderAction;

  static absl::StatusOr<std::shared_ptr<IpTaggingFilterConfig>>
  create(const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& config,
         const std::string& stat_prefix, Singleton::Manager& singleton_manager, Stats::Scope& scope,
         Runtime::Loader& runtime, Api::Api& api, ThreadLocal::SlotAllocator& tls,
         Event::Dispatcher& dispatcher, ProtobufMessage::ValidationVisitor& validation_visitor);

  Runtime::Loader& runtime() { return runtime_; }
  FilterRequestType requestType() const { return request_type_; }
  const Network::LcTrie::LcTrie<std::string>& trie() const {
    if (provider_) {
      auto tags_or_error = provider_->ipTags();
      if (tags_or_error.status().ok() && tags_or_error.value() != nullptr) {
        return *(tags_or_error.value());
      } else {
        ENVOY_LOG(debug, "Failed to get trie data from provider, falling back to empty trie");
        return empty_trie_;
      }
    } else {
      if (trie_ != nullptr) {
        return *trie_;
      } else {
        return empty_trie_;
      }
    }
  }

  OptRef<const Http::LowerCaseString> ipTagHeader() const {
    if (ip_tag_header_.get().empty()) {
      return absl::nullopt;
    }
    return ip_tag_header_;
  }
  HeaderAction ipTagHeaderAction() const { return ip_tag_header_action_; }

  void incHit(absl::string_view tag) {
    const Stats::StatName tag_counter =
        stat_name_set_->getBuiltin(absl::StrCat(tag, ".hit"), unknown_tag_);
    if (tag_counter == unknown_tag_) {
      stat_name_set_->rememberBuiltin(absl::StrCat(tag, ".hit"));
    }
    incCounter(stat_name_set_->getBuiltin(absl::StrCat(tag, ".hit"), unknown_tag_));
  }

  void incNoHit() { incCounter(no_hit_); }
  void incTotal() { incCounter(total_); }

private:
  IpTaggingFilterConfig(const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& config,
                        const std::string& stat_prefix, Singleton::Manager& singleton_manager,
                        Stats::Scope& scope, Runtime::Loader& runtime, Api::Api& api,
                        ThreadLocal::SlotAllocator& tls, Event::Dispatcher& dispatcher,
                        ProtobufMessage::ValidationVisitor& validation_visitor,
                        absl::Status& creation_status);

  static FilterRequestType requestTypeEnum(
      envoy::extensions::filters::http::ip_tagging::v3::IPTagging::RequestType request_type) {
    switch (request_type) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case envoy::extensions::filters::http::ip_tagging::v3::IPTagging::BOTH:
      return FilterRequestType::BOTH;
    case envoy::extensions::filters::http::ip_tagging::v3::IPTagging::INTERNAL:
      return FilterRequestType::INTERNAL;
    case envoy::extensions::filters::http::ip_tagging::v3::IPTagging::EXTERNAL:
      return FilterRequestType::EXTERNAL;
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  void incCounter(Stats::StatName name);
  // Allow the unit test to have access to private members.
  friend class IpTaggingFilterConfigPeer;
  const FilterRequestType request_type_;
  Stats::Scope& scope_;
  Runtime::Loader& runtime_;
  Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName stats_prefix_;
  const Stats::StatName no_hit_;
  const Stats::StatName total_;
  const Stats::StatName unknown_tag_;
  const Http::LowerCaseString
      ip_tag_header_; // An empty string indicates that no ip_tag_header is set.
  const HeaderAction ip_tag_header_action_;
  // A shared_ptr to keep the ip tags registry singleton alive as long as any of its trie structures
  // are in use.
  const std::shared_ptr<IpTagsRegistrySingleton> ip_tags_registry_;
  IpTagsLoader tags_loader_;
  LcTrieSharedPtr trie_;
  Network::LcTrie::LcTrie<std::string> empty_trie_;
  std::shared_ptr<IpTagsProvider> provider_;
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
  void applyTags(Http::RequestHeaderMap& headers, const std::vector<std::string>& tags);

  IpTaggingFilterConfigSharedPtr config_;
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  // Used for testing only.
  mutable Thread::ThreadSynchronizer synchronizer_;
  // Allow the unit test to have access to private members.
  friend class IpTaggingFilterPeer;
};

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
