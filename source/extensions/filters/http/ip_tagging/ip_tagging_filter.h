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

#include "source/common/common/callback_impl.h"
#include "source/common/common/thread.h"
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
 * Stats container for a single loaded set of ip tags. Owns the StatNameSet used to
 * intern per-tag `<tag>.hit` builtins, plus the shared `total`, `no_hit`, `unknown_tag.hit`
 * and `reload_success` stat names. Also exposes the proto-to-trie parser, which is the
 * one place tag names are interned as builtins.
 */
class IpTagsStats : public Logger::Loggable<Logger::Id::ip_tagging> {
public:
  IpTagsStats(const std::string& stat_prefix, Stats::ScopeSharedPtr scope);

  void incHit(absl::string_view tag);
  void incNoHit() { incCounter(no_hit_); }
  void incTotal() { incCounter(total_); }
  void incIpTagsReloadSuccess() { incCounter(reload_success_); }

  /**
   * Parses ip tags in proto format into a trie. Interns each `<tag>.hit` as a builtin
   * on this instance's StatNameSet so subsequent `incHit` calls go through the fast path.
   * @return Valid LcTrieSharedPtr on success or an error status otherwise.
   */
  absl::StatusOr<LcTrieSharedPtr>
  parseIpTagsAsProto(const Protobuf::RepeatedPtrField<
                     envoy::extensions::filters::http::ip_tagging::v3::IPTagging::IPTag>& ip_tags);

private:
  void incCounter(Stats::StatName name);

  // Owned by this instance so the stats remain valid even if the caller that bootstrapped
  // them goes away (relevant for providers shared across listeners).
  Stats::ScopeSharedPtr scope_;
  Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName stats_prefix_;
  const Stats::StatName unknown_tag_;
  const Stats::StatName total_;
  const Stats::StatName no_hit_;
  const Stats::StatName reload_success_;
};

using IpTagsStatsSharedPtr = std::shared_ptr<IpTagsStats>;

/**
 * A single immutable snapshot of the parsed ip tags: the trie used for lookup and the
 * stats container whose StatNameSet has the matching tag-name builtins interned. A
 * snapshot is produced atomically on every load and handed to the filter as a unit, so
 * a request never sees a trie from one reload paired with stats from another.
 */
struct LoadedIpTags {
  LcTrieSharedPtr trie;
  IpTagsStatsSharedPtr stats;
};

using LoadedIpTagsConstSharedPtr = std::shared_ptr<const LoadedIpTags>;

/**
 * Owns the file-watched DataSourceProvider that produces a new LoadedIpTags on every
 * successful reload.
 */
class IpTagsProvider : public Logger::Loggable<Logger::Id::ip_tagging> {
public:
  IpTagsProvider(const envoy::config::core::v3::DataSource& ip_tags_datasource,
                 Event::Dispatcher& main_dispatcher, Api::Api& api,
                 ProtobufMessage::ValidationVisitor& validation_visitor,
                 ThreadLocal::SlotAllocator& tls, const std::string& stat_prefix,
                 Stats::ScopeSharedPtr scope, Singleton::InstanceSharedPtr owner,
                 absl::Status& creation_status);

  ~IpTagsProvider();

  LoadedIpTagsConstSharedPtr loadedIpTags() const { return data_source_provider_->data(); }

private:
  Envoy::Config::DataSource::DataSourceProviderPtr<LoadedIpTags> data_source_provider_;
  // Keeps the registry singleton alive as long as any provider is in use.
  const Singleton::InstanceSharedPtr owner_;
};

using IpTagsProviderSharedPtr = std::shared_ptr<IpTagsProvider>;

/**
 * A singleton that de-duplicates IpTagsProvider instances by data source filename so two
 * filter configs pointing at the same file share one provider (and thus one parsed trie).
 */
class IpTagsRegistrySingleton : public Envoy::Singleton::Instance {
public:
  IpTagsRegistrySingleton() = default;

  absl::StatusOr<std::shared_ptr<IpTagsProvider>>
  getOrCreateProvider(const envoy::config::core::v3::DataSource& ip_tags_datasource, Api::Api& api,
                      ProtobufMessage::ValidationVisitor& validation_visitor,
                      ThreadLocal::SlotAllocator& tls, Event::Dispatcher& main_dispatcher,
                      const std::string& stat_prefix, Stats::Scope& scope,
                      std::shared_ptr<IpTagsRegistrySingleton> singleton);

private:
  // Each provider stores a shared_ptr to this singleton, keeping the singleton alive
  // until no provider remains. Key is a hash of the data source filename.
  absl::flat_hash_map<size_t, std::weak_ptr<IpTagsProvider>> ip_tags_registry_;
  // Shared stats scope used by every provider this singleton creates. Lazily
  // initialized as a child of the first caller's scope and kept alive by the
  // singleton so a provider can outlive any individual listener that asked for it.
  Stats::ScopeSharedPtr scope_;
};

/**
 * Type of requests the filter should apply to.
 */
enum class FilterRequestType { INTERNAL, EXTERNAL, BOTH };

/**
 * Configuration for the HTTP IP Tagging filter.
 */
class IpTaggingFilterConfig : public std::enable_shared_from_this<IpTaggingFilterConfig>,
                              public Logger::Loggable<Logger::Id::ip_tagging> {
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

  // Returns the current snapshot. For the inline (static) path this is the snapshot
  // built at construction; for the data source path it is the snapshot currently
  // published by the file watcher. The filter pulls this once per request so the trie
  // and stats it uses come from the same reload.
  LoadedIpTagsConstSharedPtr loadedIpTags() const {
    return provider_ ? provider_->loadedIpTags() : static_ip_tags_;
  }

  OptRef<const Http::LowerCaseString> ipTagHeader() const;
  HeaderAction ipTagHeaderAction() const { return ip_tag_header_action_; }

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

  friend class IpTaggingFilterConfigPeer;

  const FilterRequestType request_type_;
  Runtime::Loader& runtime_;
  const Http::LowerCaseString
      ip_tag_header_; // An empty string indicates that no ip_tag_header is set.
  const HeaderAction ip_tag_header_action_;
  // Keeps the registry singleton alive as long as any of its providers are in use.
  const std::shared_ptr<IpTagsRegistrySingleton> ip_tags_registry_;
  // Set only on the inline path.
  LoadedIpTagsConstSharedPtr static_ip_tags_;
  // Set only on the data source path.
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
