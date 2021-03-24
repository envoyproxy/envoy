#include "envoy/event/dispatcher.h"
#include "envoy/router/router.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/cluster_manager.h"
#include "extensions/filters/network/mysql_proxy/conn_pool_manager.h"
#include <memory>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {
namespace ConnPool {

class ConnectionManagerImpl : public ConnectionManager, public Logger::Loggable<Logger::Id::pool> {
public:
  ConnectionPool::Cancellable*
  newConnection(Envoy::Tcp::ConnectionPool::Callbacks& callbacks) override;

  class ThreadLocalPool : public Upstream::ClusterUpdateCallbacks {
  public:
    ThreadLocalPool(Event::Dispatcher&);
    ConnectionPool::Cancellable* newConnection(Envoy::Tcp::ConnectionPool::Callbacks& callbacks);

    void onClusterAddOrUpdateNonVirtual(Upstream::ThreadLocalCluster& cluster);
    void onHostsAdded(const std::vector<Upstream::HostSharedPtr>& hosts_added);
    void onHostsRemoved(const std::vector<Upstream::HostSharedPtr>& hosts_removed);

    // Upstream::ClusterUpdateCallbacks
    void onClusterAddOrUpdate(Upstream::ThreadLocalCluster& cluster) override {
      onClusterAddOrUpdateNonVirtual(cluster);
    }
    void onClusterRemoval(const std::string& cluster_name) override;

  private:
    Event::Dispatcher& dispatcher_;
    ConnectionManagerImpl& parent_;
    std::string cluster_name_;
    Upstream::ThreadLocalCluster* cluster_;
    Envoy::Common::CallbackHandlePtr host_set_member_update_cb_handle_;
    absl::node_hash_map<std::string, Upstream::HostConstSharedPtr> host_address_map_;
    absl::node_hash_map<Upstream::HostConstSharedPtr, Tcp::ConnectionPool::InstancePtr> pools_;
    std::list<Tcp::ConnectionPool::InstancePtr> pools_to_drain_;
    std::list<Upstream::HostSharedPtr> created_via_redirect_hosts_;
  };

private:
  Upstream::ClusterConnectivityState cluster_manager_state_;
  Upstream::ClusterManager* cm_;
  ThreadLocal::SlotPtr tls_;
};
} // namespace ConnPool
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy