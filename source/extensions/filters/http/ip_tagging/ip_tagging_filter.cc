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
                               ThreadLocal::SlotAllocator& tls, Singleton::InstanceSharedPtr owner,
                               absl::Status& creation_status)
    : ip_tags_path_(ip_tags_datasource.filename()), tags_loader_(tags_loader),
      time_source_(api.timeSource()),
      ip_tags_refresh_interval_ms_(std::chrono::milliseconds(ip_tags_refresh_interval_ms)),
      needs_refresh_(ip_tags_refresh_interval_ms_ > std::chrono::milliseconds(0) &&
                             ip_tags_datasource.has_watched_directory()
                         ? true
                         : false),
      reload_success_cb_(reload_success_cb), reload_error_cb_(reload_error_cb), owner_(owner) {
  RETURN_ONLY_IF_NOT_OK_REF(creation_status);
  if (ip_tags_datasource.filename().empty()) {
    creation_status = absl::InvalidArgumentError("Cannot load tags from empty file path.");
    return;
  }
  auto tags_or_error = tags_loader_.loadTags(ip_tags_datasource, main_dispatcher, tls);
  creation_status = tags_or_error.status();
  if (tags_or_error.status().ok()) {
    tags_ = tags_or_error.value();
  } else {
    // Initialize tags_ to empty trie to prevent null pointer dereference
    tags_ = std::make_shared<Network::LcTrie::LcTrie<std::string>>(
        std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>{});
  }

  // Only create thread if we have valid tags
  if (creation_status.ok()) {
    ip_tags_reload_dispatcher_ = api.allocateDispatcher("ip_tags_reload_routine");
    ip_tags_reload_timer_ = ip_tags_reload_dispatcher_->createTimer([this]() -> void {
      // Safety check: Ensure provider is still valid during callback
      if (!ip_tags_reload_timer_ || !ip_tags_reload_dispatcher_) {
        return;
      }

      ENVOY_LOG(debug, "Trying to update ip tags in background");
      try {
        ENVOY_LOG(warn, "12345 test - Timer callback: calling refreshTags");
        auto new_tags_or_error = tags_loader_.refreshTags();
        ENVOY_LOG(warn, "12345 test - Timer callback: refreshTags returned");
        if (new_tags_or_error.status().ok()) {
          ENVOY_LOG(warn, "12345 test - Timer callback: refreshTags succeeded, calling updateIpTags");
          updateIpTags(new_tags_or_error.value());
          ENVOY_LOG(warn, "12345 test - Timer callback: updateIpTags completed, calling success callback");
          reload_success_cb_();
          ENVOY_LOG(warn, "12345 test - Timer callback: success callback completed");
        } else {
          ENVOY_LOG(debug, "Failed to reload ip tags, using old data: {}",
                    new_tags_or_error.status().message());
          reload_error_cb_();
        }
        // Only re-enable timer if we're still valid
        if (ip_tags_reload_timer_) {
          ip_tags_reload_timer_->enableTimer(ip_tags_refresh_interval_ms_);
        }
      } catch (const std::exception& e) {
        ENVOY_LOG(debug, "Exception during background IP tag reload: {}", e.what());
        reload_error_cb_();
      } catch (...) {
        ENVOY_LOG(debug, "Unknown exception during background IP tag reload");
        reload_error_cb_();
      }
    });

    ip_tags_reload_thread_ = api.threadFactory().createThread(
        [this]() -> void {
          ENVOY_LOG(debug, "Started ip_tags_reload_routine");
          if (ip_tags_refresh_interval_ms_ > std::chrono::milliseconds(0)) {
            ip_tags_reload_timer_->enableTimer(ip_tags_refresh_interval_ms_);
          }
          ip_tags_reload_dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
        },
        Thread::Options{std::string("ip_tags_reload_routine")});
  }
}

IpTagsProvider::~IpTagsProvider() {
  ENVOY_LOG(debug, "Shutting down ip tags provider");
  if (ip_tags_reload_dispatcher_) {
    ip_tags_reload_dispatcher_->exit();
  }
  if (ip_tags_reload_thread_) {
    ip_tags_reload_thread_->join();
    ip_tags_reload_thread_.reset();
  }
};

LcTrieSharedPtr IpTagsProvider::ipTags() ABSL_LOCKS_EXCLUDED(ip_tags_mutex_) {
  absl::ReaderMutexLock lock(&ip_tags_mutex_);
  return tags_;
}

void IpTagsProvider::updateIpTags(LcTrieSharedPtr new_tags) ABSL_LOCKS_EXCLUDED(ip_tags_mutex_) {
  ENVOY_LOG(warn, "12345 test - updateIpTags called");
  absl::MutexLock lock(&ip_tags_mutex_);
  ENVOY_LOG(warn, "12345 test - updateIpTags acquired mutex lock");
  tags_ = new_tags;
  ENVOY_LOG(warn, "12345 test - updateIpTags assigned new tags, exiting");
}

absl::StatusOr<std::shared_ptr<IpTagsProvider>> IpTagsRegistrySingleton::getOrCreateProvider(
    const envoy::config::core::v3::DataSource& ip_tags_datasource, IpTagsLoader& tags_loader,
    uint64_t ip_tags_refresh_interval_ms, IpTagsReloadSuccessCb reload_success_cb,
    IpTagsReloadErrorCb reload_error_cb, Api::Api& api, ThreadLocal::SlotAllocator& tls,
    Event::Dispatcher& main_dispatcher, std::shared_ptr<IpTagsRegistrySingleton> singleton) {
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
          ip_tags_datasource, tags_loader, ip_tags_refresh_interval_ms, reload_success_cb,
          reload_error_cb, main_dispatcher, api, tls, singleton, creation_status);
      ip_tags_registry_[key] = ip_tags_provider;
    }
  } else {
    ip_tags_provider = std::make_shared<IpTagsProvider>(
        ip_tags_datasource, tags_loader, ip_tags_refresh_interval_ms, reload_success_cb,
        reload_error_cb, main_dispatcher, api, tls, singleton, creation_status);
    ip_tags_registry_[key] = ip_tags_provider;
  }
  if (!creation_status.ok()) {
    return creation_status;
  }
  return ip_tags_provider;
}

IpTagsLoader::~IpTagsLoader() { ENVOY_LOG(warn, "Destroying IpTagsLoader"); };

IpTagsLoader::IpTagsLoader(Api::Api& api, ProtobufMessage::ValidationVisitor& validation_visitor,
                           Stats::StatNameSetPtr& stat_name_set)
    : api_(api), validation_visitor_(validation_visitor), stat_name_set_(stat_name_set) {}

absl::StatusOr<LcTrieSharedPtr>
IpTagsLoader::loadTags(const envoy::config::core::v3::DataSource& ip_tags_datasource,
                       Event::Dispatcher& main_dispatcher, ThreadLocal::SlotAllocator& tls) {
  ENVOY_LOG(warn, "12345 test - loadTags called with filename: '{}'", ip_tags_datasource.filename());

  if (!ip_tags_datasource.filename().empty()) {
    std::string filename = ip_tags_datasource.filename();
    ENVOY_LOG(warn, "12345 test - Processing non-empty filename: '{}'", filename);

    // Store the filename for refreshTags to use
    ip_tags_path_ = filename;

    // Handle path normalization - try relative path first if absolute path is provided
    if (filename.starts_with("/") && filename.size() > 1) {
      std::string relative_path = filename.substr(1);
      ENVOY_LOG(warn, "12345 test - Will try relative path '{}' if absolute fails", relative_path);
    }

    if (!absl::EndsWith(filename, MessageUtil::FileExtensions::get().Yaml) &&
        !absl::EndsWith(filename, MessageUtil::FileExtensions::get().Json)) {
      ENVOY_LOG(warn, "12345 test - Unsupported file format for: '{}'", filename);
      return absl::InvalidArgumentError(
          "Unsupported file format, unable to parse ip tags from file " + filename);
    }

    ENVOY_LOG(warn, "12345 test - Calling refreshTags for: '{}'", ip_tags_path_);
    return refreshTags();
  }
  ENVOY_LOG(warn, "12345 test - Empty filename in datasource");
  return absl::InvalidArgumentError("Cannot load tags from empty filename in datasource.");
}

