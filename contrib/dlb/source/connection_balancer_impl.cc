#include "contrib/dlb/source/connection_balancer_impl.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <memory>

#include "contrib/envoy/extensions/network/connection_balance/dlb/v3alpha/dlb.pb.h"

#ifndef DLB_DISABLED
#include "dlb.h"
#endif

namespace Envoy {
namespace Extensions {
namespace Dlb {

Envoy::Network::ConnectionBalancerSharedPtr
DlbConnectionBalanceFactory::fallback(const std::string& message) {
  switch (fallback_policy) {
  case envoy::extensions::network::connection_balance::dlb::v3alpha::Dlb::ExactConnectionBalance: {
    ENVOY_LOG(warn, fmt::format("error: {}, fallback to Exact Connection Balance", message));
    return std::make_shared<Network::ExactConnectionBalancerImpl>();
  }
  case envoy::extensions::network::connection_balance::dlb::v3alpha::Dlb::NopConnectionBalance: {
    ENVOY_LOG(warn, fmt::format("error: {}, fallback to Nop Connection Balance", message));
    return std::make_shared<Network::NopConnectionBalancerImpl>();
  }
  case envoy::extensions::network::connection_balance::dlb::v3alpha::Dlb::None:
  default:
    ExceptionUtil::throwEnvoyException(message);
  }
}

Envoy::Network::ConnectionBalancerSharedPtr
DlbConnectionBalanceFactory::createConnectionBalancerFromProto(
    const Protobuf::Message& config, Server::Configuration::FactoryContext& context) {
  const auto& typed_config =
      dynamic_cast<const envoy::config::core::v3::TypedExtensionConfig&>(config);
  envoy::extensions::network::connection_balance::dlb::v3alpha::Dlb dlb_config;
  auto status = Envoy::MessageUtil::unpackToNoThrow(typed_config.typed_config(), dlb_config);
  if (!status.ok()) {
    return fallback(fmt::format("unexpected dlb config: {}", typed_config.DebugString()));
  }

  fallback_policy = dlb_config.fallback_policy();

  const uint32_t worker_num = context.serverFactoryContext().options().concurrency();

  if (worker_num > 32) {
    return fallback("Dlb connection balanncer only supports up to 32 worker threads, "
                    "please decrease the number of threads by `--concurrency`");
  }

  const uint& config_id = dlb_config.id();
  const auto& result = detectDlbDevice(config_id, "/dev");
  if (!result.has_value()) {
    return fallback("no available dlb hardware");
  }

  const uint& device_id = result.value();
  if (device_id != config_id) {
    ENVOY_LOG(warn, "dlb device {} is not found, use dlb device {} instead", config_id, device_id);
  }

#ifdef DLB_DISABLED
  throw EnvoyException("X86_64 architecture is required for Dlb.");
#else

  max_retries = dlb_config.max_retries();

  dlb_resources_t rsrcs;
  if (dlb_open(device_id, &dlb) == -1) {
    return fallback(fmt::format("dlb_open {}", errorDetails(errno)));
  }
  if (dlb_get_dev_capabilities(dlb, &cap)) {
    return fallback(fmt::format("dlb_get_dev_capabilities {}", errorDetails(errno)));
  }

  if (dlb_get_num_resources(dlb, &rsrcs)) {
    return fallback(fmt::format("dlb_get_num_resources {}", errorDetails(errno)));
  }

  ENVOY_LOG(debug,
            "dlb available resources: domains: {}, LDB queues: {}, LDB ports: {}, "
            "ES entries: {}, Contig ES entries: {}, LDB credits: {}, Config LDB credits: {}, LDB "
            "credit pools: {}",
            rsrcs.num_sched_domains, rsrcs.num_ldb_queues, rsrcs.num_ldb_ports,
            rsrcs.num_ldb_event_state_entries, rsrcs.max_contiguous_ldb_event_state_entries,
            rsrcs.num_ldb_credits, rsrcs.max_contiguous_ldb_credits, rsrcs.num_ldb_credit_pools);

  if (rsrcs.num_ldb_ports < 2 * worker_num) {
    return fallback(fmt::format("no available dlb port resources, request: {}, available: {}",
                                2 * worker_num, rsrcs.num_ldb_ports));
  }

  domain_id = createSchedDomain(dlb, rsrcs, cap, 2 * worker_num);
  if (domain_id == -1) {
    return fallback(fmt::format("dlb_create_sched_domain_ {}", errorDetails(errno)));
  }

  domain = dlb_attach_sched_domain(dlb, domain_id);
  if (domain == nullptr) {
    return fallback(fmt::format("dlb_attach_sched_domain {}", errorDetails(errno)));
  }

  const int partial_resources = 100;
  if (!cap.combined_credits) {
    int max_ldb_credits = rsrcs.num_ldb_credits * partial_resources / 100;
    int max_dir_credits = rsrcs.num_dir_credits * partial_resources / 100;

    ldb_pool_id = dlb_create_ldb_credit_pool(domain, max_ldb_credits);

    if (ldb_pool_id == -1) {
      return fallback(fmt::format("dlb_create_ldb_credit_pool {}", errorDetails(errno)));
    }

    dir_pool_id = dlb_create_dir_credit_pool(domain, max_dir_credits);

    if (dir_pool_id == -1) {
      return fallback(fmt::format("dlb_create_dir_credit_pool {}", errorDetails(errno)));
    }
  } else {
    int max_credits = rsrcs.num_credits * partial_resources / 100;

    ldb_pool_id = dlb_create_credit_pool(domain, max_credits);

    if (ldb_pool_id == -1) {
      return fallback(fmt::format("dlb_create_credit_pool {}", errorDetails(errno)));
    }
  }

  tx_queue_id = createLdbQueue(domain);
  if (tx_queue_id == -1) {
    return fallback(fmt::format("tx create_ldb_queue {}", errorDetails(errno)));
  }

  for (uint i = 0; i < worker_num; i++) {
    int tx_port_id = createLdbPort(domain, cap, ldb_pool_id, dir_pool_id);
    if (tx_port_id == -1) {
      return fallback(fmt::format("tx dlb_create_ldb_port {}", errorDetails(errno)));
    }

    dlb_port_hdl_t tx_port = dlb_attach_ldb_port(domain, tx_port_id);
    if (tx_port == nullptr) {
      return fallback(fmt::format("tx dlb_attach_ldb_port {}", errorDetails(errno)));
    }
    tx_ports.push_back(tx_port);

    int rx_port_id = createLdbPort(domain, cap, ldb_pool_id, dir_pool_id);
    if (rx_port_id == -1) {
      return fallback(fmt::format("rx dlb_create_ldb_port {}", errorDetails(errno)));
    }

    dlb_port_hdl_t rx_port = dlb_attach_ldb_port(domain, rx_port_id);
    if (rx_port == nullptr) {
      return fallback(fmt::format("rx dlb_attach_ldb_port {}", errorDetails(errno)));
    }
    rx_ports.push_back(rx_port);

    if (dlb_link_queue(rx_port, tx_queue_id, 0) == -1) {
      return fallback(fmt::format("dlb_link_queue {}", errorDetails(errno)));
    }

    int efd = eventfd(0, EFD_NONBLOCK);
    if (efd < 0) {
      return fallback(fmt::format("dlb eventfd {}", errorDetails(errno)));
    }
    if (dlb_enable_cq_epoll(rx_port, true, efd)) {
      return fallback(fmt::format("dlb_enable_cq_epoll {}", errorDetails(errno)));
    }
    efds.push_back(efd);
  }

  if (dlb_launch_domain_alert_thread(domain, nullptr, nullptr)) {
    return fallback(fmt::format("dlb_launch_domain_alert_thread {}", errorDetails(errno)));
  }

  if (dlb_start_sched_domain(domain)) {
    return fallback(fmt::format("dlb_start_sched_domain {}", errorDetails(errno)));
  }
#endif
  DlbConnectionBalanceFactorySingleton::initialize(this);

  return std::make_shared<DlbConnectionBalancerImpl>();
}

DlbConnectionBalanceFactory::~DlbConnectionBalanceFactory() {
#ifndef DLB_DISABLED
  if (dlb != nullptr) {
    for (dlb_port_hdl_t port : rx_ports) {
      if (dlb_disable_port(port)) {
        ENVOY_LOG(error, "dlb_disable_port {}", errorDetails(errno));
      }
      if (dlb_detach_port(port) == -1) {
        ENVOY_LOG(error, "dlb_detach_port {}", errorDetails(errno));
      }
    }
    for (dlb_port_hdl_t port : tx_ports) {
      if (dlb_disable_port(port)) {
        ENVOY_LOG(error, "dlb_disable_port {}", errorDetails(errno));
      }
      if (dlb_detach_port(port) == -1) {
        ENVOY_LOG(error, "dlb_detach_port {}", errorDetails(errno));
      }
    }
    if (dlb_detach_sched_domain(domain) == -1) {
      ENVOY_LOG(error, "dlb_detach_sched_domain {}", errorDetails(errno));
    }

    if (dlb_reset_sched_domain(dlb, domain_id) == -1) {
      ENVOY_LOG(error, "dlb_reset_sched_domain {}", errorDetails(errno));
    }

    if (dlb_close(dlb) == -1) {
      ENVOY_LOG(error, "dlb_close {}", errorDetails(errno));
    }
  }

#endif
  for (int fd : efds) {
    if (close(fd) == -1) {
      ENVOY_LOG(error, "dlb close fd {}", errorDetails(errno));
    }
  }
}

REGISTER_FACTORY(DlbConnectionBalanceFactory, Envoy::Network::ConnectionBalanceFactory);

void DlbBalancedConnectionHandlerImpl::setDlbEvent() {
  auto listener = dynamic_cast<Envoy::Server::ActiveTcpListener*>(&handler_);

  dlb_event_ = listener->dispatcher().createFileEvent(
      DlbConnectionBalanceFactorySingleton::get().efds[index_],
      [this](uint32_t events) -> void { onDlbEvents(events); }, Event::FileTriggerType::Level,
      Event::FileReadyType::Read);
  dlb_event_->setEnabled(Event::FileReadyType::Read);
}

void DlbBalancedConnectionHandlerImpl::post(
    [[maybe_unused]] Network::ConnectionSocketPtr&& socket) {
#ifdef DLB_DISABLED
  throw EnvoyException("X86_64 architecture is required for Dlb.");
#else
  // The pointer will be casted to unique_ptr in onDlbEvents(), no need to consider free.
  auto s = socket.release();
  dlb_event_t events[1];
  events[0].send.queue_id = DlbConnectionBalanceFactorySingleton::get().tx_queue_id;
  events[0].send.sched_type = SCHED_UNORDERED;
  events[0].adv_send.udata64 = reinterpret_cast<std::uintptr_t>(s);
  int ret = dlb_send(DlbConnectionBalanceFactorySingleton::get().tx_ports[index_], 1, &events[0]);
  if (ret != 1) {
    if (DlbConnectionBalanceFactorySingleton::get().max_retries > 0) {
      uint i = 0;
      while (i < DlbConnectionBalanceFactorySingleton::get().max_retries) {
        ENVOY_LOG(debug, "{} dlb_send fail, start retry, errono: {}", name_, errno);
        ret = dlb_send(DlbConnectionBalanceFactorySingleton::get().tx_ports[index_], 1, &events[0]);
        if (ret == 1) {
          ENVOY_LOG(warn, "{} dlb_send retry {} times and succeed", name_, i + 1);
          break;
        }
        i++;
      }

      if (ret != 1) {
        ENVOY_LOG(error,
                  "{} dlb_send fail with {} times retry, errono: {}, message: {}, increase "
                  "max_retries may help",
                  name_, DlbConnectionBalanceFactorySingleton::get().max_retries, errno,
                  errorDetails(errno));
      }
    } else {
      ENVOY_LOG(error,
                "{} dlb_send fail without retry, errono: {}, message: {}, set "
                "max_retries may help",
                name_, DlbConnectionBalanceFactorySingleton::get().max_retries, errno,
                errorDetails(errno));
    }

  } else {
    ENVOY_LOG(debug, "{} dlb send fd {}", name_, s->ioHandle().fdDoNotUse());
  }
#endif
}

void DlbBalancedConnectionHandlerImpl::onDlbEvents(uint32_t flags) {
  ASSERT(flags & (Event::FileReadyType::Read));
#ifdef DLB_DISABLED
  throw EnvoyException("X86_64 architecture is required for Dlb.");
#else
  dlb_event_t dlb_events[32];
  int num_rx = dlb_recv(DlbConnectionBalanceFactorySingleton::get().rx_ports.at(index_), 32, false,
                        dlb_events);
  if (num_rx == 0) {
    ENVOY_LOG(debug, "{} dlb receive none, skip", name_);
    return;
  } else {
    ENVOY_LOG(debug, "{} get dlb event {}", name_, num_rx);
  }

  int ret = dlb_release(DlbConnectionBalanceFactorySingleton::get().rx_ports.at(index_), num_rx);
  if (ret != num_rx) {
    ENVOY_LOG(debug, "{} dlb release {}", name_, errorDetails(errno));
  }
  for (int i = 0; i < num_rx; i++) {
    if (dlb_events[i].recv.error) {
      ENVOY_LOG(error, "{} dlb receive {}", name_, errorDetails(errno));
      continue;
    }

    if (dlb_events[i].recv.udata64) {
      const uint64_t data = dlb_events[i].recv.udata64;
      auto socket = reinterpret_cast<Network::ConnectionSocket*>(data);

      ENVOY_LOG(debug, "{} dlb recv {}", name_, socket->ioHandle().fdDoNotUse());
      auto listener = dynamic_cast<Envoy::Server::ActiveTcpListener*>(&handler_);
      auto active_socket = std::make_unique<Envoy::Server::ActiveTcpSocket>(
          *listener, std::unique_ptr<Network::ConnectionSocket>(socket),
          listener->config_->handOffRestoredDestinationConnections());
      listener->onSocketAccepted(std::move(active_socket));
      listener->incNumConnections();
    }
  }
#endif
}

void DlbConnectionBalancerImpl::registerHandler(
    Envoy::Network::BalancedConnectionHandler& handler) {

  auto listener = dynamic_cast<Envoy::Server::ActiveTcpListener*>(&handler);

  const std::string worker_name = listener->dispatcher().name();
  const int index =
      std::stoi(worker_name.substr(worker_name.find_first_of('_') + 1, worker_name.size()));

  auto dlb_handler =
      std::make_shared<DlbBalancedConnectionHandlerImpl>(handler, index, worker_name);
  dlb_handler->setDlbEvent();

  DlbConnectionBalanceFactorySingleton::get().dlb_handlers.push_back(dlb_handler);
}

void DlbConnectionBalancerImpl::unregisterHandler(
    Envoy::Network::BalancedConnectionHandler& handler) {
  auto listener = dynamic_cast<Envoy::Server::ActiveTcpListener*>(&handler);
  auto worker_name = listener->dispatcher().name();
  int index = std::stoi(worker_name.substr(worker_name.find_first_of('_') + 1, worker_name.size()));

  // Now Dlb does not support change config when running, clean Dlb related config in
  // DlbBalancedConnectionHandlerImpl
  auto dlb_handlers = DlbConnectionBalanceFactorySingleton::get().dlb_handlers;
  dlb_handlers.erase(dlb_handlers.begin() + index);
}

Envoy::Network::BalancedConnectionHandler& DlbConnectionBalancerImpl::pickTargetHandler(
    Envoy::Network::BalancedConnectionHandler& current_handler) {
  auto listener = dynamic_cast<Envoy::Server::ActiveTcpListener*>(&current_handler);
  auto worker_name = listener->dispatcher().name();
  const int index =
      std::stoi(worker_name.substr(worker_name.find_first_of('_') + 1, worker_name.size()));
  return *DlbConnectionBalanceFactorySingleton::get().dlb_handlers[index];
}

} // namespace Dlb
} // namespace Extensions
} // namespace Envoy
