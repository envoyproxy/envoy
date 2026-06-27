#include "source/common/network/reuse_port_bpf_cpu_steering_option_impl.h"

#include "envoy/network/socket.h"

#include "source/common/common/macros.h"
#include "source/common/common/scalar_to_byte_vector.h"
#include "source/common/common/utility.h"

#if defined(__linux__)
#include <linux/filter.h>
#endif

namespace Envoy {
namespace Network {

bool ReusePortBpfCpuSteeringOptionImpl::setOption(
    Socket& socket, envoy::config::core::v3::SocketOption::SocketState state) const {
#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__)
  // Attach when the socket is listening so the program runs against the established reuse port
  // group. The kernel uses the returned value as the index of the socket in the group. Worker i
  // binds the `i-th` socket of the group and accepts on it, so the group index equals the worker
  // index and the program returns the worker index for the receiving CPU. Each worker socket
  // attaches the same group wide program and the kernel keeps the last one.
  if (state != envoy::config::core::v3::SocketOption::STATE_LISTENING) {
    return true;
  }
  // With no worker assignment there is nothing to steer, so leave the default reuse port hashing
  // in place.
  if (worker_cpus_.empty()) {
    return true;
  }
  const uint32_t worker_count = worker_cpus_.size();
  // A holds the receiving CPU. Each worker gets a CPU compare that returns its socket index, and
  // each compare jumps straight to its own return so the classic BPF jump offsets stay 0 or 1
  // regardless of worker count. A CPU with no pinned worker falls through to a modulo so the
  // connection still lands on a valid socket. The program is 2 * worker_count + 3 instructions,
  // bounded by the CPU count, so it stays within the classic BPF instruction limit.
  std::vector<sock_filter> filter;
  filter.reserve(2 * worker_count + 3);
  filter.push_back(
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, static_cast<uint32_t>(SKF_AD_OFF + SKF_AD_CPU)));
  for (uint32_t i = 0; i < worker_count; i++) {
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, worker_cpus_[i], 0, 1));
    filter.push_back(BPF_STMT(BPF_RET | BPF_K, i));
  }
  filter.push_back(BPF_STMT(BPF_ALU | BPF_MOD | BPF_K, worker_count));
  filter.push_back(BPF_STMT(BPF_RET | BPF_A, 0));
  sock_fprog prog{};
  prog.len = static_cast<unsigned short>(filter.size());
  prog.filter = filter.data();
  const Api::SysCallIntResult result =
      socket.setSocketOption(ENVOY_ATTACH_REUSEPORT_CBPF.level(),
                             ENVOY_ATTACH_REUSEPORT_CBPF.option(), &prog, sizeof(prog));
  if (result.return_value_ != 0) {
    // Degrade to default reuse port hashing rather than failing the listener.
    ENVOY_LOG(warn, "reuse port BPF steering attach failed, using default reuse port hashing: {}",
              errorDetails(result.errno_));
  }
  return true;
#else
  UNREFERENCED_PARAMETER(socket);
  UNREFERENCED_PARAMETER(state);
  return true;
#endif
}

void ReusePortBpfCpuSteeringOptionImpl::hashKey(std::vector<uint8_t>& hash_key) const {
  if (ENVOY_ATTACH_REUSEPORT_CBPF.hasValue()) {
    pushScalarToByteVector(ENVOY_ATTACH_REUSEPORT_CBPF.level(), hash_key);
    pushScalarToByteVector(ENVOY_ATTACH_REUSEPORT_CBPF.option(), hash_key);
    for (const uint32_t cpu : worker_cpus_) {
      pushScalarToByteVector(cpu, hash_key);
    }
  }
}

std::optional<Socket::Option::Details> ReusePortBpfCpuSteeringOptionImpl::getOptionDetails(
    const Socket&, envoy::config::core::v3::SocketOption::SocketState) const {
  // No details for this option.
  return std::nullopt;
}

bool ReusePortBpfCpuSteeringOptionImpl::isSupported() const {
  return ENVOY_ATTACH_REUSEPORT_CBPF.hasValue();
}

} // namespace Network
} // namespace Envoy