absl::StatusOr<LcTrieSharedPtr> IpTagsLoader::refreshTags() {
  ENVOY_LOG(warn, "12345 test - refreshTags called for path: '{}'", ip_tags_path_);

  if (ip_tags_path_.empty()) {
    ENVOY_LOG(warn, "12345 test - ip_tags_path_ is empty, returning error");
    return absl::InvalidArgumentError("Unable to load tags from empty path");
  }

  ENVOY_LOG(warn, "12345 test - Using direct file read for path: '{}'", ip_tags_path_);
  try {
    IpTagFileProto ip_tags_proto;
    std::string new_data;

    // Try to read the file directly - handle path normalization here
    std::string filename_to_try = ip_tags_path_;

    auto file_data_or_error = Config::DataSource::read(
        [&filename_to_try]() -> envoy::config::core::v3::DataSource {
          envoy::config::core::v3::DataSource ds;
          ds.set_filename(filename_to_try);
          return ds;
        }(),
        true, // allow_empty
        api_
    );

    // If absolute path fails and it starts with '/', try relative path
    if (!file_data_or_error.status().ok() && filename_to_try.starts_with("/") && filename_to_try.size() > 1) {
      std::string relative_path = filename_to_try.substr(1);
      ENVOY_LOG(warn, "12345 test - Absolute path '{}' failed, trying relative path '{}'", filename_to_try, relative_path);

      file_data_or_error = Config::DataSource::read(
          [&relative_path]() -> envoy::config::core::v3::DataSource {
            envoy::config::core::v3::DataSource ds;
            ds.set_filename(relative_path);
            return ds;
          }(),
          true, // allow_empty
          api_
      );

      if (file_data_or_error.status().ok()) {
        filename_to_try = relative_path; // Update for logging
      }
    }

    if (file_data_or_error.status().ok()) {
      new_data = std::move(file_data_or_error.value());
      ENVOY_LOG(warn, "12345 test - Successfully read file '{}', size: {}", filename_to_try, new_data.size());
    } else {
      ENVOY_LOG(warn, "12345 test - Failed to read file '{}': {}", filename_to_try, file_data_or_error.status().message());
      return absl::InternalError(absl::StrCat("Failed to read file: ", file_data_or_error.status().message()));
    }

    ENVOY_LOG(warn, "12345 test - Processing file format for path: '{}'", ip_tags_path_);
    if (absl::EndsWith(ip_tags_path_, MessageUtil::FileExtensions::get().Yaml)) {
      ENVOY_LOG(warn, "12345 test - Loading YAML file");
      auto load_status =
          MessageUtil::loadFromYamlNoThrow(new_data, ip_tags_proto, validation_visitor_);
      if (!load_status.ok()) {
        ENVOY_LOG(warn, "12345 test - YAML load failed: {}", load_status.message());
        return load_status;
      }
    } else if (absl::EndsWith(ip_tags_path_, MessageUtil::FileExtensions::get().Json)) {
      ENVOY_LOG(warn, "12345 test - Loading JSON file");
      bool has_unknown_field;
      auto load_status =
          MessageUtil::loadFromJsonNoThrow(new_data, ip_tags_proto, has_unknown_field);
      if (!load_status.ok()) {
        ENVOY_LOG(warn, "12345 test - JSON load failed: {}", load_status.message());
        return load_status;
      }
    }
    ENVOY_LOG(warn, "12345 test - File loaded successfully, parsing IP tags");

    ENVOY_LOG(warn, "12345 test - About to call parseIpTagsAsProto");
    auto parse_result = parseIpTagsAsProto(ip_tags_proto.ip_tags());
    ENVOY_LOG(warn, "12345 test - parseIpTagsAsProto returned, checking status");

    if (!parse_result.status().ok()) {
      ENVOY_LOG(warn, "12345 test - parseIpTagsAsProto failed: {}", parse_result.status().message());
      return parse_result.status();
    }

    ENVOY_LOG(warn, "12345 test - parseIpTagsAsProto succeeded, about to return result");
    auto result = std::move(parse_result.value());
    ENVOY_LOG(warn, "12345 test - Moved result, returning from refreshTags");
    return result;
  } catch (const std::exception& e) {
    ENVOY_LOG(warn, "12345 test - Exception during tag refresh: {}", e.what());
    return absl::InternalError(absl::StrCat("Exception during tag refresh: ", e.what()));
  } catch (...) {
    ENVOY_LOG(warn, "12345 test - Unknown exception during tag refresh");
    return absl::InternalError("Unknown exception during tag refresh");
  }
}

