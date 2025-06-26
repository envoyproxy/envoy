#include "source/extensions/filters/http/ip_tagging/ip_tagging_filter.h"

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.h"

#include "source/common/config/datasource.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

IpTagsProvider::IpTagsProvider(const envoy::config::core::v3::DataSource& ip_tags_datasource,
                               IpTagsLoader& tags_loader, uint64_t ip_tags_refresh_interval_ms,
                               IpTagsReloadSuccessCb reload_success_cb,
                               IpTagsReloadErrorCb reload_error_cb,
                               Event::Dispatcher& main_dispatcher, Api::Api& api,
                               ProtobufMessage::ValidationVisitor& validation_visitor,
                               Stats::StatNameSetPtr stat_name_set, ThreadLocal::SlotAllocator& tls,
                               Singleton::InstanceSharedPtr owner, absl::Status& creation_status)
    : ip_tags_path_(ip_tags_datasource.filename()),
      ip_tags_datasource_(ip_tags_datasource),
      api_(api),
      validation_visitor_(validation_visitor),
      stat_name_set_(std::move(stat_name_set)),
      time_source_(api.timeSource()),
      ip_tags_refresh_interval_ms_(std::chrono::milliseconds(ip_tags_refresh_interval_ms)),
      needs_refresh_(ip_tags_refresh_interval_ms_ > std::chrono::milliseconds(0) &&
                             ip_tags_datasource.has_watched_directory()
                         ? true
                         : false),
      reload_success_cb_(reload_success_cb), reload_error_cb_(reload_error_cb), owner_(owner) {

  if (ip_tags_datasource.filename().empty()) {
    creation_status = absl::InvalidArgumentError("Cannot load tags from empty file path.");
    return;
  }

  // Load initial tags using the provided loader (only during construction)
  auto tags_or_error = tags_loader.loadTags(ip_tags_datasource, main_dispatcher, tls);
  if (tags_or_error.ok()) {
    tags_ = tags_or_error.value();
    creation_status = absl::OkStatus();
  } else {
    // Don't fail on initial load - create empty trie instead
    ENVOY_LOG(warn, "[ip_tagging] Initial tag loading failed: {}, continuing with empty tags",
              tags_or_error.status().message());
    tags_ = std::make_shared<Network::LcTrie::LcTrie<std::string>>(
        std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
    creation_status = absl::OkStatus();
  }
}

void IpTagsProvider::setupTimer(Event::Dispatcher& main_dispatcher) {
  if (!needs_refresh_) {
    return;
  }

  ip_tags_reload_timer_ = main_dispatcher.createTimer([self = shared_from_this()]() -> void {
    // Early exit conditions
    if (self->is_destroying_.load() || !self->needs_refresh_ ||
        self->ip_tags_datasource_.filename().empty()) {
      if (!self->is_destroying_.load() && self->ip_tags_reload_timer_) {
        self->ip_tags_reload_timer_->enableTimer(self->ip_tags_refresh_interval_ms_);
      }
      return;
    }

    const std::string& file_path = self->ip_tags_datasource_.filename();
    auto reload_result = self->reloadFromFile(file_path);

    if (reload_result.ok()) {
      self->updateIpTags(reload_result.value());
      if (self->reload_success_cb_) self->reload_success_cb_();
      ENVOY_LOG(debug, "[ip_tagging] Successfully reloaded tags from {}", file_path);
    } else {
      // Don't fail on reload - just log and continue with existing tags
      ENVOY_LOG(warn, "[ip_tagging] Failed to reload tags from {}: {}, continuing with existing tags",
                file_path, reload_result.status().message());
      if (self->reload_error_cb_) self->reload_error_cb_();
    }

    if (!self->is_destroying_.load() && self->ip_tags_reload_timer_) {
      self->ip_tags_reload_timer_->enableTimer(self->ip_tags_refresh_interval_ms_);
    }
  });

  ip_tags_reload_timer_->enableTimer(ip_tags_refresh_interval_ms_);
}

IpTagsProvider::~IpTagsProvider() {
  is_destroying_.store(true);
  if (ip_tags_reload_timer_) {
    ip_tags_reload_timer_->disableTimer();
  }
}

LcTrieSharedPtr IpTagsProvider::ipTags() ABSL_LOCKS_EXCLUDED(ip_tags_mutex_) {
  absl::ReaderMutexLock lock(&ip_tags_mutex_);
  return tags_;
}

void IpTagsProvider::updateIpTags(LcTrieSharedPtr new_tags) ABSL_LOCKS_EXCLUDED(ip_tags_mutex_) {
  absl::MutexLock lock(&ip_tags_mutex_);
  tags_ = new_tags;
}

absl::StatusOr<LcTrieSharedPtr> IpTagsProvider::reloadFromFile(const std::string& file_path) {
  // Handle file not found gracefully - don't cause 502 errors
  if (!api_.fileSystem().fileExists(file_path)) {
    ENVOY_LOG(debug, "[ip_tagging] File {} does not exist, keeping existing tags", file_path);
    // Return current tags instead of error
    absl::ReaderMutexLock lock(&ip_tags_mutex_);
    if (tags_) {
      return tags_;
    }
    // If no existing tags, return empty trie
    return std::make_shared<Network::LcTrie::LcTrie<std::string>>(
        std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
  }

  auto file_result = api_.fileSystem().fileReadToEnd(file_path);
  if (!file_result.ok()) {
    ENVOY_LOG(debug, "[ip_tagging] Failed to read file {}: {}, keeping existing tags",
              file_path, file_result.status().message());
    // Return current tags instead of error
    absl::ReaderMutexLock lock(&ip_tags_mutex_);
    if (tags_) {
      return tags_;
    }
    return std::make_shared<Network::LcTrie::LcTrie<std::string>>(
        std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
  }

  const std::string& file_content = file_result.value();
  if (file_content.empty()) {
    ENVOY_LOG(debug, "[ip_tagging] File {} is empty, using empty tags", file_path);
    return std::make_shared<Network::LcTrie::LcTrie<std::string>>(
        std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
  }

  return parseFileContent(file_content, file_path);
}

absl::StatusOr<LcTrieSharedPtr> IpTagsProvider::parseFileContent(const std::string& content, const std::string& file_path) {
  IpTagFileProto ip_tags_proto;

  if (absl::EndsWith(file_path, MessageUtil::FileExtensions::get().Yaml)) {
    auto load_status = MessageUtil::loadFromYamlNoThrow(content, ip_tags_proto, validation_visitor_);
    if (!load_status.ok()) {
      ENVOY_LOG(warn, "[ip_tagging] Failed to parse YAML from {}: {}", file_path, load_status.message());
      // Return current tags instead of error
      absl::ReaderMutexLock lock(&ip_tags_mutex_);
      if (tags_) {
        return tags_;
      }
      return std::make_shared<Network::LcTrie::LcTrie<std::string>>(
          std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
    }
  } else if (absl::EndsWith(file_path, MessageUtil::FileExtensions::get().Json)) {
    bool has_unknown_field;
    auto load_status = MessageUtil::loadFromJsonNoThrow(content, ip_tags_proto, has_unknown_field);
    if (!load_status.ok()) {
      ENVOY_LOG(warn, "[ip_tagging] Failed to parse JSON from {}: {}", file_path, load_status.message());
      // Return current tags instead of error
      absl::ReaderMutexLock lock(&ip_tags_mutex_);
      if (tags_) {
        return tags_;
      }
      return std::make_shared<Network::LcTrie::LcTrie<std::string>>(
          std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
    }
  } else {
    ENVOY_LOG(warn, "[ip_tagging] Unsupported file format for {}", file_path);
    // Return current tags instead of error
    absl::ReaderMutexLock lock(&ip_tags_mutex_);
    if (tags_) {
      return tags_;
    }
    return std::make_shared<Network::LcTrie::LcTrie<std::string>>(
        std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
  }

  // Use the loader's parsing method
  IpTagsLoader temp_loader(api_, validation_visitor_, stat_name_set_);
  return temp_loader.parseIpTagsAsProto(ip_tags_proto.ip_tags());
}

absl::StatusOr<std::shared_ptr<IpTagsProvider>> IpTagsRegistrySingleton::getOrCreateProvider(
    const envoy::config::core::v3::DataSource& ip_tags_datasource, IpTagsLoader& tags_loader,
    uint64_t ip_tags_refresh_interval_ms, IpTagsReloadSuccessCb reload_success_cb,
    IpTagsReloadErrorCb reload_error_cb, Api::Api& api,
    ProtobufMessage::ValidationVisitor& validation_visitor,
    Stats::StatNameSetPtr stat_name_set, ThreadLocal::SlotAllocator& tls,
    Event::Dispatcher& main_dispatcher, std::shared_ptr<IpTagsRegistrySingleton> singleton) {

  const uint64_t key = std::hash<std::string>()(ip_tags_datasource.filename());
  absl::MutexLock lock(&mu_);

  auto it = ip_tags_registry_.find(key);
  if (it != ip_tags_registry_.end()) {
    if (auto provider = it->second.lock()) {
      return provider;
    }
  }

  // Create new provider
  absl::Status creation_status = absl::OkStatus();
  auto ip_tags_provider = std::make_shared<IpTagsProvider>(
      ip_tags_datasource, tags_loader, ip_tags_refresh_interval_ms, reload_success_cb,
      reload_error_cb, main_dispatcher, api, validation_visitor, std::move(stat_name_set), tls, singleton, creation_status);

  if (!creation_status.ok()) {
    return creation_status;
  }

  ip_tags_provider->setupTimer(main_dispatcher);
  ip_tags_registry_[key] = ip_tags_provider;

  return ip_tags_provider;
}

const std::string& IpTagsLoader::getDataSourceData() {
  ENVOY_LOG(debug, "[ip_tagging] getDataSourceData() starting");
  if (!data_source_provider_) {
    ENVOY_LOG(debug, "[ip_tagging] data_source_provider_ is null, returning empty data");
    data_ = "";
    return data_;
  }

  ENVOY_LOG(debug, "[ip_tagging] Calling data_source_provider_->data()");
  if (data_source_provider_.get() == nullptr) {
    ENVOY_LOG(debug, "[ip_tagging] data_source_provider_ pointer is null, returning empty data");
    data_ = "";
    return data_;
  }

  data_ = data_source_provider_->data();
  ENVOY_LOG(debug, "[ip_tagging] Got data from provider, size: {} bytes", data_.size());

  return data_;
}

IpTagsLoader::IpTagsLoader(Api::Api& api, ProtobufMessage::ValidationVisitor& validation_visitor,
                           Stats::StatNameSetPtr& stat_name_set)
    : api_(api), validation_visitor_(validation_visitor), stat_name_set_(stat_name_set) {}

absl::StatusOr<LcTrieSharedPtr>
IpTagsLoader::loadTags(const envoy::config::core::v3::DataSource& ip_tags_datasource,
                       Event::Dispatcher& main_dispatcher, ThreadLocal::SlotAllocator& tls) {
  if (!ip_tags_datasource.filename().empty()) {
    if (!absl::EndsWith(ip_tags_datasource.filename(), MessageUtil::FileExtensions::get().Yaml) &&
        !absl::EndsWith(ip_tags_datasource.filename(), MessageUtil::FileExtensions::get().Json)) {
      ENVOY_LOG(warn, "[ip_tagging] Unsupported file format for {}, returning empty tags",
                ip_tags_datasource.filename());
      return std::make_shared<Network::LcTrie::LcTrie<std::string>>(
          std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
    }

    auto provider_or_error = Config::DataSource::DataSourceProvider::create(
        ip_tags_datasource, main_dispatcher, tls, api_, false, 0);
    if (!provider_or_error.status().ok()) {
      ENVOY_LOG(warn, "[ip_tagging] Unable to create data source '{}': {}, returning empty tags",
                ip_tags_datasource.filename(), provider_or_error.status().message());
      return std::make_shared<Network::LcTrie::LcTrie<std::string>>(
          std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
    }

    data_source_provider_ = std::move(provider_or_error.value());
    ip_tags_path_ = ip_tags_datasource.filename();
    return refreshTags();
  }

  ENVOY_LOG(warn, "[ip_tagging] Cannot load tags from empty filename, returning empty tags");
  return std::make_shared<Network::LcTrie::LcTrie<std::string>>(
      std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
}

absl::StatusOr<LcTrieSharedPtr> IpTagsLoader::refreshTags() {
  if (!data_source_provider_) {
    ENVOY_LOG(warn, "[ip_tagging] Unable to load tags from empty datasource, returning empty tags");
    return std::make_shared<Network::LcTrie::LcTrie<std::string>>(
        std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
  }

  IpTagFileProto ip_tags_proto;
  const auto new_data = data_source_provider_->data();

  if (new_data.empty()) {
    ENVOY_LOG(debug, "[ip_tagging] Data source returned empty data, returning empty tags");
    return std::make_shared<Network::LcTrie::LcTrie<std::string>>(
        std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
  }

  if (absl::EndsWith(ip_tags_path_, MessageUtil::FileExtensions::get().Yaml)) {
    auto load_status = MessageUtil::loadFromYamlNoThrow(new_data, ip_tags_proto, validation_visitor_);
    if (!load_status.ok()) {
      ENVOY_LOG(warn, "[ip_tagging] Failed to parse YAML: {}, returning empty tags", load_status.message());
      return std::make_shared<Network::LcTrie::LcTrie<std::string>>(
          std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
    }
  } else if (absl::EndsWith(ip_tags_path_, MessageUtil::FileExtensions::get().Json)) {
    bool has_unknown_field;
    auto load_status = MessageUtil::loadFromJsonNoThrow(new_data, ip_tags_proto, has_unknown_field);
    if (!load_status.ok()) {
      ENVOY_LOG(warn, "[ip_tagging] Failed to parse JSON: {}, returning empty tags", load_status.message());
      return std::make_shared<Network::LcTrie::LcTrie<std::string>>(
          std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
    }
  }

  return parseIpTagsAsProto(ip_tags_proto.ip_tags());
}

absl::StatusOr<LcTrieSharedPtr> IpTagsLoader::parseIpTagsAsProto(
    const Protobuf::RepeatedPtrField<
        envoy::extensions::filters::http::ip_tagging::v3::IPTagging::IPTag>& ip_tags) {

  if (ip_tags.empty()) {
    ENVOY_LOG(debug, "[ip_tagging] No IP tags found, returning empty trie");
    return std::make_shared<Network::LcTrie::LcTrie<std::string>>(
        std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
  }

  std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>> tag_data;
  tag_data.reserve(ip_tags.size());

  for (const auto& ip_tag : ip_tags) {
    std::vector<Network::Address::CidrRange> cidr_set;
    cidr_set.reserve(ip_tag.ip_list().size());

    for (const envoy::config::core::v3::CidrRange& entry : ip_tag.ip_list()) {
      absl::StatusOr<Network::Address::CidrRange> cidr_or_error =
          Network::Address::CidrRange::create(entry);
      if (cidr_or_error.status().ok()) {
        cidr_set.emplace_back(std::move(cidr_or_error.value()));
      } else {
        ENVOY_LOG(warn, "[ip_tagging] Invalid IP/mask combo '{}/{}', skipping",
                  entry.address_prefix(), entry.prefix_len().value());
        // Skip invalid entries instead of failing
        continue;
      }
    }

    if (!cidr_set.empty()) {
      tag_data.emplace_back(ip_tag.ip_tag_name(), std::move(cidr_set));
      stat_name_set_->rememberBuiltin(absl::StrCat(ip_tag.ip_tag_name(), ".hit"));
    }
  }

  return std::make_shared<Network::LcTrie::LcTrie<std::string>>(std::move(tag_data));
}

SINGLETON_MANAGER_REGISTRATION(ip_tags_registry);

absl::StatusOr<IpTaggingFilterConfigSharedPtr> IpTaggingFilterConfig::create(
    const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& config,
    const std::string& stat_prefix, Singleton::Manager& singleton_manager, Stats::Scope& scope,
    Runtime::Loader& runtime, Api::Api& api, ThreadLocal::SlotAllocator& tls,
    Event::Dispatcher& dispatcher, ProtobufMessage::ValidationVisitor& validation_visitor) {
  absl::Status creation_status = absl::OkStatus();
  auto config_ptr = std::shared_ptr<IpTaggingFilterConfig>(
      new IpTaggingFilterConfig(config, stat_prefix, singleton_manager, scope, runtime, api, tls,
                                dispatcher, validation_visitor, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return config_ptr;
}

IpTaggingFilterConfig::IpTaggingFilterConfig(
    const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& config,
    const std::string& stat_prefix, Singleton::Manager& singleton_manager, Stats::Scope& scope,
    Runtime::Loader& runtime, Api::Api& api, ThreadLocal::SlotAllocator& tls,
    Event::Dispatcher& dispatcher, ProtobufMessage::ValidationVisitor& validation_visitor,
    absl::Status& creation_status)
    : request_type_(requestTypeEnum(config.request_type())), scope_(scope), runtime_(runtime),
      stat_name_set_(scope.symbolTable().makeSet("IpTagging")),
      stats_prefix_(stat_name_set_->add(stat_prefix + "ip_tagging")),
      no_hit_(stat_name_set_->add("no_hit")), total_(stat_name_set_->add("total")),
      unknown_tag_(stat_name_set_->add("unknown_tag.hit")),
      ip_tag_header_(config.has_ip_tag_header() ? config.ip_tag_header().header() : ""),
      ip_tag_header_action_(config.has_ip_tag_header()
                                ? config.ip_tag_header().action()
                                : HeaderAction::IPTagging_IpTagHeader_HeaderAction_SANITIZE),
      ip_tags_registry_(singleton_manager.getTyped<IpTagsRegistrySingleton>(
          SINGLETON_MANAGER_REGISTERED_NAME(ip_tags_registry),
          [] { return std::make_shared<IpTagsRegistrySingleton>(); })),
      tags_loader_(api, validation_visitor, stat_name_set_) {

  if (config.ip_tags().empty() && !config.has_ip_tags_file_provider()) {
    creation_status = absl::InvalidArgumentError(
        "HTTP IP Tagging Filter requires either ip_tags or ip_tags_file_provider to be specified.");
    return;
  }

  if (!config.ip_tags().empty() && config.has_ip_tags_file_provider()) {
    creation_status = absl::InvalidArgumentError(
        "Only one of ip_tags or ip_tags_file_provider can be configured.");
    return;
  }

  if (!config.ip_tags().empty()) {
    // Inline configuration
    auto trie_or_error = tags_loader_.parseIpTagsAsProto(config.ip_tags());
    if (trie_or_error.status().ok()) {
      trie_ = trie_or_error.value();
    } else {
      ENVOY_LOG(warn, "[ip_tagging] Failed to parse inline tags: {}, using empty tags",
                trie_or_error.status().message());
      trie_ = std::make_shared<Network::LcTrie::LcTrie<std::string>>(
          std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
    }
  } else {
    // File-based configuration
    if (!config.ip_tags_file_provider().has_ip_tags_datasource()) {
      creation_status = absl::InvalidArgumentError(
          "ip_tags_file_provider requires a valid ip_tags_datasource to be configured.");
      return;
    }

    auto ip_tags_refresh_interval_ms =
        PROTOBUF_GET_MS_OR_DEFAULT(config.ip_tags_file_provider(), ip_tags_refresh_rate, 0);

    // Simple no-op callbacks
    auto reload_success_cb = []() {
      ENVOY_LOG_MISC(debug, "[ip_tagging] IP tags reloaded successfully");
    };

    auto reload_error_cb = []() {
      ENVOY_LOG_MISC(debug, "[ip_tagging] IP tags reload failed");
    };

    auto provider_stat_name_set = scope.symbolTable().makeSet("IpTagging");
    auto provider_or_error = ip_tags_registry_->getOrCreateProvider(
        config.ip_tags_file_provider().ip_tags_datasource(), tags_loader_,
        ip_tags_refresh_interval_ms, reload_success_cb, reload_error_cb, api, validation_visitor,
        std::move(provider_stat_name_set), tls, dispatcher, ip_tags_registry_);

    if (provider_or_error.status().ok()) {
      provider_ = provider_or_error.value();
      if (provider_ && provider_->ipTags()) {
        trie_ = provider_->ipTags();
      } else {
        // Use empty trie as fallback
        trie_ = std::make_shared<Network::LcTrie::LcTrie<std::string>>(
            std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
      }
    } else {
      ENVOY_LOG(warn, "[ip_tagging] Failed to create provider: {}, using empty tags",
                provider_or_error.status().message());
      // Use empty trie as fallback instead of failing
      trie_ = std::make_shared<Network::LcTrie::LcTrie<std::string>>(
          std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>());
    }
  }
}

void IpTaggingFilterConfig::incCounter(Stats::StatName name) {
  Stats::SymbolTable::StoragePtr storage = scope_.symbolTable().join({stats_prefix_, name});
  scope_.counterFromStatName(Stats::StatName(storage.get())).inc();
}

IpTaggingFilter::IpTaggingFilter(IpTaggingFilterConfigSharedPtr config) : config_(config) {}

IpTaggingFilter::~IpTaggingFilter() = default;

void IpTaggingFilter::onDestroy() {}

Http::FilterHeadersStatus IpTaggingFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  const bool is_internal_request = headers.EnvoyInternalRequest() &&
                                   (headers.EnvoyInternalRequest()->value() ==
                                    Http::Headers::get().EnvoyInternalRequestValues.True.c_str());

  if ((is_internal_request && config_->requestType() == FilterRequestType::EXTERNAL) ||
      (!is_internal_request && config_->requestType() == FilterRequestType::INTERNAL) ||
      !config_->runtime().snapshot().featureEnabled("ip_tagging.http_filter_enabled", 100)) {
    return Http::FilterHeadersStatus::Continue;
  }

  std::vector<std::string> tags =
      config_->trie().getData(callbacks_->streamInfo().downstreamAddressProvider().remoteAddress());

  synchronizer_.syncPoint("_trie_lookup_complete");
  applyTags(headers, tags);

  if (!tags.empty()) {
    for (const std::string& tag : tags) {
      config_->incHit(tag);
    }
  } else {
    config_->incNoHit();
  }
  config_->incTotal();
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus IpTaggingFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus IpTaggingFilter::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void IpTaggingFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

void IpTaggingFilter::applyTags(Http::RequestHeaderMap& headers,
                                const std::vector<std::string>& tags) {
  using HeaderAction = IpTaggingFilterConfig::HeaderAction;

  OptRef<const Http::LowerCaseString> header_name = config_->ipTagHeader();

  if (tags.empty()) {
    bool maybe_sanitize =
        config_->ipTagHeaderAction() == HeaderAction::IPTagging_IpTagHeader_HeaderAction_SANITIZE;
    if (header_name.has_value() && maybe_sanitize) {
      if (headers.remove(header_name.value()) != 0) {
        callbacks_->downstreamCallbacks()->clearRouteCache();
      }
    }
    return;
  }

  const std::string tags_join = absl::StrJoin(tags, ",");
  if (!header_name.has_value()) {
    headers.appendEnvoyIpTags(tags_join, ",");
  } else {
    switch (config_->ipTagHeaderAction()) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case HeaderAction::IPTagging_IpTagHeader_HeaderAction_SANITIZE:
      headers.setCopy(header_name.value(), tags_join);
      break;
    case HeaderAction::IPTagging_IpTagHeader_HeaderAction_APPEND_IF_EXISTS_OR_ADD:
      headers.appendCopy(header_name.value(), tags_join);
      break;
    }
  }

  callbacks_->downstreamCallbacks()->clearRouteCache();
}

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy