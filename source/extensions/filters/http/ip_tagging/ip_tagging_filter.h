#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <atomic>

#include "envoy/common/optref.h"
#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.h"
#include "envoy/http/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/singleton/instance.h"
#include "envoy/singleton/manager.h"

#include "source/common/common/logger.h"
#include "source/common/common/thread_synchronizer.h"
#include "source/common/config/datasource.h"
#include "source/common/network/cidr_range.h"
#include "source/common/network/lc_trie.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/protobuf/message_validator.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

using IpTagFileProto = envoy::extensions::filters::http::ip_tagging::v3::IPTagging::IPTagFile;
using LcTrieSharedPtr = std::shared_ptr<Network::LcTrie::LcTrie<std::string>>;
using IpTagsReloadSuccessCb = std::function<void()>;
using IpTagsReloadErrorCb = std::function<void()>;

/**
 * Simplified IP tags loader that handles both inline and file-based IP tags
 */
class IpTagsLoader : public Logger::Loggable<Logger::Id::ip_tagging> {
public:
  IpTagsLoader(Api::Api& api, ProtobufMessage::ValidationVisitor& validation_visitor,
               Stats::StatNameSetPtr& stat_name_set);

  // Load tags from datasource (file or inline)
  absl::StatusOr<LcTrieSharedPtr>
  loadTags(const envoy::config::core::v3::DataSource& ip_tags_datasource,
           Event::Dispatcher& dispatcher, ThreadLocal::SlotAllocator& tls);

  // Refresh tags from existing datasource
  absl::StatusOr<LcTrieSharedPtr> refreshTags();

  // Parse proto format tags into trie
  absl::StatusOr<LcTrieSharedPtr>
  parseIpTagsAsProto(const Protobuf::RepeatedPtrField<
                     envoy::extensions::filters::http::ip_tagging::v3::IPTagging::IPTag>& ip_tags);

  // Get current data from datasource (used for debugging)
  const std::string& getDataSourceData();

private:
  Api::Api& api_;
  Envoy::Config::DataSource::DataSourceProviderPtr data_source_provider_;
  std::string ip_tags_path_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Stats::StatNameSetPtr& stat_name_set_;
  std::string data_; // Cache for data source content
};

/**
 * IP tags provider with automatic refresh capability
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

  // Get current IP tags (thread-safe)
  LcTrieSharedPtr ipTags() ABSL_LOCKS_EXCLUDED(ip_tags_mutex_);

  // Setup timer (must be called after shared_ptr creation)
  void setupTimer(Event::Dispatcher& main_dispatcher);

private:
  const std::string ip_tags_path_;
  const envoy::config::core::v3::DataSource ip_tags_datasource_;
  Api::Api& api_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Stats::StatNameSetPtr stat_name_set_;
  TimeSource& time_source_;
  const std::chrono::milliseconds ip_tags_refresh_interval_ms_;
  const bool needs_refresh_;
  IpTagsReloadSuccessCb reload_success_cb_;
  IpTagsReloadErrorCb reload_error_cb_;
  mutable absl::Mutex ip_tags_mutex_;
  LcTrieSharedPtr tags_ ABSL_GUARDED_BY(ip_tags_mutex_);
  Event::TimerPtr ip_tags_reload_timer_;
  const Singleton::InstanceSharedPtr owner_;
  std::atomic<bool> is_destroying_{false};

  void updateIpTags(LcTrieSharedPtr new_tags) ABSL_LOCKS_EXCLUDED(ip_tags_mutex_);

  // File-based reload methods - these now handle missing files gracefully
  absl::StatusOr<LcTrieSharedPtr> reloadFromFile(const std::string& file_path);
  absl::StatusOr<LcTrieSharedPtr> parseFileContent(const std::string& content, const std::string& file_path);
};

/**
 * Registry singleton for managing IP tags providers
 */
class IpTagsRegistrySingleton : public Envoy::Singleton::Instance {
public:
  IpTagsRegistrySingleton() = default;

  absl::StatusOr<std::shared_ptr<IpTagsProvider>> getOrCreateProvider(
      const envoy::config::core::v3::DataSource& ip_tags_datasource, IpTagsLoader& tags_loader,
      uint64_t ip_tags_refresh_interval_ms, IpTagsReloadSuccessCb reload_success_cb,
      IpTagsReloadErrorCb reload_error_cb, Api::Api& api,
      ProtobufMessage::ValidationVisitor& validation_visitor,
      Stats::StatNameSetPtr stat_name_set, ThreadLocal::SlotAllocator& tls,
      Event::Dispatcher& main_dispatcher, std::shared_ptr<IpTagsRegistrySingleton> singleton);

private:
  absl::Mutex mu_;
  absl::flat_hash_map<size_t, std::weak_ptr<IpTagsProvider>> ip_tags_registry_ ABSL_GUARDED_BY(mu_);
};

/**
 * Type of requests the filter should apply to
 */
enum class FilterRequestType { INTERNAL, EXTERNAL, BOTH };

/**
 * Simplified configuration for the HTTP IP Tagging filter
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
    if (provider_) {
      auto provider_tags = provider_->ipTags();
      if (provider_tags) {
        return *provider_tags;
      }
    }
    // Return default empty trie if provider fails
    return *trie_;
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

  const FilterRequestType request_type_;
  Stats::Scope& scope_;
  Runtime::Loader& runtime_;
  Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName stats_prefix_;
  const Stats::StatName no_hit_;
  const Stats::StatName total_;
  const Stats::StatName unknown_tag_;
  const Http::LowerCaseString ip_tag_header_;
  const HeaderAction ip_tag_header_action_;
  const std::shared_ptr<IpTagsRegistrySingleton> ip_tags_registry_;
  IpTagsLoader tags_loader_;
  LcTrieSharedPtr trie_;
  std::shared_ptr<IpTagsProvider> provider_;
};

using IpTaggingFilterConfigSharedPtr = std::shared_ptr<IpTaggingFilterConfig>;

/**
 * HTTP IP tagging filter
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
  mutable Thread::ThreadSynchronizer synchronizer_;
};

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