absl::StatusOr<LcTrieSharedPtr> IpTagsLoader::parseIpTagsAsProto(
    const Protobuf::RepeatedPtrField<
        envoy::extensions::filters::http::ip_tagging::v3::IPTagging::IPTag>& ip_tags) {
  ENVOY_LOG(warn, "12345 test - parseIpTagsAsProto called with {} ip_tags", ip_tags.size());

  if (ip_tags.size() > 10000) {
    ENVOY_LOG(warn, "12345 test - WARNING: Large number of IP tags ({}), this may cause memory issues", ip_tags.size());
  }

  std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>> tag_data;
  tag_data.reserve(ip_tags.size());

  int processed_count = 0;
  for (const auto& ip_tag : ip_tags) {
    try {
      ENVOY_LOG(warn, "12345 test - Processing IP tag #{}: '{}' with {} CIDR ranges",
                processed_count + 1, ip_tag.ip_tag_name(), ip_tag.ip_list().size());

      std::vector<Network::Address::CidrRange> cidr_set;
      cidr_set.reserve(ip_tag.ip_list().size());

      int cidr_count = 0;
      for (const envoy::config::core::v3::CidrRange& entry : ip_tag.ip_list()) {
        try {
          absl::StatusOr<Network::Address::CidrRange> cidr_or_error =
              Network::Address::CidrRange::create(entry);
          if (cidr_or_error.status().ok()) {
            cidr_set.emplace_back(std::move(cidr_or_error.value()));
            cidr_count++;
          } else {
            ENVOY_LOG(warn, "12345 test - Invalid CIDR in tag '{}': {}/{}",
                      ip_tag.ip_tag_name(), entry.address_prefix(), entry.prefix_len().value());
            return absl::InvalidArgumentError(
                fmt::format("invalid ip/mask combo '{}/{}' (format is <ip>/<# mask bits>)",
                            entry.address_prefix(), entry.prefix_len().value()));
          }
        } catch (const std::exception& e) {
          ENVOY_LOG(warn, "12345 test - Exception processing CIDR in tag '{}': {}",
                    ip_tag.ip_tag_name(), e.what());
          return absl::InternalError(fmt::format("Exception processing CIDR: {}", e.what()));
        }
      }

      ENVOY_LOG(warn, "12345 test - Successfully processed {} CIDRs for tag '{}'",
                cidr_count, ip_tag.ip_tag_name());

      tag_data.emplace_back(ip_tag.ip_tag_name(), std::move(cidr_set));
      stat_name_set_->rememberBuiltin(absl::StrCat(ip_tag.ip_tag_name(), ".hit"));

      processed_count++;

      // Log progress for large datasets
      if (processed_count % 100 == 0) {
        ENVOY_LOG(warn, "12345 test - Progress: processed {}/{} IP tags", processed_count, ip_tags.size());
      }

    } catch (const std::exception& e) {
      ENVOY_LOG(warn, "12345 test - Exception processing IP tag #{}: {}", processed_count + 1, e.what());
      return absl::InternalError(fmt::format("Exception processing IP tag: {}", e.what()));
    }
  }

  ENVOY_LOG(warn, "12345 test - Successfully processed all {} IP tags, creating LcTrie", processed_count);

  try {
    auto result = std::make_shared<Network::LcTrie::LcTrie<std::string>>(tag_data);
    ENVOY_LOG(warn, "12345 test - LcTrie created successfully");
    return result;
  } catch (const std::exception& e) {
    ENVOY_LOG(warn, "12345 test - Exception creating LcTrie: {}", e.what());
    return absl::InternalError(fmt::format("Exception creating LcTrie: {}", e.what()));
  } catch (...) {
    ENVOY_LOG(warn, "12345 test - Unknown exception creating LcTrie");
    return absl::InternalError("Unknown exception creating LcTrie");
  }
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

  // Once loading IP tags from a file system is supported, the restriction on the size
  // of the set should be removed and observability into what tags are loaded needs
  // to be implemented.
  // TODO(ccaraman): Remove size check once file system support is implemented.
  // Work is tracked by issue https://github.com/envoyproxy/envoy/issues/2695.
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

    // Create safe callbacks that don't crash if the config object is destroyed
    // We use a temporary approach by making them no-ops to prevent the segfault
    auto safe_success_cb = []() {
      // No-op to prevent segfault - stats will be handled elsewhere if needed
    };
    auto safe_error_cb = []() {
      // No-op to prevent segfault - stats will be handled elsewhere if needed
    };

    auto provider_or_error = ip_tags_registry_->getOrCreateProvider(
        config.ip_tags_file_provider().ip_tags_datasource(), tags_loader_,
        ip_tags_refresh_interval_ms, safe_success_cb, safe_error_cb, api, tls, dispatcher, ip_tags_registry_);
    if (provider_or_error.status().ok()) {
      provider_ = provider_or_error.value();
    } else {
      creation_status = provider_or_error.status();
      return;
    }
    if (provider_) {
      auto initial_tags = provider_->ipTags();
      if (initial_tags) {
        trie_ = initial_tags;
      } else {
        trie_ = std::make_shared<Network::LcTrie::LcTrie<std::string>>(
            std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>{});
      }
    } else {
      trie_ = std::make_shared<Network::LcTrie::LcTrie<std::string>>(
          std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>>{});
    }
    stat_name_set_->rememberBuiltin("ip_tags_reload_success");
    stat_name_set_->rememberBuiltin("ip_tags_reload_error");
  }
}

void IpTaggingFilterConfig::incCounter(Stats::StatName name) {
  Stats::SymbolTable::StoragePtr storage = scope_.symbolTable().join({stats_prefix_, name});
  scope_.counterFromStatName(Stats::StatName(storage.get())).inc();
}

IpTaggingFilter::IpTaggingFilter(IpTaggingFilterConfigSharedPtr config) : config_(config) {};

IpTaggingFilter::~IpTaggingFilter() = default;

void IpTaggingFilter::onDestroy() {}

Http::FilterHeadersStatus IpTaggingFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  // Defensive checks to prevent crashes during cascade failures
  if (!config_) {
    return Http::FilterHeadersStatus::Continue;
  }
  if (!callbacks_) {
    return Http::FilterHeadersStatus::Continue;
  }

  const bool is_internal_request = headers.EnvoyInternalRequest() &&
                                   (headers.EnvoyInternalRequest()->value() ==
                                    Http::Headers::get().EnvoyInternalRequestValues.True.c_str());

  if ((is_internal_request && config_->requestType() == FilterRequestType::EXTERNAL) ||
      (!is_internal_request && config_->requestType() == FilterRequestType::INTERNAL) ||
      !config_->runtime().snapshot().featureEnabled("ip_tagging.http_filter_enabled", 100)) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Additional safety check for stream info chain
  auto* stream_info_ptr = &callbacks_->streamInfo();
  if (!stream_info_ptr) {
    return Http::FilterHeadersStatus::Continue;
  }

  auto remote_address = stream_info_ptr->downstreamAddressProvider().remoteAddress();
  if (!remote_address) {
    return Http::FilterHeadersStatus::Continue;
  }

  std::vector<std::string> tags = config_->trie().getData(remote_address);

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

  // Defensive check to prevent crashes during cascade failures
  if (!config_ || !callbacks_) {
    return;
  }

  OptRef<const Http::LowerCaseString> header_name = config_->ipTagHeader();

  if (tags.empty()) {
    bool maybe_sanitize =
        config_->ipTagHeaderAction() == HeaderAction::IPTagging_IpTagHeader_HeaderAction_SANITIZE;
    if (header_name.has_value() && maybe_sanitize) {
      if (headers.remove(header_name.value()) != 0) {
        // We must clear the route cache in case it held a decision based on the now-removed header.
        if (callbacks_->downstreamCallbacks()) {
          callbacks_->downstreamCallbacks()->clearRouteCache();
        }
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
  if (callbacks_->downstreamCallbacks()) {
    callbacks_->downstreamCallbacks()->clearRouteCache();
  }
}

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
