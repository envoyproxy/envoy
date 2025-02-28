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

#include "source/common/network/cidr_range.h"
#include "source/common/network/lc_trie.h"
#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

using IpTagFileProto = envoy::data::ip_tagging::v3::IPTagFile;
using LcTrieSharedPtr = std::shared_ptr<Network::LcTrie::LcTrie<std::string>>;

// TODO supports stats for ip tags
// Add tests for singleton
// Support async reload of tags file
class IpTagsLoader {
public:
  IpTagsLoader(Api::Api& api, ProtobufMessage::ValidationVisitor& validation_visitor,
               Stats::StatNameSetPtr& stat_name_set);

  LcTrieSharedPtr loadTags(const std::string& ip_tags_path);

  LcTrieSharedPtr
  parseIpTags(const Protobuf::RepeatedPtrField<envoy::data::ip_tagging::v3::IPTag>& ip_tags);

private:
  Api::Api& api_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Stats::StatNameSetPtr& stat_name_set_;
};

class IpTagsProvider : public Logger::Loggable<Logger::Id::ip_tagging> {
public:
  IpTagsProvider(const std::string& ip_tags_path, IpTagsLoader& tags_loader,
                 Event::Dispatcher& dispatcher, Api::Api& api, Singleton::InstanceSharedPtr owner)
      : ip_tags_path_(ip_tags_path), tags_loader_(tags_loader), owner_(owner) {
    if (ip_tags_path.empty()) {
      throw EnvoyException("Cannot load tags from empty file path.");
    }
    tags_ = tags_loader_.loadTags(ip_tags_path_);
    ip_tags_reload_dispatcher_ = api.allocateDispatcher("ip_tags_reload_routine");
    ip_tags_file_watcher_ = dispatcher.createFilesystemWatcher();
    ip_tags_reload_thread_ = api.threadFactory().createThread(
        [this]() -> void {
          ENVOY_LOG_MISC(debug, "Started ip_tags_reload_routine");
          THROW_IF_NOT_OK(
              ip_tags_file_watcher_->addWatch(ip_tags_path_, Filesystem::Watcher::Events::MovedTo,
                                              [this](uint32_t) { return onIpTagsFileUpdate(); }));
          ip_tags_reload_dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
        },
        Thread::Options{std::string("ip_tags_reload_routine")});
  }

  ~IpTagsProvider() {
    ENVOY_LOG(debug, "Shutting down ip tags provider");
    if (ip_tags_reload_dispatcher_) {
      ip_tags_reload_dispatcher_->exit();
    }
    if (ip_tags_reload_thread_) {
      ip_tags_reload_thread_->join();
      ip_tags_reload_thread_.reset();
    }
  };

  absl::Status onIpTagsFileUpdate() { return absl::OkStatus(); }

private:
  const std::string ip_tags_path_;
  IpTagsLoader& tags_loader_;
  LcTrieSharedPtr tags_;
  Thread::ThreadPtr ip_tags_reload_thread_;
  Event::DispatcherPtr ip_tags_reload_dispatcher_;
  Filesystem::WatcherPtr ip_tags_file_watcher_;
  // A shared_ptr to keep the provider singleton alive as long as any of its providers are in use.
  const Singleton::InstanceSharedPtr owner_;
};

/**
 * A singleton for file based loading of ip tags and looking up parsed trie data structures with Ip
 * tags. When given equivalent file paths to the Ip tags, the singleton returns pointers to the same
 * trie structure.
 */
class IpTagsRegistrySingleton : public Envoy::Singleton::Instance {
public:
  IpTagsRegistrySingleton() { std::cerr << "****Create " << std::endl; }
  LcTrieSharedPtr get(const std::string& ip_tags_path, IpTagsLoader& tags_loader);

private:
  absl::Mutex mu_;
  //  We keep weak_ptr here so the trie structures can be destroyed if the config is updated to stop
  //  using that trie. Each provider stores shared_ptrs to this singleton, which keeps the singleton
  //  from being destroyed unless it's no longer keeping track of any providers. (The singleton
  //  shared_ptr is *only* held by driver instances.)
  absl::flat_hash_map<size_t, std::weak_ptr<Network::LcTrie::LcTrie<std::string>>>
      ip_tags_registry_ ABSL_GUARDED_BY(mu_);
};

SINGLETON_MANAGER_REGISTRATION(ip_tags_registry);

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

  IpTaggingFilterConfig(const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& config,
                        std::shared_ptr<IpTagsRegistrySingleton> ip_tags_registry,
                        const std::string& stat_prefix, Stats::Scope& scope,
                        Runtime::Loader& runtime, Api::Api& api,
                        ProtobufMessage::ValidationVisitor& validation_visitor);

  Runtime::Loader& runtime() { return runtime_; }
  FilterRequestType requestType() const { return request_type_; }
  const Network::LcTrie::LcTrie<std::string>& trie() const { return *trie_; }

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
  LcTrieSharedPtr trie_;
  const Http::LowerCaseString
      ip_tag_header_; // An empty string indicates that no ip_tag_header is set.
  const HeaderAction ip_tag_header_action_;
  const std::string ip_tags_path_;
  // A shared_ptr to keep the ip tags registry singleton alive as long as any of its trie structures
  // are in use.
  const std::shared_ptr<IpTagsRegistrySingleton> ip_tags_registry_;
  IpTagsLoader tags_loader_;
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
};

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
