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
                               uint64_t ip_tags_refresh_interval_ms,
                               Event::Dispatcher& main_dispatcher, Api::Api& api,
                               ProtobufMessage::ValidationVisitor& validation_visitor,
                               ThreadLocal::SlotAllocator& tls, Stats::Scope& scope,
                               Singleton::InstanceSharedPtr owner, absl::Status& creation_status)
    : ip_tags_path_(ip_tags_datasource.filename()), scope_(scope.createScope("ip_tagging_reload.")),
      stat_name_set_(scope.symbolTable().makeSet("IpTaggingReload")),
      stats_prefix_(stat_name_set_->add("ip_tagging_reload")),
      unknown_tag_(stat_name_set_->add("unknown_tag.hit")), tags_loader_(api, validation_visitor),
      time_source_(api.timeSource()),
      ip_tags_refresh_interval_ms_(std::chrono::milliseconds(ip_tags_refresh_interval_ms)),
      needs_refresh_(ip_tags_refresh_interval_ms_ > std::chrono::milliseconds(0) &&
                             ip_tags_datasource.has_watched_directory()
                         ? true
                         : false),
      owner_(owner) {
  RETURN_ONLY_IF_NOT_OK_REF(creation_status);
  stat_name_set_->rememberBuiltin("reload_success");
  stat_name_set_->rememberBuiltin("reload_error");
  auto tags_or_error = tags_loader_.loadTags(ip_tags_datasource, main_dispatcher, tls);
  creation_status = tags_or_error.status();
  if (tags_or_error.status().ok()) {
    tags_ = tags_or_error.value();
  }
  ip_tags_reload_timer_ = main_dispatcher.createTimer([this]() -> void {
    ENVOY_LOG(debug, "Trying to update ip tags in background");
    auto new_tags_or_error = tags_loader_.refreshTags();
    if (new_tags_or_error.status().ok()) {
      updateIpTags(new_tags_or_error.value());
      incIpTagsReloadSuccess();
    } else {
      ENVOY_LOG(debug, "Failed to reload ip tags, using old data: {}",
                new_tags_or_error.status().message());
      incIpTagsReloadError();
    }
    ip_tags_reload_timer_->enableTimer(ip_tags_refresh_interval_ms_);
  });
  ip_tags_reload_timer_->enableTimer(ip_tags_refresh_interval_ms_);
}

IpTagsProvider::~IpTagsProvider() {
  if (ip_tags_reload_timer_) {
    ip_tags_reload_timer_->disableTimer();
  }
  ENVOY_LOG(debug, "Shutting down ip tags provider");
};

LcTrieSharedPtr IpTagsProvider::ipTags() ABSL_LOCKS_EXCLUDED(ip_tags_mutex_) {
  absl::ReaderMutexLock lock(&ip_tags_mutex_);
  return tags_;
}

void IpTagsProvider::updateIpTags(LcTrieSharedPtr new_tags) ABSL_LOCKS_EXCLUDED(ip_tags_mutex_) {
  absl::MutexLock lock(&ip_tags_mutex_);
  tags_ = new_tags;
}

void IpTagsProvider::incCounter(Stats::StatName name) {
  Stats::SymbolTable::StoragePtr storage = scope_->symbolTable().join({name});
  scope_->counterFromStatName(Stats::StatName(storage.get())).inc();
}

absl::StatusOr<std::shared_ptr<IpTagsProvider>> IpTagsRegistrySingleton::getOrCreateProvider(
    const envoy::config::core::v3::DataSource& ip_tags_datasource,
    uint64_t ip_tags_refresh_interval_ms, Api::Api& api,
    ProtobufMessage::ValidationVisitor& validation_visitor, ThreadLocal::SlotAllocator& tls,
    Event::Dispatcher& main_dispatcher, Stats::Scope& scope,
    std::shared_ptr<IpTagsRegistrySingleton> singleton) {
  std::shared_ptr<IpTagsProvider> ip_tags_provider;
  absl::Status creation_status = absl::OkStatus();
  const uint64_t key = std::hash<std::string>()(ip_tags_datasource.filename());
  absl::MutexLock lock(&mu_);
  auto it = ip_tags_registry_.find(key);
  if (it != ip_tags_registry_.end()) {
    if (std::shared_ptr<IpTagsProvider> provider = it->second.lock()) {
      ip_tags_provider = provider;
    } else {
      ip_tags_provider = std::make_shared<IpTagsProvider>(
          ip_tags_datasource, ip_tags_refresh_interval_ms, main_dispatcher, api, validation_visitor,
          tls, scope, singleton, creation_status);
      ip_tags_registry_[key] = ip_tags_provider;
    }
  } else {
    ip_tags_provider = std::make_shared<IpTagsProvider>(
        ip_tags_datasource, ip_tags_refresh_interval_ms, main_dispatcher, api, validation_visitor,
        tls, scope, singleton, creation_status);
    ip_tags_registry_[key] = ip_tags_provider;
  }
  if (!creation_status.ok()) {
    return creation_status;
  }
  return ip_tags_provider;
}

IpTagsLoader::IpTagsLoader(Api::Api& api, ProtobufMessage::ValidationVisitor& validation_visitor)
    : api_(api), validation_visitor_(validation_visitor) {}

absl::StatusOr<LcTrieSharedPtr>
IpTagsLoader::loadTags(const envoy::config::core::v3::DataSource& ip_tags_datasource,
                       Event::Dispatcher& main_dispatcher, ThreadLocal::SlotAllocator& tls) {
  if (!ip_tags_datasource.filename().empty()) {
    if (!absl::EndsWith(ip_tags_datasource.filename(), MessageUtil::FileExtensions::get().Yaml) &&
        !absl::EndsWith(ip_tags_datasource.filename(), MessageUtil::FileExtensions::get().Json)) {
      return absl::InvalidArgumentError(
          "Unsupported file format, unable to parse ip tags from file " +
          ip_tags_datasource.filename());
    }
    auto provider_or_error = Config::DataSource::DataSourceProvider::create(
        ip_tags_datasource, main_dispatcher, tls, api_, false, 0);
    if (!provider_or_error.status().ok()) {
      return absl::InvalidArgumentError(
          fmt::format("unable to create data source '{}'", provider_or_error.status().message()));
    }
    data_source_provider_ = std::move(provider_or_error.value());
    ip_tags_path_ = ip_tags_datasource.filename();
    return refreshTags();
  }
  return absl::InvalidArgumentError("Cannot load tags from empty filename in datasource.");
}

absl::StatusOr<LcTrieSharedPtr> IpTagsLoader::refreshTags() {
  if (data_source_provider_) {
    IpTagFileProto ip_tags_proto;
    const auto new_data = data_source_provider_->data();
    if (absl::EndsWith(ip_tags_path_, MessageUtil::FileExtensions::get().Yaml)) {
      auto load_status =
          MessageUtil::loadFromYamlNoThrow(new_data, ip_tags_proto, validation_visitor_);
      if (!load_status.ok()) {
        return load_status;
      }
    } else if (absl::EndsWith(ip_tags_path_, MessageUtil::FileExtensions::get().Json)) {
      bool has_unknown_field;
      auto load_status =
          MessageUtil::loadFromJsonNoThrow(new_data, ip_tags_proto, has_unknown_field);
      if (!load_status.ok()) {
        return load_status;
      }
    }
    return parseIpTagsAsProto(ip_tags_proto.ip_tags());
  } else {
    return absl::InvalidArgumentError("Unable to load tags from empty datasource");
  }
}

absl::StatusOr<LcTrieSharedPtr> IpTagsLoader::parseIpTagsAsProto(
    const Protobuf::RepeatedPtrField<
        envoy::extensions::filters::http::ip_tagging::v3::IPTagging::IPTag>& ip_tags) {
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
        return absl::InvalidArgumentError(
            fmt::format("invalid ip/mask combo '{}/{}' (format is <ip>/<# mask bits>)",
                        entry.address_prefix(), entry.prefix_len().value()));
      }
    }
    tag_data.emplace_back(ip_tag.ip_tag_name(), cidr_set);
  }
  return std::make_shared<Network::LcTrie::LcTrie<std::string>>(tag_data);
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
      tags_loader_(api, validation_visitor) {

  if (config.ip_tags().empty() && !config.has_ip_tags_file_provider()) {
    creation_status = absl::InvalidArgumentError(
        "HTTP IP Tagging Filter requires either ip_tags or ip_tags_file_provider to be specified.");
  }

  if (!config.ip_tags().empty() && config.has_ip_tags_file_provider()) {
    creation_status = absl::InvalidArgumentError(
        "Only one of ip_tags or ip_tags_file_provider can be configured.");
  }

  RETURN_ONLY_IF_NOT_OK_REF(creation_status);
  if (!config.ip_tags().empty()) {
    auto trie_or_error = tags_loader_.parseIpTagsAsProto(config.ip_tags());
    if (trie_or_error.status().ok()) {
      trie_ = trie_or_error.value();
    } else {
      creation_status = trie_or_error.status();
      return;
    }
  } else {
    if (!config.ip_tags_file_provider().has_ip_tags_datasource()) {
      creation_status = absl::InvalidArgumentError(
          "ip_tags_file_provider requires a valid ip_tags_datasource to be configured.");
      return;
    }
    auto ip_tags_refresh_interval_ms =
        PROTOBUF_GET_MS_OR_DEFAULT(config.ip_tags_file_provider(), ip_tags_refresh_rate, 0);
    auto provider_or_error = ip_tags_registry_->getOrCreateProvider(
        config.ip_tags_file_provider().ip_tags_datasource(), ip_tags_refresh_interval_ms, api,
        validation_visitor, tls, dispatcher, scope, ip_tags_registry_);
    if (provider_or_error.status().ok()) {
      provider_ = provider_or_error.value();
    } else {
      creation_status = provider_or_error.status();
      return;
    }
    if (provider_ && provider_->ipTags()) {
      trie_ = provider_->ipTags();
    } else {
      creation_status = absl::InvalidArgumentError("Failed to get ip tags from provider");
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

  // Used for testing.
  synchronizer_.syncPoint("_trie_lookup_complete");
  applyTags(headers, tags);
  if (!tags.empty()) {
    // For a large number(ex > 1000) of tags, stats cardinality will be an issue.
    // If there are use cases with a large set of tags, a way to opt into these stats
    // should be exposed and other observability options like logging tags need to be implemented.
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
        // We must clear the route cache in case it held a decision based on the now-removed header.
        callbacks_->downstreamCallbacks()->clearRouteCache();
      }
    }
    return;
  }

  const std::string tags_join = absl::StrJoin(tags, ",");
  if (!header_name.has_value()) {
    // The x-envoy-ip-tags header was cleared at the start of the filter chain.
    // We only do append here, so that if multiple ip-tagging filters are run sequentially,
    // the behaviour will be backwards compatible.
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

  // We must clear the route cache so it can match on the updated value of the header.
  callbacks_->downstreamCallbacks()->clearRouteCache();
}

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
