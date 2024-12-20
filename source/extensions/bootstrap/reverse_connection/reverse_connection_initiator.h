#pragma once

#include <memory>

#include "envoy/extensions/filters/http/reverse_conn/v3/reverse_conn.pb.h"

#include "source/common/event/dispatcher_impl.h"
#include "source/common/http/headers.h"
#include "source/common/network/filter_impl.h"
#include "source/common/upstream/load_balancer_context_base.h"
#include "source/extensions/bootstrap/reverse_connection/reverse_connection_manager_impl.h"

namespace Envoy {

namespace Http {
namespace Utility {
using QueryParams = std::map<std::string, std::string>;
std::string queryParamsToString(const QueryParams& query_params);
} // namespace Utility
} // namespace Http

namespace Event {
class DispatcherImpl;
} // namespace Event

namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ReverseConnectionManagerImpl;
static const char CRLF[] = "\r\n";
static const char DOUBLE_CRLF[] = "\r\n\r\n";

/**
 * All ReverseConnectionInitator stats. @see stats_macros.h
 * This encompasses the stats for all initiated reverse connections by the initiator envoy.
 */
#define ALL_RCINITIATOR_STATS(GAUGE)                                                               \
  GAUGE(reverse_conn_cx_idle, NeverImport)                                                         \
  GAUGE(reverse_conn_cx_used, NeverImport)                                                         \
  GAUGE(reverse_conn_cx_total, NeverImport)

/**
 * Struct definition for all ReverseConnectionManager stats. @see stats_macros.h
 */
struct RCInitiatorStats {
  ALL_RCINITIATOR_STATS(GENERATE_GAUGE_STRUCT)
};

using RCInitiatorStatsPtr = std::unique_ptr<RCInitiatorStats>;

/**
 * The ReverseConnectionInitiator initiates and saves reverse connections to a set of remote
 * clusters based on listener metadata. A unique ReverseConnectionInitiator is created for each
 * unique listener.
 */
class ReverseConnectionInitiator : Logger::Loggable<Logger::Id::main> {
public:
  // Parameters used by the current initiator during their cycle.
  struct ReverseConnectionOptions {
    // The node ID, cluster ID and tenant ID of the originating cluster.
    std::string src_node_id_;
    std::string src_cluster_id_;
    std::string src_tenant_id_;
    // A map of remote cluster names to the number of reverse connections requested for each
    // cluster.
    absl::flat_hash_map<std::string, uint32_t> remote_cluster_to_conns_;
  };

  ReverseConnectionInitiator(const Network::ListenerConfig& listener_ref,
                             const ReverseConnectionOptions& options,
                             ReverseConnectionManagerImpl& rc_manager, Stats::Scope& scope);

  ~ReverseConnectionInitiator();

  // Constants used while initiating/accepting reverse connections.
  static const std::string reverse_connections_path;
  static const std::string reverse_connections_request_path;

  /**
   * Removes stale host from host_to_rc_conn_ and host_to_cluster_ maps and
   * unregisters all connections to those hosts.
   * @param host stale host to be removed.
   */
  void removeStaleHostAndCloseConnections(const std::string& host);

  /**
   * Updates the set of resolved hosts for a cluster in cluster_to_resolved_host_.
   * This also initiates termination of stale connections.
   * @param cluster_id remote cluster to which reverse conns are initiated.
   * @param hosts list of resolved hosts for a cluster.
   */
  void maybeUpdateHostsMappingsAndConnections(const std::string& cluster_id,
                                              const std::vector<std::string>& hosts);

  /**
   * The main function of the RC Initiator that iterates through remote_cluster_to_conns_
   * to get the upstream clusters, uses those upstream clusters to derive there
   * corresponding hosts and initiates the required number of reverse connections
   * to these hosts.
   */
  bool maintainConnCount();

  /**
   * Initiate one reverse connection to remote_cluster_id. Set to public as it will be called by
   * the listener on socket closure.
   * @param remote_cluster_id remote cluster to which reverse conns are initiated.
   * @param host remote host to which reverse conns are initiated.
   * @return true if the connection is successfully created, false otherwise.
   */
  bool initiateOneReverseConnection(const std::string& remote_cluster_id, const std::string& host);

  /**
   * Retrieves the upstream cluster name using a given connection key.
   * @param connectionKey The connection.
   * @return the remote cluster ID for the connection, "" if no mapping is present.
   */
  std::string getRemoteClusterForConn(const std::string& connectionKey);

  void notifyConnectionClose(const std::string& connectionKey, const bool is_used);

