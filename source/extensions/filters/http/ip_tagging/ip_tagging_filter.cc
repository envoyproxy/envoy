#include "source/extensions/filters/http/ip_tagging/ip_tagging_filter.h"

#include "envoy/config/core/v3/address.pb.h"
#include "envoy/extensions/filters/http/ip_tagging/v3/ip_tagging.pb.h"

#include "source/common/config/datasource.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"

#include "absl/strings/str_join.h"

// Add includes for file system operations and threading
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <thread>

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
  ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider constructor starting - file: {}, refresh_interval: {}ms",
            ip_tags_path_, ip_tags_refresh_interval_ms);

  RETURN_ONLY_IF_NOT_OK_REF(creation_status);
  if (ip_tags_datasource.filename().empty()) {
    ENVOY_LOG(error, "[ip_tagging] IpTagsProvider constructor failed - empty file path");
    creation_status = absl::InvalidArgumentError("Cannot load tags from empty file path.");
    return;
  }

  ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider loading initial tags from: {}", ip_tags_path_);
  auto tags_or_error = tags_loader_.loadTags(ip_tags_datasource, main_dispatcher, tls);
  creation_status = tags_or_error.status();
  if (tags_or_error.status().ok()) {
    ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider initial tags loaded successfully");
    tags_ = tags_or_error.value();
  } else {
    ENVOY_LOG(error, "[ip_tagging] IpTagsProvider failed to load initial tags: {}",
              tags_or_error.status().message());
    return;
  }

  ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider creating reload timer");
  ip_tags_reload_timer_ = main_dispatcher.createTimer([this]() -> void {
    ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider timer callback starting");

    try {
      // If data source provider is null (due to startup race condition), try to recreate it
      if (!data_source_provider_) {
        ENVOY_LOG(warn, "[ip_tagging] IpTagsProvider data source provider is null, attempting to recreate");

        // Check if file now exists
        struct stat file_stat;
        if (stat(ip_tags_path_.c_str(), &file_stat) == 0) {
          ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider file now exists: {} (size: {} bytes)",
                    ip_tags_path_, file_stat.st_size);

          // Try to recreate the data source provider
          envoy::config::core::v3::DataSource datasource;
          datasource.set_filename(ip_tags_path_);

          // We need access to the dispatcher and tls from the timer context
          // For now, log the issue and rely on the next timer cycle
          ENVOY_LOG(warn, "[ip_tagging] IpTagsProvider cannot recreate data source provider from timer context, skipping this cycle");
          return;
        } else {
          ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider file still doesn't exist: {} (errno: {})",
                    ip_tags_path_, strerror(errno));
          return;
        }
      }

      ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider getting new data source data");
      const auto new_data = tags_loader_.getDataSourceData();
      ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider got new data, size: {} bytes", new_data.size());

      // If data is empty and this is likely a volume mount issue, don't update the trie
      if (new_data.empty()) {
        ENVOY_LOG(warn, "[ip_tagging] IpTagsProvider got empty data, likely volume mount issue, skipping update");
        return;
      }

      ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider refreshing tags with new data");
      auto new_tags_or_error = tags_loader_.refreshTags(new_data);

      if (new_tags_or_error.status().ok()) {
        ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider tag refresh successful, updating tags");
        updateIpTags(new_tags_or_error.value());

        ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider about to call reload_success_cb_");
        ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider reload_success_cb_ ptr: {}",
                  static_cast<void*>(&reload_success_cb_));

        if (reload_success_cb_) {
          ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider calling reload_success_cb_");
          reload_success_cb_();
          ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider reload_success_cb_ completed successfully");
        } else {
          ENVOY_LOG(error, "[ip_tagging] IpTagsProvider reload_success_cb_ is null!");
        }
      } else {
        ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider tag refresh failed: {}, calling reload_error_cb_",
                  new_tags_or_error.status().message());

        ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider reload_error_cb_ ptr: {}",
                  static_cast<void*>(&reload_error_cb_));

        if (reload_error_cb_) {
          ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider calling reload_error_cb_");
          reload_error_cb_();
          ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider reload_error_cb_ completed successfully");
        } else {
          ENVOY_LOG(error, "[ip_tagging] IpTagsProvider reload_error_cb_ is null!");
        }
      }

    } catch (const std::exception& e) {
      ENVOY_LOG(error, "[ip_tagging] IpTagsProvider timer callback exception: {}", e.what());
      // DO NOT re-throw exceptions from timer callbacks - they cause std::terminate!
      // Instead, call the error callback to notify about the failure
      ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider calling reload_error_cb_ due to exception");
      if (reload_error_cb_) {
        try {
          reload_error_cb_();
          ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider reload_error_cb_ completed after exception");
        } catch (const std::exception& cb_e) {
          ENVOY_LOG(error, "[ip_tagging] IpTagsProvider reload_error_cb_ also threw exception: {}", cb_e.what());
        } catch (...) {
          ENVOY_LOG(error, "[ip_tagging] IpTagsProvider reload_error_cb_ threw unknown exception");
        }
      } else {
        ENVOY_LOG(error, "[ip_tagging] IpTagsProvider reload_error_cb_ is null, cannot notify about exception");
      }
    } catch (...) {
      ENVOY_LOG(error, "[ip_tagging] IpTagsProvider timer callback unknown exception");
      // DO NOT re-throw exceptions from timer callbacks - they cause std::terminate!
      // Instead, call the error callback to notify about the failure
      ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider calling reload_error_cb_ due to unknown exception");
      if (reload_error_cb_) {
        try {
          reload_error_cb_();
          ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider reload_error_cb_ completed after unknown exception");
        } catch (const std::exception& cb_e) {
          ENVOY_LOG(error, "[ip_tagging] IpTagsProvider reload_error_cb_ also threw exception: {}", cb_e.what());
        } catch (...) {
          ENVOY_LOG(error, "[ip_tagging] IpTagsProvider reload_error_cb_ threw unknown exception");
        }
      } else {
        ENVOY_LOG(error, "[ip_tagging] IpTagsProvider reload_error_cb_ is null, cannot notify about unknown exception");
      }
    }

    // Always try to re-enable the timer for the next refresh, even after errors
    try {
      ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider re-enabling timer for next refresh");
      ip_tags_reload_timer_->enableTimer(ip_tags_refresh_interval_ms_);
      ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider timer callback completed");
    } catch (const std::exception& timer_e) {
      ENVOY_LOG(error, "[ip_tagging] IpTagsProvider failed to re-enable timer: {}", timer_e.what());
    } catch (...) {
      ENVOY_LOG(error, "[ip_tagging] IpTagsProvider failed to re-enable timer: unknown exception");
    }
  });

  ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider enabling initial timer");
  ip_tags_reload_timer_->enableTimer(ip_tags_refresh_interval_ms_);
  ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider constructor completed successfully");
}

