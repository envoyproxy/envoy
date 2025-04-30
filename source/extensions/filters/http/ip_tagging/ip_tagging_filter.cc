#include "source/extensions/filters/http/ip_tagging/ip_tagging_filter.h"

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

IpTagsProvider::IpTagsProvider(const std::string& ip_tags_path, IpTagsLoader& tags_loader,
                               IpTagsReloadSuccessCb reload_success_cb,
                               IpTagsReloadErrorCb reload_error_cb, Event::Dispatcher& dispatcher,
                               Api::Api& api, Singleton::InstanceSharedPtr owner, absl::Status& creation_status)
    : ip_tags_path_(ip_tags_path), tags_loader_(tags_loader), reload_success_cb_(reload_success_cb),
      reload_error_cb_(reload_error_cb), tags_(tags_loader_.loadTags(ip_tags_path_, creation_status)),
      ip_tags_reload_dispatcher_(api.allocateDispatcher("ip_tags_reload_routine")),
      ip_tags_file_watcher_(dispatcher.createFilesystemWatcher()), owner_(owner) {
  if (ip_tags_path.empty()) {
    creation_status = absl::InvalidArgumentError("Cannot load tags from empty file path.");
  }
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


IpTagsProvider::~IpTagsProvider() {
  ENVOY_LOG(debug, "Shutting down ip tags provider");
  ip_tags_reload_dispatcher_->exit();
  if (ip_tags_reload_thread_) {
    ip_tags_reload_thread_->join();
    ip_tags_reload_thread_.reset();
  }
};

LcTrieSharedPtr IpTagsProvider::ipTags() const ABSL_LOCKS_EXCLUDED(ip_tags_mutex_) {
  absl::ReaderMutexLock lock(&ip_tags_mutex_);
  return tags_;
};

absl::Status IpTagsProvider::onIpTagsFileUpdate() {
  absl::Status reload_status;
  LcTrieSharedPtr reloaded_tags = tags_loader_.loadTags(ip_tags_path_, reload_status);
  return ipTagsReload(reloaded_tags, reload_status);
}

absl::Status IpTagsProvider::ipTagsReload(const LcTrieSharedPtr reloaded_tags, absl::Status& reload_status) {
  if (reload_status.ok()) {
    updateIpTags(reloaded_tags);
    reload_success_cb_();
  } else {
    reload_error_cb_();
  }
  return absl::OkStatus();
}

void IpTagsProvider::updateIpTags(const LcTrieSharedPtr reloaded_tags)
    ABSL_LOCKS_EXCLUDED(ip_tags_mutex_) {
  absl::MutexLock lock(&ip_tags_mutex_);
  tags_ = reloaded_tags;
}

IpTagsLoader::IpTagsLoader(Api::Api& api, ProtobufMessage::ValidationVisitor& validation_visitor,
                           Stats::StatNameSetPtr& stat_name_set)
    : api_(api), validation_visitor_(validation_visitor), stat_name_set_(stat_name_set) {}

LcTrieSharedPtr IpTagsLoader::loadTags(const std::string& ip_tags_path, absl::Status& creation_status) {
  if (!ip_tags_path.empty()) {
    if (!absl::EndsWith(ip_tags_path, MessageUtil::FileExtensions::get().Yaml) &&
        !absl::EndsWith(ip_tags_path, MessageUtil::FileExtensions::get().Json)) {
      creation_status = absl::InvalidArgumentError("Unsupported file format, unable to parse ip tags from file.");
      return nullptr;
    }
    auto file_or_error = api_.fileSystem().fileReadToEnd(ip_tags_path);
    if (file_or_error.status().ok()) {
      IpTagFileProto ip_tags_proto;
      if (absl::EndsWith(ip_tags_path, MessageUtil::FileExtensions::get().Yaml)) {
        try {
          MessageUtil::loadFromYaml(file_or_error.value(), ip_tags_proto, validation_visitor_);
        } catch (const EnvoyException& e) {
          ENVOY_LOG_MISC(warn, "failed to parse ip tags file: {}", e.what());
          return nullptr;
        }
      } else if (absl::EndsWith(ip_tags_path, MessageUtil::FileExtensions::get().Json)) {
        try {
          MessageUtil::loadFromJson(file_or_error.value(), ip_tags_proto, validation_visitor_);
        } catch (const EnvoyException& e) {
          ENVOY_LOG_MISC(warn, "failed to parse ip tags file: {}", e.what());
          return nullptr;
        }
      }
      return parseIpTags(ip_tags_proto.ip_tags(), creation_status);
    } else {
      return nullptr;
    }
  }
  return nullptr;
}

LcTrieSharedPtr IpTagsLoader::parseIpTags(
    const Protobuf::RepeatedPtrField<envoy::data::ip_tagging::v3::IPTag>& ip_tags, absl::Status& creation_status) {
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
        creation_status = absl::InvalidArgumentError(
            fmt::format("invalid ip/mask combo '{}/{}' (format is <ip>/<# mask bits>)",
                        entry.address_prefix(), entry.prefix_len().value()));
        return nullptr;
      }
    }
    tag_data.emplace_back(ip_tag.ip_tag_name(), cidr_set);
    stat_name_set_->rememberBuiltin(absl::StrCat(ip_tag.ip_tag_name(), ".hit"));
  }
  return std::make_shared<Network::LcTrie::LcTrie<std::string>>(tag_data);
}

