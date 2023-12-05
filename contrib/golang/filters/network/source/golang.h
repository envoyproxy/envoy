#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/ssl/connection.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/network/connection_impl.h"
#include "source/common/upstream/load_balancer_impl.h"
#include "source/extensions/filters/network/common/factory_base.h"

#include "contrib/envoy/extensions/filters/network/golang/v3alpha/golang.pb.h"
#include "contrib/golang/common/dso/dso.h"
#include "contrib/golang/filters/network/source/upstream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Golang {

/**
 * Configuration for the Golang network filter.
 */
class FilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::network::golang::v3alpha::Config& proto_config);

  const std::string& libraryID() const { return library_id_; }
  const std::string& libraryPath() const { return library_path_; }
  const std::string& pluginName() const { return plugin_name_; }
  const ProtobufWkt::Any& pluginConfig() const { return plugin_config_; }

private:
  const std::string library_id_;
  const std::string library_path_;
  const std::string plugin_name_;
  const ProtobufWkt::Any plugin_config_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

struct FilterWrapper;

/**
 * Implementation of a basic golang filter.
 */
class Filter : public Network::Filter,
               public Network::ConnectionCallbacks,
               public std::enable_shared_from_this<Filter>,
               Logger::Loggable<Logger::Id::golang> {
public:
  explicit Filter(Server::Configuration::FactoryContext& context, FilterConfigSharedPtr config,
                  uint64_t config_id, Dso::NetworkFilterDsoPtr dynamic_lib)
      : context_(context), config_(config),
        config_id_(config_id), plugin_name_{config->pluginName()}, dynamic_lib_(dynamic_lib) {}

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  void write(Buffer::Instance& buf, bool end_stream);
  void close(Network::ConnectionCloseType close_type);

  Event::Dispatcher* dispatcher() { return dispatcher_; }
  Upstream::ClusterManager& clusterManager() {
    return context_.serverFactoryContext().clusterManager();
  }

  std::string getLocalAddrStr() const { return local_addr_; }
  std::string getRemoteAddrStr() const { return addr_; };

  CAPIStatus setFilterState(absl::string_view key, absl::string_view value, int state_type,
                            int life_span, int stream_sharing);
  CAPIStatus getFilterState(absl::string_view key, GoString* value_str);

private:
  Server::Configuration::FactoryContext& context_;
  const FilterConfigSharedPtr config_;
  const uint64_t config_id_;
  std::string plugin_name_{};
  Dso::NetworkFilterDsoPtr dynamic_lib_{nullptr};
  FilterWrapper* wrapper_{nullptr};
  Event::Dispatcher* dispatcher_{nullptr};

  bool closed_{false};
  Network::ReadFilterCallbacks* read_callbacks_{};
  std::string local_addr_{};
  std::string addr_{};

  Thread::MutexBasicLockable mutex_{};
};

using FilterSharedPtr = std::shared_ptr<Filter>;
using FilterWeakPtr = std::weak_ptr<Filter>;

struct FilterWrapper {
public:
  FilterWrapper(FilterWeakPtr ptr) : filter_ptr_(ptr) {}
  ~FilterWrapper() = default;

  FilterWeakPtr filter_ptr_{};
  // anchor a string temporarily, make sure it won't be freed before copied to Go.
  std::string str_value_;
};

class GoStringFilterState : public StreamInfo::FilterState::Object {
public:
  GoStringFilterState(absl::string_view value) : value_(value) {}
  const std::string& value() const { return value_; }

private:
  const std::string value_;
};

} // namespace Golang
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