IpTagsProvider::~IpTagsProvider() {
  ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider destructor starting");
  if (ip_tags_reload_timer_) {
    ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider disabling timer in destructor");
    ip_tags_reload_timer_->disableTimer();
  }
  ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider destructor completed");
}

LcTrieSharedPtr IpTagsProvider::ipTags() ABSL_LOCKS_EXCLUDED(ip_tags_mutex_) {
  ENVOY_LOG(trace, "[ip_tagging] IpTagsProvider::ipTags() acquiring read lock");
  absl::ReaderMutexLock lock(&ip_tags_mutex_);
  ENVOY_LOG(trace, "[ip_tagging] IpTagsProvider::ipTags() returning tags, ptr: {}",
            static_cast<void*>(tags_.get()));
  return tags_;
}

void IpTagsProvider::updateIpTags(LcTrieSharedPtr new_tags) ABSL_LOCKS_EXCLUDED(ip_tags_mutex_) {
  ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider::updateIpTags() acquiring write lock, new_tags ptr: {}",
            static_cast<void*>(new_tags.get()));
  absl::MutexLock lock(&ip_tags_mutex_);
  ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider::updateIpTags() updating tags");
  tags_ = new_tags;
  ENVOY_LOG(debug, "[ip_tagging] IpTagsProvider::updateIpTags() completed");
}

absl::StatusOr<std::shared_ptr<IpTagsProvider>> IpTagsRegistrySingleton::getOrCreateProvider(
    const envoy::config::core::v3::DataSource& ip_tags_datasource, IpTagsLoader& tags_loader,
    uint64_t ip_tags_refresh_interval_ms, IpTagsReloadSuccessCb reload_success_cb,
    IpTagsReloadErrorCb reload_error_cb, Api::Api& api, ThreadLocal::SlotAllocator& tls,
    Event::Dispatcher& main_dispatcher, std::shared_ptr<IpTagsRegistrySingleton> singleton) {
  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTagsRegistrySingleton::getOrCreateProvider() starting for file: {}",
            ip_tags_datasource.filename());

  std::shared_ptr<IpTagsProvider> ip_tags_provider;
  absl::Status creation_status = absl::OkStatus();
  const uint64_t key = std::hash<std::string>()(ip_tags_datasource.filename());

  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTagsRegistrySingleton::getOrCreateProvider() file key: {}", key);

  absl::MutexLock lock(&mu_);
  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTagsRegistrySingleton::getOrCreateProvider() acquired registry lock");

  auto it = ip_tags_registry_.find(key);
  if (it != ip_tags_registry_.end()) {
    ENVOY_LOG_MISC(debug, "[ip_tagging] IpTagsRegistrySingleton::getOrCreateProvider() found existing entry");
    if (std::shared_ptr<IpTagsProvider> provider = it->second.lock()) {
      ENVOY_LOG_MISC(debug, "[ip_tagging] IpTagsRegistrySingleton::getOrCreateProvider() reusing existing provider");
      ip_tags_provider = provider;
    } else {
      ENVOY_LOG_MISC(debug, "[ip_tagging] IpTagsRegistrySingleton::getOrCreateProvider() existing provider expired, creating new one");
      ip_tags_provider = std::make_shared<IpTagsProvider>(
          ip_tags_datasource, tags_loader, ip_tags_refresh_interval_ms, reload_success_cb,
          reload_error_cb, main_dispatcher, api, tls, singleton, creation_status);
      ip_tags_registry_[key] = ip_tags_provider;
    }
  } else {
    ENVOY_LOG_MISC(debug, "[ip_tagging] IpTagsRegistrySingleton::getOrCreateProvider() creating new provider");
    ip_tags_provider = std::make_shared<IpTagsProvider>(
        ip_tags_datasource, tags_loader, ip_tags_refresh_interval_ms, reload_success_cb,
        reload_error_cb, main_dispatcher, api, tls, singleton, creation_status);
    ip_tags_registry_[key] = ip_tags_provider;
  }

  if (!creation_status.ok()) {
    ENVOY_LOG_MISC(error, "[ip_tagging] IpTagsRegistrySingleton::getOrCreateProvider() provider creation failed: {}",
              creation_status.message());
    return creation_status;
  }

  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTagsRegistrySingleton::getOrCreateProvider() completed successfully");
  return ip_tags_provider;
}

const std::string& IpTagsLoader::getDataSourceData() {
  ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::getDataSourceData() starting");
  try {
    if (!data_source_provider_) {
      ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::getDataSourceData() data_source_provider_ is null");
      data_ = "";
      return data_;
    }

    data_ = data_source_provider_->data();
    ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::getDataSourceData() got data, size: {} bytes", data_.size());
    return data_;
  } catch (const std::bad_variant_access& e) {
    ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::getDataSourceData() bad_variant_access: {}", e.what());
    ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::getDataSourceData() data source provider may be in invalid state, returning empty data");
    data_ = "";
    return data_;
  } catch (const std::exception& e) {
    ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::getDataSourceData() exception: {}", e.what());
    // For non-variant access errors, still throw as they might be more serious
    throw;
  } catch (...) {
    ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::getDataSourceData() unknown exception");
    throw;
  }
}

IpTagsLoader::IpTagsLoader(Api::Api& api, ProtobufMessage::ValidationVisitor& validation_visitor,
                           Stats::StatNameSetPtr& stat_name_set)
    : api_(api), validation_visitor_(validation_visitor), stat_name_set_(stat_name_set) {
  ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader constructor completed");
}

absl::StatusOr<LcTrieSharedPtr>
IpTagsLoader::loadTags(const envoy::config::core::v3::DataSource& ip_tags_datasource,
                       Event::Dispatcher& main_dispatcher, ThreadLocal::SlotAllocator& tls) {
  ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::loadTags() starting for file: {}", ip_tags_datasource.filename());

  if (!ip_tags_datasource.filename().empty()) {
    ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::loadTags() checking file extension");
    if (!absl::EndsWith(ip_tags_datasource.filename(), MessageUtil::FileExtensions::get().Yaml) &&
        !absl::EndsWith(ip_tags_datasource.filename(), MessageUtil::FileExtensions::get().Json)) {
      ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::loadTags() unsupported file format: {}",
                ip_tags_datasource.filename());
      return absl::InvalidArgumentError(
          "Unsupported file format, unable to parse ip tags from file " +
          ip_tags_datasource.filename());
    }

    // Add diagnostics for Kubernetes volume mount race conditions
    const std::string& file_path = ip_tags_datasource.filename();
    ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::loadTags() diagnosing file system state for: {}", file_path);

    // Check if the directory exists
    auto dir_pos = file_path.find_last_of('/');
    if (dir_pos != std::string::npos) {
      std::string dir_path = file_path.substr(0, dir_pos);
      struct stat dir_stat;
      if (stat(dir_path.c_str(), &dir_stat) == 0) {
        ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::loadTags() directory exists: {}", dir_path);
        if (S_ISDIR(dir_stat.st_mode)) {
          ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::loadTags() confirmed directory is valid");
        } else {
          ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::loadTags() path exists but is not a directory: {}", dir_path);
        }
      } else {
        ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::loadTags() directory does not exist: {} (errno: {})",
                  dir_path, strerror(errno));
      }
    }

    // Check if the file exists
    struct stat file_stat;
    if (stat(file_path.c_str(), &file_stat) == 0) {
      ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::loadTags() file exists: {} (size: {} bytes)",
                file_path, file_stat.st_size);
    } else {
      ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::loadTags() file does not exist: {} (errno: {})",
                file_path, strerror(errno));
    }

    ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::loadTags() creating data source provider with retry logic");

    // Implement retry logic for volume mount race conditions
    const int max_retries = 5;
    const std::chrono::milliseconds retry_delay(100);
    absl::Status last_error;

    for (int attempt = 1; attempt <= max_retries; ++attempt) {
      ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::loadTags() attempt {} of {} to create data source provider",
                attempt, max_retries);

      auto provider_or_error = Config::DataSource::DataSourceProvider::create(
          ip_tags_datasource, main_dispatcher, tls, api_, false, 0);

      if (provider_or_error.status().ok()) {
        ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::loadTags() data source provider created successfully on attempt {}",
                  attempt);
        data_source_provider_ = std::move(provider_or_error.value());
        ip_tags_path_ = ip_tags_datasource.filename();

        ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::loadTags() getting initial data");
        const auto& new_data = getDataSourceData();

        if (!new_data.empty()) {
          ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::loadTags() refreshing tags with initial data");
          return refreshTags(new_data);
        } else {
          ENVOY_LOG(warn, "[ip_tagging] IpTagsLoader::loadTags() file is empty, creating empty trie");
          // Return empty trie instead of failing - file might be populated later
          std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>> empty_data;
          return std::make_shared<Network::LcTrie::LcTrie<std::string>>(empty_data);
        }
      } else {
        last_error = provider_or_error.status();
        ENVOY_LOG(warn, "[ip_tagging] IpTagsLoader::loadTags() attempt {} failed: {}",
                  attempt, last_error.message());

        if (attempt < max_retries) {
          ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::loadTags() waiting {}ms before retry",
                    retry_delay.count());
          std::this_thread::sleep_for(retry_delay);

          // Re-check file system state
          if (stat(file_path.c_str(), &file_stat) == 0) {
            ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::loadTags() file now exists on attempt {}", attempt);
          } else {
            ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::loadTags() file still missing on attempt {}", attempt);
          }
        }
      }
    }

    ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::loadTags() all {} attempts failed, last error: {}",
              max_retries, last_error.message());

    // For Kubernetes deployments, we'll be more lenient and create an empty trie
    // The file reload mechanism will pick up the file once it's available
    if (ip_tags_datasource.has_watched_directory()) {
      ENVOY_LOG(warn, "[ip_tagging] IpTagsLoader::loadTags() file watching enabled, creating empty trie and relying on reload");
      data_source_provider_ = nullptr; // Will be recreated during reload
      ip_tags_path_ = ip_tags_datasource.filename();
      std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>> empty_data;
      return std::make_shared<Network::LcTrie::LcTrie<std::string>>(empty_data);
    }

    return absl::InvalidArgumentError(
        fmt::format("unable to create data source after {} attempts: '{}'", max_retries, last_error.message()));
  }

  ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::loadTags() empty filename in datasource");
  return absl::InvalidArgumentError("Cannot load tags from empty filename in datasource.");
}

