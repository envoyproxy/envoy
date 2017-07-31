#pragma once

#include <fcntl.h>
#include <sys/un.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

#include "envoy/server/hot_restart.h"
#include "envoy/server/options.h"

#include "common/common/assert.h"
#include "common/stats/stats_impl.h"

#include "exe/shared_memory.h"

namespace Envoy {
namespace Server {

#ifdef __linux__
/**
 * Implementation of HotRestart built for Linux.
 */
class HotRestartImpl : public HotRestart,
                       public Stats::RawStatDataAllocator,
                       Logger::Loggable<Logger::Id::main> {
public:
  HotRestartImpl(Options& options);

  Thread::BasicLockable& logLock() { return log_lock_; }
  Thread::BasicLockable& accessLogLock() { return access_log_lock_; }

  // Server::HotRestart
  void drainParentListeners() override;
  int duplicateParentListenSocket(const std::string& address) override;
  void getParentStats(GetParentStatsInfo& info) override;
  void initialize(Event::Dispatcher& dispatcher, Server::Instance& server) override;
  void shutdownParentAdmin(ShutdownParentAdminInfo& info) override;
  void terminateParent() override;
  void shutdown() override;
  std::string version() override;

  // RawStatDataAllocator
  Stats::RawStatData* alloc(const std::string& name) override;
  void free(Stats::RawStatData& data) override;

private:
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

  struct RpcBase {
    RpcBase(RpcMessageType type, uint64_t length = sizeof(RpcBase))
        : type_(type), length_(length) {}

    RpcMessageType type_;
    uint64_t length_;
  } __attribute__((packed));

  struct RpcGetListenSocketRequest : public RpcBase {
    RpcGetListenSocketRequest() : RpcBase(RpcMessageType::GetListenSocketRequest, sizeof(*this)) {}

    char address_[256]{0};
  } __attribute__((packed));

  struct RpcGetListenSocketReply : public RpcBase {
    RpcGetListenSocketReply() : RpcBase(RpcMessageType::GetListenSocketReply, sizeof(*this)) {}

    int fd_{0};
  } __attribute__((packed));

  struct RpcShutdownAdminReply : public RpcBase {
    RpcShutdownAdminReply() : RpcBase(RpcMessageType::ShutdownAdminReply, sizeof(*this)) {}

    uint64_t original_start_time_{0};
  } __attribute__((packed));

  struct RpcGetStatsReply : public RpcBase {
    RpcGetStatsReply() : RpcBase(RpcMessageType::GetStatsReply, sizeof(*this)) {}

    uint64_t memory_allocated_{0};
    uint64_t num_connections_{0};
    uint64_t unused_[16]{0};
  } __attribute__((packed));

  template <class rpc_class, RpcMessageType rpc_type> rpc_class* receiveTypedRpc() {
    RpcBase* base_message = receiveRpc(true);
    RELEASE_ASSERT(base_message->length_ == sizeof(rpc_class));
    RELEASE_ASSERT(base_message->type_ == rpc_type);
    return reinterpret_cast<rpc_class*>(base_message);
  }

  int bindDomainSocket(uint64_t id);
  sockaddr_un createDomainSocketAddress(uint64_t id);
  void onGetListenSocket(RpcGetListenSocketRequest& rpc);
  void onSocketEvent();
  RpcBase* receiveRpc(bool block);
  void sendMessage(sockaddr_un& address, RpcBase& rpc);

  Options& options_;
  SharedMemory& shmem_;
  ProcessSharedMutex log_lock_;
  ProcessSharedMutex access_log_lock_;
  ProcessSharedMutex stat_lock_;
  ProcessSharedMutex init_lock_;
  int my_domain_socket_{-1};
  sockaddr_un parent_address_;
  sockaddr_un child_address_;
  Event::FileEventPtr socket_event_;
  std::array<uint8_t, 4096> rpc_buffer_;
  Server::Instance* server_{};
  bool parent_terminated_{};
};

#else  // __linux__

/**
 * No-op implementation of HotRestart for everybody else.
 */
class HotRestartImpl : public HotRestart, public Stats::RawStatDataAllocator {
public:
  HotRestartImpl(Options& options);

  Thread::BasicLockable& logLock() { return log_lock_; }
  Thread::BasicLockable& accessLogLock() { return access_log_lock_; }

  // Server::HotRestart
  void drainParentListeners() override {}
  int duplicateParentListenSocket(const std::string&) override { return -1; }
  void getParentStats(GetParentStatsInfo& info) override { memset(&info, 0, sizeof(info)); }
  void initialize(Event::Dispatcher&, Server::Instance&) override {}
  void shutdownParentAdmin(ShutdownParentAdminInfo&) override {}
  void terminateParent() override {}
  void shutdown() override {}
  std::string version() override { return "disabled"; }

  // RawStatDataAllocator
  Stats::RawStatData* alloc(const std::string& name) override;
  void free(Stats::RawStatData& data) override;

private:
  SharedMemory& shmem_;
  ProcessSharedMutex log_lock_;
  ProcessSharedMutex access_log_lock_;
  ProcessSharedMutex stat_lock_;
};
#endif // __linux__

} // namespace Server
} // namespace Envoy
