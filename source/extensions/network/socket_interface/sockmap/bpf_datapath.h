#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/common/pure.h"
#include "envoy/network/address.h"

namespace Envoy {
namespace Network {

// Connection tuple key for the sockhash. The layout must stay in sync with struct sock_key in
// `bpf/sockmap_kern.c` so user space registered sockets match the keys built by the `sock_ops`
// program.
struct SockKey {
  uint32_t sip4;
  uint32_t dip4;
  uint8_t family;
  uint8_t pad1;
  uint16_t pad2;
  uint32_t sport;
  uint32_t dport;
};

// Builds the sockhash key for a connected IPv4 socket from its local and peer addresses using the
// same convention as the `sock_ops` program. Returns false when either address is not IPv4.
bool buildSockKey(const Address::Instance& local, const Address::Instance& peer, SockKey& out);

// Settings the datapath needs to load and attach the eBPF programs.
struct BpfDatapathConfig {
  std::string bpf_program_path;
  std::string cgroup_path;
  uint32_t sockhash_max_entries{0};
};

// Registers connected sockets into the sockhash so the `sk_msg` program can redirect their
// payloads. Registration is best effort and never throws, so a failure leaves the socket on the
// standard datapath. The matching tuple key is removed when the socket closes so a later connection
// that reuses the tuple is not redirected into a stale entry.
class BpfDatapath {
public:
  virtual ~BpfDatapath() = default;

  virtual void registerSocket(os_fd_t fd, const Address::Instance& local,
                              const Address::Instance& peer) PURE;

  // Removes the tuple key for a connected socket from the sockhash. Best effort and never throws; a
  // missing key is not an error.
  virtual void unregisterSocket(const Address::Instance& local, const Address::Instance& peer) PURE;
};

using BpfDatapathSharedPtr = std::shared_ptr<BpfDatapath>;

// Creates the datapath from config. Returns nullptr when acceleration is disabled or the eBPF
// programs cannot be loaded or attached, in which case all sockets use the standard datapath.
BpfDatapathSharedPtr createBpfDatapath(const BpfDatapathConfig& config);

} // namespace Network
} // namespace Envoy
