#pragma once

#include "envoy/event/deferred_deletable.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/timespan.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"

#include "absl/types/optional.h"
#include "contrib/rocketmq_proxy/filters/network/source/codec.h"
#include "contrib/rocketmq_proxy/filters/network/source/protocol.h"
#include "contrib/rocketmq_proxy/filters/network/source/router/router.h"
#include "contrib/rocketmq_proxy/filters/network/source/topic_route.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

class ConnectionManager;

/**
 * ActiveMessage represents an in-flight request from downstream that has not yet received response
 * from upstream.
 */
class ActiveMessage : public LinkedObject<ActiveMessage>,
                      public Event::DeferredDeletable,
                      Logger::Loggable<Logger::Id::rocketmq> {
public:
  ActiveMessage(ConnectionManager& conn_manager, RemotingCommandPtr&& request);

  ~ActiveMessage() override;

  /**
   * Set up filter-chain according to configuration from bootstrap config file and dynamic
   * configuration items from Pilot.
   */
  void createFilterChain();

  /**
   * Relay requests from downstream to upstream cluster. If the target cluster is absent at the
   * moment, it triggers cluster discovery service request and mark awaitCluster as true.
   * ClusterUpdateCallback will process requests marked await-cluster once the target cluster is
   * in place.
   */
  void sendRequestToUpstream();

  const RemotingCommandPtr& downstreamRequest() const;

  /**
   * Parse pop response and insert ack route directive such that ack requests will be forwarded to
   * the same broker host from which messages are popped.
   * @param buffer Pop response body.
   * @param group Consumer group name.
   * @param topic Topic from which messages are popped
   * @param directive ack route directive
   */
  virtual void fillAckMessageDirective(Buffer::Instance& buffer, const std::string& group,
                                       const std::string& topic,
                                       const AckMessageDirective& directive);

  virtual void sendResponseToDownstream();

  void onQueryTopicRoute();

  virtual void onError(absl::string_view error_message);

  ConnectionManager& connectionManager() { return connection_manager_; }

  virtual void onReset();

  bool onUpstreamData(Buffer::Instance& data, bool end_stream,
                      Tcp::ConnectionPool::ConnectionDataPtr& conn_data);

  virtual MessageMetadataSharedPtr metadata() const { return metadata_; }

  virtual Router::RouteConstSharedPtr route();

  void recordPopRouteInfo(Upstream::HostDescriptionConstSharedPtr host_description);

  static void fillBrokerData(std::vector<BrokerData>& list, const std::string& cluster,
                             const std::string& broker_name, int64_t broker_id,
                             const std::string& address);

private:
  ConnectionManager& connection_manager_;
  RemotingCommandPtr request_;
  RemotingCommandPtr response_;
  MessageMetadataSharedPtr metadata_;
  Router::RouterPtr router_;
  absl::optional<Router::RouteConstSharedPtr> cached_route_;

  void updateActiveRequestStats(bool is_inc = true);
};

using ActiveMessagePtr = std::unique_ptr<ActiveMessage>;

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
