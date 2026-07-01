#include "source/extensions/network/socket_interface/sockmap/bpf_datapath.h"

#include "envoy/common/platform.h"

namespace Envoy {
namespace Network {

bool buildSockKey(const Address::Instance& local, const Address::Instance& peer, SockKey& out) {
  const Address::Ip* local_ip = local.ip();
  const Address::Ip* peer_ip = peer.ip();
  if (local_ip == nullptr || peer_ip == nullptr) {
    return false;
  }
  if (local_ip->version() != Address::IpVersion::v4 ||
      peer_ip->version() != Address::IpVersion::v4) {
    return false;
  }
  // Addresses are already in network order. Ports are host order, so htons stores them network
  // order in the low 16 bits, matching the keys the `sock_ops` and `sk_msg` programs build.
  out.sip4 = local_ip->ipv4()->address();
  out.dip4 = peer_ip->ipv4()->address();
  // Fixed IPv4 tag that must match the value the sock_ops program writes.
  out.family = 1;
  out.pad1 = 0;
  out.pad2 = 0;
  out.sport = htons(local_ip->port());
  out.dport = htons(peer_ip->port());
  return true;
}

} // namespace Network
} // namespace Envoy

#ifdef ENVOY_ENABLE_SOCKMAP

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <fcntl.h>
#include <unistd.h>

#include "source/common/common/logger.h"
#include "source/common/common/utility.h"

namespace Envoy {
namespace Network {
namespace {

constexpr char SockhashName[] = "envoy_sockhash";
constexpr char SockOpsName[] = "envoy_sockops";
constexpr char SkMsgName[] = "envoy_sk_msg";

// Loads the `sock_ops` and `sk_msg` programs through libbpf, attaches the `sk_msg` verdict to the
// sockhash and, when configured, the `sock_ops` program to a cgroup. Every registration is best
// effort.
class LibbpfDatapath : public BpfDatapath, public Logger::Loggable<Logger::Id::connection> {
public:
  static BpfDatapathSharedPtr create(const BpfDatapathConfig& config);

  LibbpfDatapath(struct bpf_object* obj, int sockhash_fd, int sk_msg_prog_fd, int sockops_prog_fd,
                 int cgroup_fd)
      : obj_(obj), sockhash_fd_(sockhash_fd), sk_msg_prog_fd_(sk_msg_prog_fd),
        sockops_prog_fd_(sockops_prog_fd), cgroup_fd_(cgroup_fd) {}
  LibbpfDatapath(const LibbpfDatapath&) = delete;
  LibbpfDatapath& operator=(const LibbpfDatapath&) = delete;

  ~LibbpfDatapath() override {
    bpf_prog_detach2(sk_msg_prog_fd_, sockhash_fd_, BPF_SK_MSG_VERDICT);
    if (cgroup_fd_ >= 0) {
      bpf_prog_detach2(sockops_prog_fd_, cgroup_fd_, BPF_CGROUP_SOCK_OPS);
      ::close(cgroup_fd_);
    }
    bpf_object__close(obj_);
  }

  void registerSocket(os_fd_t fd, const Address::Instance& local,
                      const Address::Instance& peer) override {
    SockKey key;
    if (!buildSockKey(local, peer, key)) {
      return;
    }
    int value = fd;
    if (bpf_map_update_elem(sockhash_fd_, &key, &value, BPF_ANY) != 0) {
      ENVOY_LOG(debug, "sockmap could not register fd {}: {}", fd, errorDetails(errno));
    }
  }