  void initializeStats(Stats::Scope& scope);

  void addStatshandlerForCluster(const std::string& cluster_name);

  void markConnUsed(const std::string& connectionKey);

  uint64_t getNumberOfSockets(const std::string& key);
  void getSocketCountMap(absl::flat_hash_map<std::string, size_t>& response);

  // Returns the parent listener tag for this RCInitiator.
  uint64_t getID() { return listener_ref_.listenerTag(); }

private:
  // The listener that triggers initiation of reverse connections.
  const Network::ListenerConfig& listener_ref_;

  ReverseConnectionOptions rc_options_;

  // The parent ReverseConnectionManager that manages the lifecycle of this
  // ReverseConnectionInitiator.
  ReverseConnectionManagerImpl& parent_rc_manager_;

  // Mapping of connection keys to their respective remote hosts.
  absl::flat_hash_map<std::string, std::string> rc_conn_to_host_map_;

  // Mapping of clusters to the set of resolved hosts belonging to each cluster.
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>> cluster_to_resolved_hosts_map_;

  // Mapping of remote hosts to the set of successful connections established with each host.
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>> host_to_rc_conns_map_;

  // Mapping of resolved hosts to their corresponding cluster.
  absl::flat_hash_map<std::string, std::string> host_to_cluster_map_;

  // A map of the remote cluster ID -> RCInitiatorStatsPtr, used to log initiated
  // reverse conn stats for every remote cluster, by the local envoy as initiator.
  absl::flat_hash_map<std::string, RCInitiatorStatsPtr> rc_stats_map_;

  // The scope for RCInitiatorStats stats.
  Stats::ScopeSharedPtr reverse_conn_scope_;

  // Periodically executes maintainConnCount function.
  Event::TimerPtr rev_conn_retry_timer_;

  /**
   * RCConnectionManager manages the lifecycle of a ClientConnectionPtr and ensures
   * the integrity of RCInitiator's mappings by removing invalid entries. It utilizes
   * ReverseConnectionManagerImpl to access the necessary RCInitiator object. It uses
   * ReverseConnectionManagerImpl because a new LDS update could delete an existing
   * RCInitiator before its registered RCConnectionManager completes its post-callbacks.
   *
   * RCConnectionManager establishes callbacks on the ClientConnectionPtr to respond to
   * state changes and execute corresponding operations.
   *
   * Note: The RCConnectionManager instance handles cleanup within its callbacks.
   */
  class RCConnectionManager : public Network::ConnectionCallbacks {
  public:
    RCConnectionManager(ReverseConnectionManagerImpl& parent_rc_manager,
                        const Network::ListenerConfig& listener_ref,
                        Network::ClientConnectionPtr connection)
        : parent_rc_manager_(parent_rc_manager), listener_ref_(listener_ref),
          connection_(std::move(connection)) {}

    /**
     * Handles connection state change events.
     * @param event The connection event triggered.
     */
    void onEvent(Network::ConnectionEvent event) override;

    /**
     * RCConnectionManager does not implement the onAboveWriteBufferHighWatermark method.
     */
    void onAboveWriteBufferHighWatermark() override {}

    /**
     * RCConnectionManager does not implement the onBelowWriteBufferLowWatermark method.
     */
    void onBelowWriteBufferLowWatermark() override {}

    /**
     * Adds connection callbacks, read filters, and initializes routines for a new connection.
     * @param rc_options Options for the reverse connection.
     */
    std::string connect(const ReverseConnectionOptions& rc_options);

    /**
     * Processes the response and calls the reverseConnectionDone() method of its parent
     * RCInitiator.
     * @param error Error message, if any.
     */
    void onData(const std::string& error);

    /**
     * Removes the connection callbacks.
     */
    void onFailure() {
      ENVOY_LOG(debug, "RCConnectionManager: connection: {}, removing connection callbacks",
                connection_->id());
      connection_->removeConnectionCallbacks(*this);
    }

    Network::ClientConnection* getConnection() { return connection_.get(); }

  private:
    // The parent ReverseConnectionManagerImpl.
    ReverseConnectionManagerImpl& parent_rc_manager_;

    // The listener that triggers initiation of reverse connections.
    const Network::ListenerConfig& listener_ref_;

    // Client Connection.
    Network::ClientConnectionPtr connection_;

    /**
     * Read filter that is added to each connection initiated by the RCInitiator. Upon receiving a
     * response from remote envoy, the Read filter parses it and calls its parent
     * RCConnectionManager onData().
     */
    struct ConnReadFilter : public Network::ReadFilterBaseImpl {

      /**
       * expected response will be something like:
       * 'HTTP/1.1 200 OK\r\ncontent-length: 27\r\ncontent-type: text/plain\r\ndate: Tue, 11 Feb
       * 2020 07:37:24 GMT\r\nserver: envoy\r\n\r\nreverse connection accepted'
       */
      ConnReadFilter(RCConnectionManager* parent) : parent_(parent) {}

      // Implementation of Network::ReadFilter.
      Network::FilterStatus onData(Buffer::Instance& buffer, bool) {
        if (parent_ == nullptr) {
          ENVOY_LOG(error, "RC Connection Manager is null. Aborting read.");
          return Network::FilterStatus::StopIteration;
        }

        Network::ClientConnection* connection = parent_->getConnection();

        if (connection != nullptr) {
          ENVOY_LOG(info, "Connection read filter: reading data on connection ID: {}",
                    connection->id());
        } else {
          ENVOY_LOG(error, "Connection read filter: connection is null. Aborting read.");
          return Network::FilterStatus::StopIteration;
        }

        response_buffer_string_ += buffer.toString();
        const size_t headers_end_index = response_buffer_string_.find(DOUBLE_CRLF);
        if (headers_end_index == std::string::npos) {
          ENVOY_LOG(debug, "Received {} bytes, but not all the headers.",
                    response_buffer_string_.length());
          return Network::FilterStatus::Continue;
        }

        const std::vector<absl::string_view>& headers =
            StringUtil::splitToken(response_buffer_string_.substr(0, headers_end_index), CRLF,
                                   false /* keep_empty_string */, true /* trim_whitespace */);
        const absl::string_view content_length_str = Http::Headers::get().ContentLength.get();
        absl::string_view length_header;
        for (const absl::string_view& header : headers) {
          if (!StringUtil::CaseInsensitiveCompare()(header.substr(0, content_length_str.length()),
                                                    content_length_str)) {
            continue;
          }
          length_header = header;
        }

        // Since the Ikat hub is expected to send a simple HTTP response which is not chunk
        // encoded, we should always find a content length header.
        RELEASE_ASSERT(length_header.length() > 0, "Could not find a valid Content-length header");

        // Decode response content length from a Header value to an unsigned integer.
        const std::vector<absl::string_view>& header_val =
            StringUtil::splitToken(length_header, ":", false, true);
        uint32_t body_size = std::stoi(std::string(header_val[1]));
        ENVOY_LOG(debug, "Decoding a Response of length {}", body_size);

        const size_t expected_response_size = headers_end_index + strlen(DOUBLE_CRLF) + body_size;
        if (response_buffer_string_.length() < expected_response_size) {
          // We have not received the complete body yet.
          ENVOY_LOG(trace, "Received {} of {} expected response bytes.",
                    response_buffer_string_.length(), expected_response_size);
          return Network::FilterStatus::Continue;
        }

        envoy::extensions::filters::http::reverse_conn::v3::ReverseConnHandshakeRet ret;
        ret.ParseFromString(
            response_buffer_string_.substr(headers_end_index + strlen(DOUBLE_CRLF)));
        ENVOY_LOG(debug, "Found ReverseConnHandshakeRet {}", ret.DebugString());
        parent_->onData(ret.status_message());
        return Network::FilterStatus::StopIteration;
      }

      // The parent RCConnectionManager.
      RCConnectionManager* parent_;
      std::string response_buffer_string_;
    };
  };

  using RCConnectionManagerPtr = std::unique_ptr<RCConnectionManager>;

  /**
   * Internal function that is called by the read filter when a message is received from the remote
   * envoy. It saves the connection socket, and passes it to the listener associated with its parent
   * RCinitiator.
   */
  void reverseConnectionDone(const std::string& error, RCConnectionManagerPtr rc_connection_manager,
                             bool connectionClosed);
};

/**
 * Custom load balancer context for reverse connections. This class enables the
 * rc_initiator to propagate upstream host details to the cluster_manager, ensuring
 * that connections are initiated to specified hosts rather than random ones. It inherits
 * from the LoadBalancerContextBase class and overrides the `overrideHostToSelect` method.
 */
class ReverseConnectionLoadBalancerContext : public Upstream::LoadBalancerContextBase {
public:
  ReverseConnectionLoadBalancerContext(const std::string& host_to_select) {
    host_to_select_ = std::make_pair(host_to_select, false);
  }

  absl::optional<OverrideHost> overrideHostToSelect() const override {
    return absl::make_optional(host_to_select_);
  }

private:
  OverrideHost host_to_select_;
};
} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
