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

IpTagsStats::IpTagsStats(const std::string& stat_prefix, Stats::ScopeSharedPtr scope)
    : scope_(std::move(scope)), stat_name_set_(scope_->symbolTable().makeSet("IpTagging")),
      stats_prefix_(stat_name_set_->add(stat_prefix + "ip_tagging")),
      unknown_tag_(stat_name_set_->add("unknown_tag.hit")), total_(stat_name_set_->add("total")),
      no_hit_(stat_name_set_->add("no_hit")),
      reload_success_(stat_name_set_->add("reload_success")) {}

void IpTagsStats::incHit(absl::string_view tag) {
  incCounter(stat_name_set_->getBuiltin(absl::StrCat(tag, ".hit"), unknown_tag_));
}

void IpTagsStats::incCounter(Stats::StatName name) {
  Stats::SymbolTable::StoragePtr storage = scope_->symbolTable().join({stats_prefix_, name});
  scope_->counterFromStatName(Stats::StatName(storage.get())).inc();
}

absl::StatusOr<LcTrieSharedPtr> IpTagsStats::parseIpTagsAsProto(
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
    stat_name_set_->rememberBuiltin(absl::StrCat(ip_tag.ip_tag_name(), ".hit"));
  }
  return std::make_shared<Network::LcTrie::LcTrie<std::string>>(tag_data);
}

IpTagsProvider::IpTagsProvider(const envoy::config::core::v3::DataSource& ip_tags_datasource,
                               Event::Dispatcher& main_dispatcher, Api::Api& api,
                               ProtobufMessage::ValidationVisitor& validation_visitor,
                               ThreadLocal::SlotAllocator& tls, const std::string& stat_prefix,
                               Stats::ScopeSharedPtr scope, Singleton::InstanceSharedPtr owner,
                               absl::Status& creation_status)
    : owner_(owner) {
  const auto& datasource_filename = ip_tags_datasource.filename();
  if (datasource_filename.empty()) {
    creation_status =
        absl::InvalidArgumentError("Cannot load tags from empty filename in datasource.");
    return;
  }
  if (!absl::EndsWith(datasource_filename, MessageUtil::FileExtensions::get().Yaml) &&
      !absl::EndsWith(datasource_filename, MessageUtil::FileExtensions::get().Json)) {
    creation_status = absl::InvalidArgumentError(
        "Unsupported file format, unable to parse ip tags from file " + datasource_filename);
    return;
  }

  // Each successful load (initial + every file-watcher reload) atomically produces a
  // new LoadedIpTags containing a fresh trie and matching IpTagsStats, which the
  // DataSourceProvider publishes via data(). `first_load` skips the reload_success
  // increment on the initial load so the counter only reflects subsequent reloads. The
  // `scope` shared_ptr is captured by value so the stats scope outlives any individual
  // listener that bootstrapped this provider (providers are shared across listeners
  // via IpTagsRegistrySingleton).
  auto provider_or_error = Config::DataSource::DataSourceProvider<LoadedIpTags>::create(
      ip_tags_datasource, main_dispatcher, tls, api, /*allow_empty=*/false,
      [stat_prefix, scope, &validation_visitor, datasource_filename, first_load = true](
          absl::string_view new_data) mutable -> absl::StatusOr<std::shared_ptr<LoadedIpTags>> {
        IPTagsProto ip_tags_proto;
        if (absl::EndsWith(datasource_filename, MessageUtil::FileExtensions::get().Yaml)) {
          // TODO(nezdolik) remove string casting once yaml utility has been migrated to
          // string_view.
          auto data = std::string(new_data);
          auto load_status =
              MessageUtil::loadFromYamlNoThrow(data, ip_tags_proto, validation_visitor);
          if (!load_status.ok()) {
            return load_status;
          }
        } else if (absl::EndsWith(datasource_filename, MessageUtil::FileExtensions::get().Json)) {
          bool has_unknown_field;
          auto load_status =
              MessageUtil::loadFromJsonNoThrow(new_data, ip_tags_proto, has_unknown_field);
          if (!load_status.ok()) {
            return load_status;
          }
        }
        auto stats = std::make_shared<IpTagsStats>(stat_prefix, scope);
        auto trie_or = stats->parseIpTagsAsProto(ip_tags_proto.ip_tags());
        if (!trie_or.ok()) {
          return trie_or.status();
        }
        if (first_load) {
          first_load = false;
        } else {
          stats->incIpTagsReloadSuccess();
        }
        return std::make_shared<LoadedIpTags>(LoadedIpTags{*std::move(trie_or), std::move(stats)});
      },
      0);
  if (!provider_or_error.status().ok()) {
    creation_status = absl::InvalidArgumentError(
        fmt::format("unable to create data source '{}'", provider_or_error.status().message()));
    return;
  }
  data_source_provider_ = std::move(provider_or_error.value());
}

IpTagsProvider::~IpTagsProvider() { ENVOY_LOG(debug, "Shutting down ip tags provider"); }

absl::StatusOr<std::shared_ptr<IpTagsProvider>> IpTagsRegistrySingleton::getOrCreateProvider(
    const envoy::config::core::v3::DataSource& ip_tags_datasource, Api::Api& api,
    ProtobufMessage::ValidationVisitor& validation_visitor, ThreadLocal::SlotAllocator& tls,
    Event::Dispatcher& main_dispatcher, const std::string& stat_prefix, Stats::Scope& scope,
    std::shared_ptr<IpTagsRegistrySingleton> singleton) {
  // Lazily create a singleton-owned scope on first use. Reusing it across providers
  // keeps the stats scope alive even after the listener that first created the
  // provider goes away, since other listeners may still share this provider.
  if (scope_ == nullptr) {
    scope_ = scope.createScope("");
  }
  const size_t key = std::hash<std::string>()(ip_tags_datasource.filename());
  auto it = ip_tags_registry_.find(key);
  if (it != ip_tags_registry_.end()) {
    if (std::shared_ptr<IpTagsProvider> provider = it->second.lock()) {
      return provider;
    }
  }
  absl::Status creation_status = absl::OkStatus();
  auto ip_tags_provider =
      std::make_shared<IpTagsProvider>(ip_tags_datasource, main_dispatcher, api, validation_visitor,
                                       tls, stat_prefix, scope_, singleton, creation_status);
  if (!creation_status.ok()) {
    return creation_status;
  }
  ip_tags_registry_[key] = ip_tags_provider;
  return ip_tags_provider;
}

SINGLETON_MANAGER_REGISTRATION(ip_tags_registry);

absl::StatusOr<IpTaggingFilterConfigSharedPtr> IpTaggingFilterConfig::create(
    const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& config,
    const std::string& stat_prefix, Singleton::Manager& singleton_manager, Stats::Scope& scope,
    Runtime::Loader& runtime, Api::Api& api, ThreadLocal::SlotAllocator& tls,
    Event::Dispatcher& dispatcher, ProtobufMessage::ValidationVisitor& validation_visitor) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::shared_ptr<IpTaggingFilterConfig>(
      new IpTaggingFilterConfig(config, stat_prefix, singleton_manager, scope, runtime, api, tls,
                                dispatcher, validation_visitor, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

IpTaggingFilterConfig::IpTaggingFilterConfig(
    const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& config,
    const std::string& stat_prefix, Singleton::Manager& singleton_manager, Stats::Scope& scope,
    Runtime::Loader& runtime, Api::Api& api, ThreadLocal::SlotAllocator& tls,
    Event::Dispatcher& dispatcher, ProtobufMessage::ValidationVisitor& validation_visitor,
    absl::Status& creation_status)
    : request_type_(requestTypeEnum(config.request_type())), runtime_(runtime),
      ip_tag_header_(config.has_ip_tag_header() ? config.ip_tag_header().header() : ""),
      ip_tag_header_action_(config.has_ip_tag_header()
                                ? config.ip_tag_header().action()
                                : HeaderAction::IPTagging_IpTagHeader_HeaderAction_SANITIZE),
      ip_tags_registry_(singleton_manager.getTyped<IpTagsRegistrySingleton>(
          SINGLETON_MANAGER_REGISTERED_NAME(ip_tags_registry),
          [] { return std::make_shared<IpTagsRegistrySingleton>(); })) {
  if (config.ip_tags().empty() && !config.has_ip_tags_datasource()) {
    creation_status = absl::InvalidArgumentError(
        "HTTP IP Tagging Filter requires either ip_tags or ip_tags_datasource to be specified.");
    return;
  }

  if (!config.ip_tags().empty() && config.has_ip_tags_datasource()) {
    creation_status =
        absl::InvalidArgumentError("Only one of ip_tags or ip_tags_datasource can be configured.");
    return;
  }

  if (!config.ip_tags().empty()) {
    auto stats = std::make_shared<IpTagsStats>(stat_prefix, scope.createScope(""));
    auto trie_or = stats->parseIpTagsAsProto(config.ip_tags());
    if (!trie_or.ok()) {
      creation_status = trie_or.status();
      return;
    }
    static_ip_tags_ =
        std::make_shared<LoadedIpTags>(LoadedIpTags{*std::move(trie_or), std::move(stats)});
    return;
  }

  auto provider_or_error = ip_tags_registry_->getOrCreateProvider(
      config.ip_tags_datasource(), api, validation_visitor, tls, dispatcher, stat_prefix, scope,
      ip_tags_registry_);
  if (!provider_or_error.status().ok()) {
    creation_status = provider_or_error.status();
    return;
  }
  provider_ = std::move(provider_or_error.value());
}

OptRef<const Http::LowerCaseString> IpTaggingFilterConfig::ipTagHeader() const {
  if (ip_tag_header_.get().empty()) {
    return std::nullopt;
  }
  return ip_tag_header_;
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

  // Pull the current snapshot once so the trie lookup and stats updates that follow
  // come from the same reload, even if a file watcher publishes a new snapshot mid-call.
  LoadedIpTagsConstSharedPtr loaded = config_->loadedIpTags();
  if (loaded == nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }

  std::vector<std::string> tags;
  if (loaded->trie != nullptr) {
    tags =
        loaded->trie->getData(callbacks_->streamInfo().downstreamAddressProvider().remoteAddress());
  }

  // Used for testing.
  synchronizer_.syncPoint("_trie_lookup_complete");
  applyTags(headers, tags);
  if (!tags.empty()) {
    // For a large number(ex > 1000) of tags, stats cardinality will be an issue.
    // If there are use cases with a large set of tags, a way to opt into these stats
    // should be exposed and other observability options like logging tags need to be implemented.
    for (const std::string& tag : tags) {
      loaded->stats->incHit(tag);
    }
  } else {
    loaded->stats->incNoHit();
  }
  loaded->stats->incTotal();
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
