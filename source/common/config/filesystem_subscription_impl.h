#pragma once

#include "envoy/api/api.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/protobuf/message_validator.h"

#include "source/common/common/logger.h"
#include "source/common/config/watched_directory.h"

namespace Envoy {
namespace Config {

envoy::config::core::v3::PathConfigSource makePathConfigSource(const std::string& path);

/**
 * Filesystem inotify implementation of the API Subscription interface. This allows the API to be
 * consumed on filesystem changes to files containing the JSON canonical representation of
 * lists of xDS resources.
 */
class FilesystemSubscriptionImpl : public Config::Subscription,
                                   protected Logger::Loggable<Logger::Id::config> {
public:
  FilesystemSubscriptionImpl(Event::Dispatcher& dispatcher,
                             const envoy::config::core::v3::PathConfigSource& path_config_source,
                             SubscriptionCallbacks& callbacks,
                             OpaqueResourceDecoderSharedPtr resource_decoder,
                             SubscriptionStats stats,
                             ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api);

  // Config::Subscription
  // We report all discovered resources in the watched file, so the resource names arguments are
  // unused, and updateResourceInterest is a no-op (other than updating a stat).
  void start(const absl::flat_hash_set<std::string>&) override;
  void updateResourceInterest(const absl::flat_hash_set<std::string>&) override;
  void requestOnDemandUpdate(const absl::flat_hash_set<std::string>&) override {
    ENVOY_BUG(false, "unexpected request for on demand update");
  }

protected:
  virtual std::string refreshInternal(ProtobufTypes::MessagePtr* config_update);
  void refresh();
  void configRejected(const EnvoyException& e, const std::string& message);

  bool started_{};
  const std::string path_;
  std::unique_ptr<Filesystem::Watcher> file_watcher_;
  WatchedDirectoryPtr directory_watcher_;
  SubscriptionCallbacks& callbacks_;
  OpaqueResourceDecoderSharedPtr resource_decoder_;
  SubscriptionStats stats_;
  Api::Api& api_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
};

// Currently a FilesystemSubscriptionImpl subclass, but this will need to change when we support
// non-inline collection resources.
class FilesystemCollectionSubscriptionImpl : public FilesystemSubscriptionImpl {
public:
  FilesystemCollectionSubscriptionImpl(
      Event::Dispatcher& dispatcher,
      const envoy::config::core::v3::PathConfigSource& path_config_source,
      SubscriptionCallbacks& callbacks, OpaqueResourceDecoderSharedPtr resource_decoder,
      SubscriptionStats stats, ProtobufMessage::ValidationVisitor& validation_visitor,
      Api::Api& api);

  std::string refreshInternal(ProtobufTypes::MessagePtr* config_update) override;
};

} // namespace Config
} // namespace Envoy
