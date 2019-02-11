#include "server/hot_restarting_base.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Server {

void HotRestartingBase::initDomainSocketAddress(sockaddr_un* address) {
  memset(address, 0, sizeof(*address));
  address->sun_family = AF_UNIX;
}

sockaddr_un HotRestartingBase::createDomainSocketAddress(uint64_t id, const std::string& role) {
  // Right now we only allow a maximum of 3 concurrent envoy processes to be running. When the third
  // starts up it will kill the oldest parent.
  const uint64_t MAX_CONCURRENT_PROCESSES = 3;
  id = id % MAX_CONCURRENT_PROCESSES;

  // This creates an anonymous domain socket name (where the first byte of the name of \0).
  sockaddr_un address;
  initDomainSocketAddress(&address);
  StringUtil::strlcpy(&address.sun_path[1],
                      fmt::format("envoy_domain_socket_{}_{}", role, base_id_ + id).c_str(),
                      sizeof(address.sun_path) - 1);
  address.sun_path[0] = 0;
  return address;
}

void HotRestartingBase::bindDomainSocket(uint64_t id, const std::string& role) {
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  // This actually creates the socket and binds it. We use the socket in datagram mode so we can
  // easily read single messages.
  my_domain_socket_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  sockaddr_un address = createDomainSocketAddress(id, role);
  Api::SysCallIntResult result =
      os_sys_calls.bind(my_domain_socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  if (result.rc_ != 0) {
    throw EnvoyException(
        fmt::format("unable to bind domain socket with id={} (see --base-id option)", id));
  }
}

void HotRestartingBase::sendMessage(sockaddr_un& address, RpcBase& rpc) {
  iovec iov[1];
  iov[0].iov_base = &rpc;
  iov[0].iov_len = rpc.length_;

  msghdr message;
  memset(&message, 0, sizeof(message));
  message.msg_name = &address;
  message.msg_namelen = sizeof(address);
  message.msg_iov = iov;
  message.msg_iovlen = 1;
  int rc = sendmsg(my_domain_socket_, &message, 0);
  RELEASE_ASSERT(rc != -1, "");
}

HotRestartingBase::RpcBase* HotRestartingBase::receiveRpc(bool block) {
  // By default the domain socket is non blocking. If we need to block, make it blocking first.
  if (block) {
    int rc = fcntl(my_domain_socket_, F_SETFL, 0);
    RELEASE_ASSERT(rc != -1, "");
  }

  iovec iov[1];
  iov[0].iov_base = &rpc_buffer_[0];
  iov[0].iov_len = rpc_buffer_.size();

  // We always setup to receive an FD even though most messages do not pass one.
  uint8_t control_buffer[CMSG_SPACE(sizeof(int))];
  memset(control_buffer, 0, CMSG_SPACE(sizeof(int)));

  msghdr message;
  memset(&message, 0, sizeof(message));
  message.msg_iov = iov;
  message.msg_iovlen = 1;
  message.msg_control = control_buffer;
  message.msg_controllen = CMSG_SPACE(sizeof(int));

  int rc = recvmsg(my_domain_socket_, &message, 0);
  if (!block && rc == -1 && errno == EAGAIN) {
    return nullptr;
  }

  RELEASE_ASSERT(rc != -1, "");
  RELEASE_ASSERT(message.msg_flags == 0, "");

  // Turn non-blocking back on if we made it blocking.
  if (block) {
    int rc = fcntl(my_domain_socket_, F_SETFL, O_NONBLOCK);
    RELEASE_ASSERT(rc != -1, "");
  }

  RpcBase* rpc = reinterpret_cast<RpcBase*>(&rpc_buffer_[0]);
  RELEASE_ASSERT(static_cast<uint64_t>(rc) == rpc->length_, "");

  // We should only get control data in a GetListenSocketReply. If that's the case, pull the
  // cloned fd out of the control data and stick it into the RPC so that higher level code does
  // need to deal with any of this.
  for (cmsghdr* cmsg = CMSG_FIRSTHDR(&message); cmsg != nullptr;
       cmsg = CMSG_NXTHDR(&message, cmsg)) {

    if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
        rpc->type_ == RpcMessageType::GetListenSocketReply) {

      reinterpret_cast<RpcGetListenSocketReply*>(rpc)->fd_ =
          *reinterpret_cast<int*>(CMSG_DATA(cmsg));
    } else {
      RELEASE_ASSERT(false, "");
    }
  }

  return rpc;
}

} // namespace Server
} // namespace Envoy