  void unregisterSocket(const Address::Instance& local, const Address::Instance& peer) override {
    SockKey key;
    if (!buildSockKey(local, peer, key)) {
      return;
    }
    // A missing key is expected when the socket was never registered or the kernel already evicted
    // it, so ENOENT is not logged.
    if (bpf_map_delete_elem(sockhash_fd_, &key) != 0 && errno != ENOENT) {
      ENVOY_LOG(debug, "sockmap could not unregister socket: {}", errorDetails(errno));
    }
  }

private:
  struct bpf_object* obj_;
  const int sockhash_fd_;
  const int sk_msg_prog_fd_;
  const int sockops_prog_fd_;
  const int cgroup_fd_;
};

BpfDatapathSharedPtr LibbpfDatapath::create(const BpfDatapathConfig& config) {
  struct bpf_object* obj = bpf_object__open_file(config.bpf_program_path.c_str(), nullptr);
  // Newer libbpf returns nullptr on failure while older releases return an error pointer.
  if (obj == nullptr || libbpf_get_error(obj) != 0) {
    ENVOY_LOG_MISC(warn, "sockmap could not open eBPF object {}", config.bpf_program_path);
    return nullptr;
  }

  struct bpf_map* map = bpf_object__find_map_by_name(obj, SockhashName);
  struct bpf_program* sk_msg = bpf_object__find_program_by_name(obj, SkMsgName);
  struct bpf_program* sockops = bpf_object__find_program_by_name(obj, SockOpsName);
  if (map == nullptr || sk_msg == nullptr || sockops == nullptr) {
    ENVOY_LOG_MISC(warn, "sockmap eBPF object is missing the expected programs or map");
    bpf_object__close(obj);
    return nullptr;
  }

  // A zero would make the kernel reject the sockhash. The proto enforces a minimum of one, so this
  // only guards a config built directly, keeping the placeholder rather than zeroing the map.
  if (config.sockhash_max_entries > 0) {
    bpf_map__set_max_entries(map, config.sockhash_max_entries);
  }

  if (bpf_object__load(obj) != 0) {
    ENVOY_LOG_MISC(warn, "sockmap could not load eBPF programs: {}", errorDetails(errno));
    bpf_object__close(obj);
    return nullptr;
  }

  const int sockhash_fd = bpf_map__fd(map);
  const int sk_msg_fd = bpf_program__fd(sk_msg);
  const int sockops_fd = bpf_program__fd(sockops);

  if (bpf_prog_attach(sk_msg_fd, sockhash_fd, BPF_SK_MSG_VERDICT, 0) != 0) {
    ENVOY_LOG_MISC(warn, "sockmap could not attach the sk_msg program: {}", errorDetails(errno));
    bpf_object__close(obj);
    return nullptr;
  }

  int cgroup_fd = -1;
  if (!config.cgroup_path.empty()) {
    cgroup_fd = ::open(config.cgroup_path.c_str(), O_RDONLY);
    if (cgroup_fd < 0) {
      ENVOY_LOG_MISC(warn,
                     "sockmap could not open cgroup {}, application hops will not be "
                     "accelerated: {}",
                     config.cgroup_path, errorDetails(errno));
    } else if (bpf_prog_attach(sockops_fd, cgroup_fd, BPF_CGROUP_SOCK_OPS, 0) != 0) {
      ENVOY_LOG_MISC(warn, "sockmap could not attach the sock_ops program: {}",
                     errorDetails(errno));
      ::close(cgroup_fd);
      cgroup_fd = -1;
    }
  }

  ENVOY_LOG_MISC(info, "sockmap acceleration enabled using {}", config.bpf_program_path);
  return std::make_shared<LibbpfDatapath>(obj, sockhash_fd, sk_msg_fd, sockops_fd, cgroup_fd);
}

} // namespace

BpfDatapathSharedPtr createBpfDatapath(const BpfDatapathConfig& config) {
  if (config.bpf_program_path.empty()) {
    ENVOY_LOG_MISC(info, "sockmap acceleration disabled, no bpf_program_path configured");
    return nullptr;
  }
  return LibbpfDatapath::create(config);
}

} // namespace Network
} // namespace Envoy

#else

#include "source/common/common/logger.h"

namespace Envoy {
namespace Network {

BpfDatapathSharedPtr createBpfDatapath(const BpfDatapathConfig& config) {
  if (!config.bpf_program_path.empty()) {
    ENVOY_LOG_MISC(
        warn, "sockmap acceleration was not compiled in but bpf_program_path is set, using the "
              "standard datapath");
  } else {
    ENVOY_LOG_MISC(debug, "sockmap acceleration was not compiled in, using the standard datapath");
  }
  return nullptr;
}

} // namespace Network
} // namespace Envoy

#endif
