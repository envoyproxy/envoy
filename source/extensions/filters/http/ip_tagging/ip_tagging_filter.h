#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <atomic>

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

using IpTagFileProto = envoy::extensions::filters::http::ip_tagging::v3::IPTagging::IPTagFile;
using LcTrieSharedPtr = std::shared_ptr<Network::LcTrie::LcTrie<std::string>>;

/**
 * This class is responsible for loading and parsing of ip tags (both inline and file based)
 * as well as for periodic refresh of ip tags (file based).
 */
class IpTagsLoader : public Logger::Loggable<Logger::Id::ip_tagging> {
public:
  IpTagsLoader(Api::Api& api, ProtobufMessage::ValidationVisitor& validation_visitor,
               Stats::StatNameSetPtr& stat_name_set);

  /**
   * Loads file based ip tags from a data source and parses them into a trie structure.
   * @param ip_tags_datasource file based data source to load ip tags from.
   * @param dispatcher The dispatcher for the thread used by a data source provider.
   * @param tls The thread local slot allocator used by a data source provider.
   * @param creation_status This status will be populated with error if loading fails.
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
  absl::StatusOr<LcTrieSharedPtr> refreshTags();

  /**
   * Parses ip tags in a proto format into a trie structure.
   * @param ip_tags Collection of ip tags in proto format.
   * @param creation_status This status will be populated with error if parsing fails.
   * @return Valid LcTrieSharedPtr if parsing succeeded or error status otherwise.
   */
  absl::StatusOr<LcTrieSharedPtr>
  parseIpTagsAsProto(const Protobuf::RepeatedPtrField<
                     envoy::extensions::filters::http::ip_tagging::v3::IPTagging::IPTag>& ip_tags);

private:
  Api::Api& api_;
  Envoy::Config::DataSource::DataSourceProviderPtr data_source_provider_;
  std::string ip_tags_path_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Stats::StatNameSetPtr& stat_name_set_;
};

using IpTagsReloadSuccessCb = std::function<void()>;
using IpTagsReloadErrorCb = std::function<void()>;

/**
 * This class owns ip tags trie structure for a configured absolute file path and provides access to
 * the ip tags data. It also performs periodic refresh of ip tags data.
 */
class IpTagsProvider : public Logger::Loggable<Logger::Id::ip_tagging>,
                       public std::enable_shared_from_this<IpTagsProvider> {
public:
  IpTagsProvider(const envoy::config::core::v3::DataSource& ip_tags_datasource,
                 IpTagsLoader& tags_loader, uint64_t ip_tags_refresh_interval_ms,
                 IpTagsReloadSuccessCb reload_success_cb, IpTagsReloadErrorCb reload_error_cb,
                 Event::Dispatcher& main_dispatcher, Api::Api& api,
                 ProtobufMessage::ValidationVisitor& validation_visitor,
                 Stats::StatNameSetPtr stat_name_set, ThreadLocal::SlotAllocator& tls,
                 Singleton::InstanceSharedPtr owner, absl::Status& creation_status);

  ~IpTagsProvider();

  /**
   * Getter method for ip tags trie structure which either returns the current version of ip tags
   * data or performs a reload of data and returns the most recent version of ip tags data.
   * @return Valid LcTrieSharedPtr or nullptr if reload has failed.
   */
  LcTrieSharedPtr ipTags() ABSL_LOCKS_EXCLUDED(ip_tags_mutex_);

  /**
   * Set up the timer after construction. This must be called after the shared_ptr is created.
   */
  void setupTimer(Event::Dispatcher& main_dispatcher);

private:
  const std::string ip_tags_path_;
  // Store the data source configuration and dependencies for self-contained operation
  const envoy::config::core::v3::DataSource ip_tags_datasource_;
  Api::Api& api_;
  const std::chrono::milliseconds ip_tags_refresh_interval_ms_;
  const bool needs_refresh_;
  IpTagsReloadSuccessCb reload_success_cb_;
  IpTagsReloadErrorCb reload_error_cb_;
  mutable absl::Mutex ip_tags_mutex_;
  LcTrieSharedPtr tags_ ABSL_GUARDED_BY(ip_tags_mutex_);
  Event::TimerPtr ip_tags_reload_timer_;
  // A shared_ptr to keep the provider singleton alive as long as any of its providers are in use.
  const Singleton::InstanceSharedPtr owner_;

  // Flag to track if the object is being destroyed
  std::atomic<bool> is_destroying_{false};

  void updateIpTags(LcTrieSharedPtr new_tags) ABSL_LOCKS_EXCLUDED(ip_tags_mutex_);
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

  absl::StatusOr<std::shared_ptr<IpTagsProvider>> getOrCreateProvider(
      const envoy::config::core::v3::DataSource& ip_tags_datasource, IpTagsLoader& tags_loader,
      uint64_t ip_tags_refresh_interval_ms, IpTagsReloadSuccessCb reload_success_cb,
      IpTagsReloadErrorCb reload_error_cb, Api::Api& api,
      ProtobufMessage::ValidationVisitor& validation_visitor,
      Stats::StatNameSetPtr stat_name_set, ThreadLocal::SlotAllocator& tls,
      Event::Dispatcher& main_dispatcher, std::shared_ptr<IpTagsRegistrySingleton> singleton);

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
class IpTaggingFilterConfig {
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
    if (provider_ && provider_->ipTags()) {
      return *(provider_->ipTags());
    } else if (trie_) {
      return *trie_;
    } else {
      // Fallback: create empty trie if both are null (should never happen with graceful init)
      static const auto empty_trie = []() {
        std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>> empty_data;
        return std::make_shared<Network::LcTrie::LcTrie<std::string>>(empty_data);
      }();
      return *empty_trie;
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
    incCounter(stat_name_set_->getBuiltin(absl::StrCat(tag, ".hit"), unknown_tag_));
  }

  void incIpTagsReloadSuccess() {
    incCounter(stat_name_set_->getBuiltin("ip_tags_reload_success", unknown_tag_));
  }
  void incIpTagsReloadError() {
    incCounter(stat_name_set_->getBuiltin("ip_tags_reload_error", unknown_tag_));
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
