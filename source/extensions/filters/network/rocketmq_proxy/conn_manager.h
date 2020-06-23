#pragma once

#include <list>

#include "envoy/common/time.h"
#include "envoy/extensions/filters/network/rocketmq_proxy/v3/rocketmq_proxy.pb.h"
#include "envoy/extensions/filters/network/rocketmq_proxy/v3/rocketmq_proxy.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stats/timespan.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

#include "extensions/filters/network/rocketmq_proxy/active_message.h"
#include "extensions/filters/network/rocketmq_proxy/codec.h"
#include "extensions/filters/network/rocketmq_proxy/stats.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

class Config {
public:
  virtual ~Config() = default;

  virtual RocketmqFilterStats& stats() PURE;

  virtual Upstream::ClusterManager& clusterManager() PURE;

  virtual Router::RouterPtr createRouter() PURE;

  /**
   * Indicate whether this proxy is running in develop mode. Once set true, this proxy plugin may
   * work without dedicated traffic intercepting facility without considering backward
   * compatibility.
   * @return true when in development mode; false otherwise.
   */
  virtual bool developMode() const PURE;

  virtual std::string proxyAddress() PURE;

  virtual Router::Config& routerConfig() PURE;

  virtual std::chrono::milliseconds transientObjectLifeSpan() const PURE;
};

class ConnectionManager;

/**
 * This class is to ensure legacy RocketMQ SDK works. Heartbeat between client SDK and envoy is not
 * necessary any more and should be removed once the lite SDK is in-place.
 */
class ConsumerGroupMember {
public:
  ConsumerGroupMember(absl::string_view client_id, ConnectionManager& conn_manager);

  bool operator==(const ConsumerGroupMember& other) const { return client_id_ == other.client_id_; }

  bool operator<(const ConsumerGroupMember& other) const { return client_id_ < other.client_id_; }

  void refresh();

  bool expired() const;

  absl::string_view clientId() const { return client_id_; }

  void setLastForTest(MonotonicTime tp) { last_ = tp; }

private:
  std::string client_id_;
  ConnectionManager* connection_manager_;
  MonotonicTime last_;
};

class ConnectionManager : public Network::ReadFilter, Logger::Loggable<Logger::Id::filter> {
public:
  ConnectionManager(Config& config, TimeSource& time_source);

  ~ConnectionManager() override = default;

  /**
   * Called when data is read on the connection.
   * @param data supplies the read data which may be modified.
   * @param end_stream supplies whether this is the last byte on the connection. This will only
   *        be set if the connection has half-close semantics enabled.
   * @return status used by the filter manager to manage further filter iteration.
   */
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;

  /**
   * Called when a connection is first established. Filters should do one time long term processing
   * that needs to be done when a connection is established. Filter chain iteration can be stopped
   * if needed.
   * @return status used by the filter manager to manage further filter iteration.
   */
  Network::FilterStatus onNewConnection() override;

  /**
   * Initializes the read filter callbacks used to interact with the filter manager. It will be
   * called by the filter manager a single time when the filter is first registered. Thus, any
   * construction that requires the backing connection should take place in the context of this
   * function.
   *
   * IMPORTANT: No outbound networking or complex processing should be done in this function.
   *            That should be done in the context of onNewConnection() if needed.
   *
   * @param callbacks supplies the callbacks.
   */
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks&) override;

  /**
   * Send response to downstream either when envoy proxy has received result from upstream hosts or
   * the proxy itself may serve the request.
   * @param response Response to write to downstream with identical opaque number.
   */
  void sendResponseToDownstream(RemotingCommandPtr& response);

  void onGetTopicRoute(RemotingCommandPtr request);

  /**
   * Called when downstream sends heartbeat requests.
   * @param request heartbeat request from downstream
   */
  void onHeartbeat(RemotingCommandPtr request);

  void addOrUpdateGroupMember(absl::string_view group, absl::string_view client_id);

  void onUnregisterClient(RemotingCommandPtr request);

  void onError(RemotingCommandPtr& request, absl::string_view error_msg);

  void onSendMessage(RemotingCommandPtr request);

  void onGetConsumerListByGroup(RemotingCommandPtr request);

  void onPopMessage(RemotingCommandPtr request);

  void onAckMessage(RemotingCommandPtr request);

  ActiveMessage& createActiveMessage(RemotingCommandPtr& request);

  void deferredDelete(ActiveMessage& active_message);

  void resetAllActiveMessages(absl::string_view error_msg);

  Config& config() { return config_; }

  RocketmqFilterStats& stats() { return stats_; }

  absl::flat_hash_map<std::string, std::vector<ConsumerGroupMember>>& groupMembersForTest() {
    return group_members_;
  }

  std::list<ActiveMessagePtr>& activeMessageList() { return active_message_list_; }

  void insertAckDirective(const std::string& key, const AckMessageDirective& directive) {
    ack_directive_table_.insert(std::make_pair(key, directive));
  }

  void eraseAckDirective(const std::string& key) {
    auto it = ack_directive_table_.find(key);
    if (it != ack_directive_table_.end()) {
      ack_directive_table_.erase(it);
    }
  }

  TimeSource& timeSource() const { return time_source_; }

  const absl::flat_hash_map<std::string, AckMessageDirective>& getAckDirectiveTableForTest() const {
    return ack_directive_table_;
  }

  friend class ConsumerGroupMember;

private:
  /**
   * Dispatch incoming requests from downstream to run through filter chains.
   */
  void dispatch();

  /**
   * Invoked by heartbeat to purge deprecated ack_directive entries.
   */
  void purgeDirectiveTable();

  Network::ReadFilterCallbacks* read_callbacks_{};
  Buffer::OwnedImpl request_buffer_;

  Config& config_;
  TimeSource& time_source_;
  RocketmqFilterStats& stats_;

  std::list<ActiveMessagePtr> active_message_list_;

  absl::flat_hash_map<std::string, std::vector<ConsumerGroupMember>> group_members_;

  /**
   * Message unique key to message acknowledge directive mapping.
   * Acknowledge requests first consult this table to determine which host in the cluster to go.
   */
  absl::flat_hash_map<std::string, AckMessageDirective> ack_directive_table_;
};
} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy