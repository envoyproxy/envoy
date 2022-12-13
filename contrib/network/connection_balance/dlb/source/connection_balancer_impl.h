#pragma once

#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/network/connection_balancer_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/server/active_tcp_listener.h"

#include "contrib/envoy/extensions/network/connection_balance/dlb/v3alpha/dlb.pb.h"
#include "contrib/envoy/extensions/network/connection_balance/dlb/v3alpha/dlb.pb.validate.h"

#ifndef DLB_DISABLED
#include "dlb.h"
#endif

namespace Envoy {
namespace Extensions {
namespace Dlb {

class DlbBalancedConnectionHandlerImpl : public Envoy::Network::BalancedConnectionHandler,
                                         public Logger::Loggable<Logger::Id::connection> {
public:
  DlbBalancedConnectionHandlerImpl(Envoy::Network::BalancedConnectionHandler& handler, int index,
                                   std::string name)
      : handler_(handler), index_(index), name_(name) {}
  // Post socket to Dlb hardware.
  void post(Network::ConnectionSocketPtr&& socket) override;

  void onAcceptWorker(Network::ConnectionSocketPtr&&, bool, bool) override {}

  // Create Dlb event and callback.
  void setDlbEvent();

  // Get socket from Dlb hardware and re-use listener onAcceptWorker().
  void onDlbEvents(uint32_t flags);

  // Only for override, those are never used.
  uint64_t numConnections() const override { return 0; }
  void incNumConnections() override {}

private:
  Envoy::Network::BalancedConnectionHandler& handler_;
  int index_;
  std::string name_;
  Envoy::Event::FileEventPtr dlb_event_;
};

// The dir should always be "/dev" in production.
// For test it is a temporary directory.
// Return Dlb device id, absl::nullopt means error.
static absl::optional<uint> detectDlbDevice(const uint config_id, const std::string& dir) {
  uint device_id = config_id;
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  struct stat buffer;

  std::string device_path = fmt::format("{}/dlb{}", dir, device_id);
  if (os_sys_calls.stat(device_path.c_str(), &buffer).return_value_ != 0) {
    int i = 0;
    // auto detect available dlb devices, now the max number of dlb device id is 63.
    const int max_id = 64;
    for (; i < max_id; i++) {
      device_path = fmt::format("{}/dlb{}", dir, i);
      if (os_sys_calls.stat(device_path.c_str(), &buffer).return_value_ == 0) {
        device_id = i;
        break;
      }
    }
    if (i == 64) {
      return absl::nullopt;
    }
  }
  return absl::optional<uint>{device_id};
}

class DlbConnectionBalanceFactory : public Envoy::Network::ConnectionBalanceFactory,
                                    public Logger::Loggable<Logger::Id::config> {
public:
  ~DlbConnectionBalanceFactory() override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::network::connection_balance::dlb::v3alpha::Dlb>();
  }

  Envoy::Network::ConnectionBalancerSharedPtr
  createConnectionBalancerFromProto(const Protobuf::Message& config,
                                    Server::Configuration::FactoryContext& context) override;

  std::string name() const override { return "envoy.network.connection_balance.dlb"; }

  // Init those only when Envoy start.
  int domain_id, ldb_pool_id, dir_pool_id, tx_queue_id;
#ifndef DLB_DISABLED
  dlb_domain_hdl_t domain;
  dlb_hdl_t dlb;
  dlb_dev_cap_t cap;

  // Share those cross worker threads.
  std::vector<dlb_port_hdl_t> tx_ports, rx_ports;
  uint max_retries;
#endif
  std::vector<int> efds;
  std::vector<std::shared_ptr<DlbBalancedConnectionHandlerImpl>> dlb_handlers;
  std::vector<Envoy::Event::FileEventPtr> file_events;
#ifndef DLB_DISABLED
  const static int cq_depth = 8;
  static int createLdbPort(dlb_domain_hdl_t domain, dlb_dev_cap_t cap, int ldb_pool, int dir_pool) {
    dlb_create_port_t args;

    if (!cap.combined_credits) {
      args.ldb_credit_pool_id = ldb_pool;
      args.dir_credit_pool_id = dir_pool;
    } else {
      args.credit_pool_id = ldb_pool;
    }
    args.cq_depth = cq_depth;
    args.num_ldb_event_state_entries = cq_depth * 2;
    args.cos_id = DLB_PORT_COS_ID_ANY;

    return dlb_create_ldb_port(domain, &args);
  }

  static int createSchedDomain(dlb_hdl_t dlb, dlb_resources_t rsrcs, dlb_dev_cap_t cap) {
    int p_rsrsc = 100;
    dlb_create_sched_domain_t args;

    args.num_ldb_queues = 1;
    args.num_ldb_ports = 64;
    args.num_dir_ports = 0;
    args.num_ldb_event_state_entries = 2 * args.num_ldb_ports * cq_depth;
    if (!cap.combined_credits) {
      args.num_ldb_credits = rsrcs.max_contiguous_ldb_credits * p_rsrsc / 100;
      args.num_dir_credits = rsrcs.max_contiguous_dir_credits * p_rsrsc / 100;

      args.num_ldb_credit_pools = 1;
      args.num_dir_credit_pools = 1;
    } else {
      args.num_credits = rsrcs.num_credits * p_rsrsc / 100;
      args.num_credit_pools = 1;
    }

    args.num_sn_slots[0] = rsrcs.num_sn_slots[0] * p_rsrsc / 100;
    args.num_sn_slots[1] = rsrcs.num_sn_slots[1] * p_rsrsc / 100;

    return dlb_create_sched_domain(dlb, &args);
  }

  static int createLdbQueue(dlb_domain_hdl_t domain) {
    dlb_create_ldb_queue_t args = {0, 0};

    return dlb_create_ldb_queue(domain, &args);
  }
#endif
};

using DlbConnectionBalanceFactorySingleton = InjectableSingleton<DlbConnectionBalanceFactory>;

/**
 * Implementation of connection balancer that does balancing with the help of Dlb hardware.
 */
class DlbConnectionBalancerImpl : public Envoy::Network::ConnectionBalancer,
                                  public Logger::Loggable<Logger::Id::connection> {
public:
  /** registerHandler() does following things:
   * - get listener
   * - create DlbBalancedConnectionHandlerImpl
   * - create Dlb event of DlbBalancedConnectionHandlerImpl
   */
  void registerHandler(Envoy::Network::BalancedConnectionHandler&) override;

  // Remove DlbBalancedConnectionHandlerImpl by listener.
  void unregisterHandler(Envoy::Network::BalancedConnectionHandler&) override;

  // Return DlbBalancedConnectionHandlerImpl to handle Dlb send/recv.
  Envoy::Network::BalancedConnectionHandler&
  pickTargetHandler(Envoy::Network::BalancedConnectionHandler& current_handler) override;
};

} // namespace Dlb
} // namespace Extensions
} // namespace Envoy