SINGLETON_MANAGER_REGISTRATION(ip_tags_registry);

absl::StatusOr<IpTaggingFilterConfigSharedPtr> IpTaggingFilterConfig::create(
    const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& config,
         const std::string& stat_prefix, Singleton::Manager& singleton_manager,
                        Stats::Scope& scope, Runtime::Loader& runtime, Api::Api& api,
                        Event::Dispatcher& dispatcher,
                        ProtobufMessage::ValidationVisitor& validation_visitor) {
  absl::Status creation_status = absl::OkStatus();
  auto config_ptr = std::shared_ptr<IpTaggingFilterConfig>(
      new IpTaggingFilterConfig(config, stat_prefix, singleton_manager, scope, runtime, api, dispatcher, validation_visitor, creation_status));
  RETURN_IF_NOT_OK(creation_status);
  return config_ptr;
}

IpTaggingFilterConfig::IpTaggingFilterConfig(
    const envoy::extensions::filters::http::ip_tagging::v3::IPTagging& config,
    const std::string& stat_prefix, Singleton::Manager& singleton_manager, Stats::Scope& scope,
    Runtime::Loader& runtime, Api::Api& api, Event::Dispatcher& dispatcher,
    ProtobufMessage::ValidationVisitor& validation_visitor, absl::Status& creation_status)
    : request_type_(requestTypeEnum(config.request_type())), scope_(scope), runtime_(runtime),
      stat_name_set_(scope.symbolTable().makeSet("IpTagging")),
      stats_prefix_(stat_name_set_->add(stat_prefix + "ip_tagging")),
      no_hit_(stat_name_set_->add("no_hit")), total_(stat_name_set_->add("total")),
      unknown_tag_(stat_name_set_->add("unknown_tag.hit")),
      ip_tag_header_(config.has_ip_tag_header() ? config.ip_tag_header().header() : ""),
      ip_tag_header_action_(config.has_ip_tag_header()
                                ? config.ip_tag_header().action()
                                : HeaderAction::IPTagging_IpTagHeader_HeaderAction_SANITIZE),
      ip_tags_path_(config.ip_tags_path()),
      ip_tags_registry_(singleton_manager.getTyped<IpTagsRegistrySingleton>(
          SINGLETON_MANAGER_REGISTERED_NAME(ip_tags_registry),
          [] { return std::make_shared<IpTagsRegistrySingleton>(); })),
      tags_loader_(api, validation_visitor, stat_name_set_) {

  // Once loading IP tags from a file system is supported, the restriction on the size
  // of the set should be removed and observability into what tags are loaded needs
  // to be implemented.
  // TODO(ccaraman): Remove size check once file system support is implemented.
  // Work is tracked by issue https://github.com/envoyproxy/envoy/issues/2695.
  if (config.ip_tags().empty() && config.ip_tags_path().empty()) {
    creation_status = absl::InvalidArgumentError("HTTP IP Tagging Filter requires either ip_tags or ip_tags_path to be specified.");
  }

  if (!config.ip_tags().empty() && !config.ip_tags_path().empty()) {
    creation_status = absl::InvalidArgumentError("Only one of ip_tags or ip_tags_path can be configured.");
  }

  if (creation_status.ok()) {
      if (!config.ip_tags().empty()) {
    trie_ = tags_loader_.parseIpTags(config.ip_tags(), creation_status);
  } else {
    provider_ = ip_tags_registry_->get(
        ip_tags_path_, tags_loader_, [this]() { incIpTagsReloadSuccess(); },
        [this]() { incIpTagsReloadError(); }, api, dispatcher, ip_tags_registry_, creation_status);
    if (provider_ && provider_->ipTags()) {
      trie_ = provider_->ipTags();
    } else {
      if (creation_status.ok()) {
        creation_status = absl::InvalidArgumentError("Failed to get ip tags from provider");
      }
    }
    stat_name_set_->rememberBuiltin("ip_tags_reload_success");
    stat_name_set_->rememberBuiltin("ip_tags_reload_error");
  }
  }
}

void IpTaggingFilterConfig::incCounter(Stats::StatName name) {
  Stats::SymbolTable::StoragePtr storage = scope_.symbolTable().join({stats_prefix_, name});
  scope_.counterFromStatName(Stats::StatName(storage.get())).inc();
}

IpTaggingFilter::IpTaggingFilter(IpTaggingFilterConfigSharedPtr config) : config_(config){};

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
  std::cerr << "***Tags from decodeHeaders: " << std::endl;
  for (auto t : tags)
    std::cerr << t << ' ';
  std::cerr << std::endl;
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