absl::StatusOr<LcTrieSharedPtr> IpTagsLoader::refreshTags(const std::string& new_data) {
  ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::refreshTags() starting with data size: {} bytes", new_data.size());

  if (data_source_provider_) {
    ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::refreshTags() parsing data as proto");
    IpTagFileProto ip_tags_proto;

    try {
      if (absl::EndsWith(ip_tags_path_, MessageUtil::FileExtensions::get().Yaml)) {
        ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::refreshTags() loading YAML data");
        auto load_status =
            MessageUtil::loadFromYamlNoThrow(new_data, ip_tags_proto, validation_visitor_);
        if (!load_status.ok()) {
          ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::refreshTags() YAML loading failed: {}",
                    load_status.message());
          return load_status;
        }
        ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::refreshTags() YAML loaded successfully");
      } else if (absl::EndsWith(ip_tags_path_, MessageUtil::FileExtensions::get().Json)) {
        ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::refreshTags() loading JSON data");
        bool has_unknown_field;
        auto load_status =
            MessageUtil::loadFromJsonNoThrow(new_data, ip_tags_proto, has_unknown_field);
        if (!load_status.ok()) {
          ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::refreshTags() JSON loading failed: {}",
                    load_status.message());
          return load_status;
        }
        ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::refreshTags() JSON loaded successfully");
      }

      ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::refreshTags() parsing proto with {} ip_tags",
                ip_tags_proto.ip_tags().size());
      return parseIpTagsAsProto(ip_tags_proto.ip_tags());

    } catch (const std::exception& e) {
      ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::refreshTags() exception during parsing: {}", e.what());
      return absl::InvalidArgumentError(fmt::format("Failed to parse IP tags: {}", e.what()));
    } catch (...) {
      ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::refreshTags() unknown exception during parsing");
      return absl::InvalidArgumentError("Failed to parse IP tags: unknown exception");
    }
  } else {
    ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::refreshTags() no data source provider available");
    return absl::InvalidArgumentError("Unable to load tags from empty datasource");
  }
}

