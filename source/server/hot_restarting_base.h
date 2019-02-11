#pragma once

#include <fcntl.h>
#include <sys/un.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/server/hot_restart.h"
#include "envoy/server/options.h"
#include "envoy/stats/stats_options.h"

#include "common/common/assert.h"
#include "common/stats/raw_stat_data.h"

namespace Envoy {
namespace Server {

/**
 * Logic shared by the implementations of both sides of the child<-->parent hot restart protocol:
 * domain socket communication, and our ad hoc RPC protocol.
 */
class HotRestartingBase {
protected:
  HotRestartingBase(int base_id) : base_id_(base_id) {}

  enum class RpcMessageType {
    DrainListenersRequest = 1,
    GetListenSocketRequest = 2,
    GetListenSocketReply = 3,
    ShutdownAdminRequest = 4,
    ShutdownAdminReply = 5,
    TerminateRequest = 6,
    UnknownRequestReply = 7,
    GetStatsRequest = 8,
    GetStatsReply = 9
  };

  PACKED_STRUCT(struct RpcBase {
    RpcBase(RpcMessageType type, uint64_t length = sizeof(RpcBase))
        : type_(type), length_(length) {}

    RpcMessageType type_;
    uint64_t length_;
  });

  PACKED_STRUCT(struct RpcGetListenSocketRequest
                : public RpcBase {
                  RpcGetListenSocketRequest()
                      : RpcBase(RpcMessageType::GetListenSocketRequest, sizeof(*this)) {}

                  char address_[256]{0};
                });

  PACKED_STRUCT(struct RpcGetListenSocketReply
                : public RpcBase {
                  RpcGetListenSocketReply()
                      : RpcBase(RpcMessageType::GetListenSocketReply, sizeof(*this)) {}

                  int fd_{0};
                });

  PACKED_STRUCT(struct RpcShutdownAdminReply
                : public RpcBase {
                  RpcShutdownAdminReply()
                      : RpcBase(RpcMessageType::ShutdownAdminReply, sizeof(*this)) {}

                  uint64_t original_start_time_{0};
                });

  PACKED_STRUCT(struct RpcGetStatsReply
                : public RpcBase {
                  RpcGetStatsReply() : RpcBase(RpcMessageType::GetStatsReply, sizeof(*this)) {}

                  uint64_t memory_allocated_{0};
                  uint64_t num_connections_{0};
                  uint64_t unused_[16]{0};
                });

  template <class rpc_class, RpcMessageType rpc_type> rpc_class* receiveTypedRpc() {
    RpcBase* base_message = receiveRpc(true);
    RELEASE_ASSERT(base_message->length_ == sizeof(rpc_class), "");
    RELEASE_ASSERT(base_message->type_ == rpc_type, "");
    return reinterpret_cast<rpc_class*>(base_message);
  }

  void initDomainSocketAddress(sockaddr_un* address);
  sockaddr_un createDomainSocketAddress(uint64_t id, const std::string& role);
  void bindDomainSocket(uint64_t id, const std::string& role);
  int my_domain_socket() const { return my_domain_socket_; }
  void sendMessage(sockaddr_un& address, RpcBase& rpc);
  RpcBase* receiveRpc(bool block);

private:
  const int base_id_;
  int my_domain_socket_{-1};
  std::array<uint8_t, 4096> rpc_buffer_;
};

} // namespace Server
} // namespace Envoy