absl::StatusOr<LcTrieSharedPtr> IpTagsLoader::parseIpTagsAsProto(
    const Protobuf::RepeatedPtrField<
        envoy::extensions::filters::http::ip_tagging::v3::IPTagging::IPTag>& ip_tags) {
  ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::parseIpTagsAsProto() starting with {} tags", ip_tags.size());

  std::vector<std::pair<std::string, std::vector<Network::Address::CidrRange>>> tag_data;
  tag_data.reserve(ip_tags.size());

  try {
    for (const auto& ip_tag : ip_tags) {
      ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::parseIpTagsAsProto() processing tag: {} with {} IPs",
                ip_tag.ip_tag_name(), ip_tag.ip_list().size());

      std::vector<Network::Address::CidrRange> cidr_set;
      cidr_set.reserve(ip_tag.ip_list().size());

      for (const envoy::config::core::v3::CidrRange& entry : ip_tag.ip_list()) {
        ENVOY_LOG(trace, "[ip_tagging] IpTagsLoader::parseIpTagsAsProto() processing CIDR: {}/{}",
                  entry.address_prefix(), entry.prefix_len().value());

        absl::StatusOr<Network::Address::CidrRange> cidr_or_error =
            Network::Address::CidrRange::create(entry);
        if (cidr_or_error.status().ok()) {
          cidr_set.emplace_back(std::move(cidr_or_error.value()));
        } else {
          ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::parseIpTagsAsProto() invalid CIDR: {}/{}, error: {}",
                    entry.address_prefix(), entry.prefix_len().value(), cidr_or_error.status().message());
          return absl::InvalidArgumentError(
              fmt::format("invalid ip/mask combo '{}/{}' (format is <ip>/<# mask bits>)",
                          entry.address_prefix(), entry.prefix_len().value()));
        }
      }

      tag_data.emplace_back(ip_tag.ip_tag_name(), cidr_set);
      stat_name_set_->rememberBuiltin(absl::StrCat(ip_tag.ip_tag_name(), ".hit"));
      ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::parseIpTagsAsProto() registered stat for tag: {}",
                ip_tag.ip_tag_name());
    }

    ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::parseIpTagsAsProto() creating LcTrie with {} tags", tag_data.size());
    auto trie = std::make_shared<Network::LcTrie::LcTrie<std::string>>(tag_data);
    ENVOY_LOG(debug, "[ip_tagging] IpTagsLoader::parseIpTagsAsProto() LcTrie created successfully, ptr: {}",
              static_cast<void*>(trie.get()));
    return trie;

  } catch (const std::exception& e) {
    ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::parseIpTagsAsProto() exception: {}", e.what());
    return absl::InvalidArgumentError(fmt::format("Failed to parse IP tags: {}", e.what()));
  } catch (...) {
    ENVOY_LOG(error, "[ip_tagging] IpTagsLoader::parseIpTagsAsProto() unknown exception");
    return absl::InvalidArgumentError("Failed to parse IP tags: unknown exception");
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

  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig constructor starting");

  // Once loading IP tags from a file system is supported, the restriction on the size
  // of the set should be removed and observability into what tags are loaded needs
  // to be implemented.
  // TODO(ccaraman): Remove size check once file system support is implemented.
  // Work is tracked by issue https://github.com/envoyproxy/envoy/issues/2695.
  if (config.ip_tags().empty() && !config.has_ip_tags_file_provider()) {
    ENVOY_LOG_MISC(error, "[ip_tagging] IpTaggingFilterConfig validation failed - no ip_tags or ip_tags_file_provider");
    creation_status = absl::InvalidArgumentError(
        "HTTP IP Tagging Filter requires either ip_tags or ip_tags_file_provider to be specified.");
  }

  if (!config.ip_tags().empty() && config.has_ip_tags_file_provider()) {
    ENVOY_LOG_MISC(error, "[ip_tagging] IpTaggingFilterConfig validation failed - both ip_tags and ip_tags_file_provider specified");
    creation_status = absl::InvalidArgumentError(
        "Only one of ip_tags or ip_tags_file_provider can be configured.");
  }

  RETURN_ONLY_IF_NOT_OK_REF(creation_status);
  if (!config.ip_tags().empty()) {
    ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig using inline ip_tags");
    auto trie_or_error = tags_loader_.parseIpTagsAsProto(config.ip_tags());
    if (trie_or_error.status().ok()) {
      ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig inline tags parsed successfully");
      trie_ = trie_or_error.value();
    } else {
      ENVOY_LOG_MISC(error, "[ip_tagging] IpTaggingFilterConfig inline tags parsing failed: {}",
                trie_or_error.status().message());
      creation_status = trie_or_error.status();
      return;
    }
  } else {
    ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig using file-based ip_tags");
    if (!config.ip_tags_file_provider().has_ip_tags_datasource()) {
      ENVOY_LOG_MISC(error, "[ip_tagging] IpTaggingFilterConfig file provider missing datasource");
      creation_status = absl::InvalidArgumentError(
          "ip_tags_file_provider requires a valid ip_tags_datasource to be configured.");
      return;
    }

    ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig registering reload stats");
    stat_name_set_->rememberBuiltin("ip_tags_reload_success");
    stat_name_set_->rememberBuiltin("ip_tags_reload_error");

    auto ip_tags_refresh_interval_ms =
        PROTOBUF_GET_MS_OR_DEFAULT(config.ip_tags_file_provider(), ip_tags_refresh_rate, 0);

    ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig creating reload success callback");
    auto reload_success_cb = [scope = std::ref(scope_), stats_prefix = stats_prefix_, stat_name_set = stat_name_set_]() {
      ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig reload_success_cb starting");
      try {
        ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig reload_success_cb incrementing success counter");
        // Safely increment counter without relying on 'this' pointer
        auto success_stat = stat_name_set->getBuiltin("ip_tags_reload_success", stat_name_set->add("unknown_tag.hit"));
        Stats::SymbolTable::StoragePtr storage = scope.get().symbolTable().join({stats_prefix, success_stat});
        scope.get().counterFromStatName(Stats::StatName(storage.get())).inc();
        ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig reload_success_cb completed");
      } catch (const std::exception& e) {
        ENVOY_LOG_MISC(error, "[ip_tagging] IpTaggingFilterConfig reload_success_cb exception: {}", e.what());
        // Don't re-throw from callbacks that may be called from timer context
      } catch (...) {
        ENVOY_LOG_MISC(error, "[ip_tagging] IpTaggingFilterConfig reload_success_cb unknown exception");
        // Don't re-throw from callbacks that may be called from timer context
      }
    };

    ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig creating reload error callback");
    auto reload_error_cb = [scope = std::ref(scope_), stats_prefix = stats_prefix_, stat_name_set = stat_name_set_]() {
      ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig reload_error_cb starting");
      try {
        ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig reload_error_cb incrementing error counter");
        // Safely increment counter without relying on 'this' pointer
        auto error_stat = stat_name_set->getBuiltin("ip_tags_reload_error", stat_name_set->add("unknown_tag.hit"));
        Stats::SymbolTable::StoragePtr storage = scope.get().symbolTable().join({stats_prefix, error_stat});
        scope.get().counterFromStatName(Stats::StatName(storage.get())).inc();
        ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig reload_error_cb completed");
      } catch (const std::exception& e) {
        ENVOY_LOG_MISC(error, "[ip_tagging] IpTaggingFilterConfig reload_error_cb exception: {}", e.what());
        // Don't re-throw from callbacks that may be called from timer context
      } catch (...) {
        ENVOY_LOG_MISC(error, "[ip_tagging] IpTaggingFilterConfig reload_error_cb unknown exception");
        // Don't re-throw from callbacks that may be called from timer context
      }
    };

    ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig getting or creating provider");
    auto provider_or_error = ip_tags_registry_->getOrCreateProvider(
        config.ip_tags_file_provider().ip_tags_datasource(), tags_loader_,
        ip_tags_refresh_interval_ms, reload_success_cb, reload_error_cb, api, tls, dispatcher, ip_tags_registry_);

    if (provider_or_error.status().ok()) {
      ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig provider created successfully");
      provider_ = provider_or_error.value();
    } else {
      ENVOY_LOG_MISC(error, "[ip_tagging] IpTaggingFilterConfig provider creation failed: {}",
                provider_or_error.status().message());
      creation_status = provider_or_error.status();
      return;
    }

    ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig getting initial tags from provider");
    if (provider_ && provider_->ipTags()) {
      ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig got initial tags from provider");
      trie_ = provider_->ipTags();
    } else {
      ENVOY_LOG_MISC(error, "[ip_tagging] IpTaggingFilterConfig failed to get ip tags from provider");
      creation_status = absl::InvalidArgumentError("Failed to get ip tags from provider");
    }
  }

  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilterConfig constructor completed successfully");
}

void IpTaggingFilterConfig::incCounter(Stats::StatName name) {
  ENVOY_LOG_MISC(trace, "[ip_tagging] IpTaggingFilterConfig::incCounter() called");
  try {
    Stats::SymbolTable::StoragePtr storage = scope_.symbolTable().join({stats_prefix_, name});
    scope_.counterFromStatName(Stats::StatName(storage.get())).inc();
    ENVOY_LOG_MISC(trace, "[ip_tagging] IpTaggingFilterConfig::incCounter() completed");
  } catch (const std::exception& e) {
    ENVOY_LOG_MISC(error, "[ip_tagging] IpTaggingFilterConfig::incCounter() exception: {}", e.what());
    throw;
  } catch (...) {
    ENVOY_LOG_MISC(error, "[ip_tagging] IpTaggingFilterConfig::incCounter() unknown exception");
    throw;
  }
}

IpTaggingFilter::IpTaggingFilter(IpTaggingFilterConfigSharedPtr config) : config_(config) {
  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter constructor completed, config ptr: {}",
            static_cast<void*>(config_.get()));
}

IpTaggingFilter::~IpTaggingFilter() {
  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter destructor starting");
  // Destructor is default, but adding logging for completeness
  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter destructor completed");
}

void IpTaggingFilter::onDestroy() {
  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::onDestroy() called");
}

Http::FilterHeadersStatus IpTaggingFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::decodeHeaders() starting");

  const bool is_internal_request = headers.EnvoyInternalRequest() &&
                                   (headers.EnvoyInternalRequest()->value() ==
                                    Http::Headers::get().EnvoyInternalRequestValues.True.c_str());

  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::decodeHeaders() is_internal_request: {}", is_internal_request);

  if ((is_internal_request && config_->requestType() == FilterRequestType::EXTERNAL) ||
      (!is_internal_request && config_->requestType() == FilterRequestType::INTERNAL) ||
      !config_->runtime().snapshot().featureEnabled("ip_tagging.http_filter_enabled", 100)) {
    ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::decodeHeaders() skipping due to request type or feature flag");
    return Http::FilterHeadersStatus::Continue;
  }

  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::decodeHeaders() getting remote address");
  const auto& remote_address = callbacks_->streamInfo().downstreamAddressProvider().remoteAddress();
  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::decodeHeaders() remote address: {}", remote_address->asString());

  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::decodeHeaders() looking up tags in trie");
  std::vector<std::string> tags = config_->trie().getData(remote_address);
  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::decodeHeaders() found {} tags", tags.size());

  // Used for testing.
  synchronizer_.syncPoint("_trie_lookup_complete");

  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::decodeHeaders() applying tags to headers");
  applyTags(headers, tags);

  if (!tags.empty()) {
    ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::decodeHeaders() incrementing hit counters for tags");
    // For a large number(ex > 1000) of tags, stats cardinality will be an issue.
    // If there are use cases with a large set of tags, a way to opt into these stats
    // should be exposed and other observability options like logging tags need to be implemented.
    for (const std::string& tag : tags) {
      ENVOY_LOG_MISC(trace, "[ip_tagging] IpTaggingFilter::decodeHeaders() incrementing hit for tag: {}", tag);
      config_->incHit(tag);
    }
  } else {
    ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::decodeHeaders() no tags found, incrementing no_hit");
    config_->incNoHit();
  }

  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::decodeHeaders() incrementing total counter");
  config_->incTotal();

  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::decodeHeaders() completed successfully");
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus IpTaggingFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus IpTaggingFilter::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void IpTaggingFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::setDecoderFilterCallbacks() called, callbacks ptr: {}",
            static_cast<void*>(&callbacks));
  callbacks_ = &callbacks;
}

void IpTaggingFilter::applyTags(Http::RequestHeaderMap& headers,
                                const std::vector<std::string>& tags) {
  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::applyTags() starting with {} tags", tags.size());

  using HeaderAction = IpTaggingFilterConfig::HeaderAction;

  OptRef<const Http::LowerCaseString> header_name = config_->ipTagHeader();

  if (tags.empty()) {
    ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::applyTags() no tags to apply");
    bool maybe_sanitize =
        config_->ipTagHeaderAction() == HeaderAction::IPTagging_IpTagHeader_HeaderAction_SANITIZE;
    if (header_name.has_value() && maybe_sanitize) {
      ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::applyTags() sanitizing header: {}", header_name.value().get());
      if (headers.remove(header_name.value()) != 0) {
        ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::applyTags() header removed, clearing route cache");
        // We must clear the route cache in case it held a decision based on the now-removed header.
        callbacks_->downstreamCallbacks()->clearRouteCache();
      }
    }
    return;
  }

  const std::string tags_join = absl::StrJoin(tags, ",");
  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::applyTags() joined tags: {}", tags_join);

  if (!header_name.has_value()) {
    ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::applyTags() appending to x-envoy-ip-tags header");
    // The x-envoy-ip-tags header was cleared at the start of the filter chain.
    // We only do append here, so that if multiple ip-tagging filters are run sequentially,
    // the behaviour will be backwards compatible.
    headers.appendEnvoyIpTags(tags_join, ",");
  } else {
    ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::applyTags() applying to custom header: {}", header_name.value().get());
    switch (config_->ipTagHeaderAction()) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case HeaderAction::IPTagging_IpTagHeader_HeaderAction_SANITIZE:
      ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::applyTags() sanitizing and setting header");
      headers.setCopy(header_name.value(), tags_join);
      break;
    case HeaderAction::IPTagging_IpTagHeader_HeaderAction_APPEND_IF_EXISTS_OR_ADD:
      ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::applyTags() appending to existing header");
      headers.appendCopy(header_name.value(), tags_join);
      break;
    }
  }

  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::applyTags() clearing route cache");
  // We must clear the route cache so it can match on the updated value of the header.
  callbacks_->downstreamCallbacks()->clearRouteCache();

  ENVOY_LOG_MISC(debug, "[ip_tagging] IpTaggingFilter::applyTags() completed successfully");
}

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
