#include "server/hot_restart_impl.h"

#include <signal.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/un.h>

#include <cstdint>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/server/instance.h"
#include "envoy/server/options.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/fmt.h"
#include "common/common/lock_guard.h"
#include "common/common/utility.h"
#include "common/network/utility.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

// Increment this whenever there is a shared memory / RPC change that will prevent a hot restart
// from working. Operations code can then cope with this and do a full restart.
const uint64_t SharedMemory::VERSION = 9;

static BlockMemoryHashSetOptions blockMemHashOptions(uint64_t max_stats) {
  BlockMemoryHashSetOptions hash_set_options;
  hash_set_options.capacity = max_stats;

  // https://stackoverflow.com/questions/3980117/hash-table-why-size-should-be-prime
  hash_set_options.num_slots = Primes::findPrimeLargerThan(hash_set_options.capacity / 2);
  return hash_set_options;
}

SharedMemory& SharedMemory::initialize(uint64_t stats_set_size, Options& options) {
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();

  const uint64_t entry_size = Stats::RawStatData::size();
  const uint64_t total_size = sizeof(SharedMemory) + stats_set_size;

  int flags = O_RDWR;
  const std::string shmem_name = fmt::format("/envoy_shared_memory_{}", options.baseId());
  if (options.restartEpoch() == 0) {
    flags |= O_CREAT | O_EXCL;

    // If we are meant to be first, attempt to unlink a previous shared memory instance. If this
    // is a clean restart this should then allow the shm_open() call below to succeed.
    os_sys_calls.shmUnlink(shmem_name.c_str());
  }

  int shmem_fd = os_sys_calls.shmOpen(shmem_name.c_str(), flags, S_IRUSR | S_IWUSR);
  if (shmem_fd == -1) {
    PANIC(fmt::format("cannot open shared memory region {} check user permissions", shmem_name));
  }

  if (options.restartEpoch() == 0) {
    int rc = os_sys_calls.ftruncate(shmem_fd, total_size);
    RELEASE_ASSERT(rc != -1);
  }

  SharedMemory* shmem = reinterpret_cast<SharedMemory*>(
      os_sys_calls.mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shmem_fd, 0));
  RELEASE_ASSERT(shmem != MAP_FAILED);
  RELEASE_ASSERT((reinterpret_cast<uintptr_t>(shmem) % alignof(decltype(shmem))) == 0);

  if (options.restartEpoch() == 0) {
    shmem->size_ = total_size;
    shmem->version_ = VERSION;
    shmem->max_stats_ = options.maxStats();
    shmem->entry_size_ = entry_size;
    shmem->initializeMutex(shmem->log_lock_);
    shmem->initializeMutex(shmem->access_log_lock_);
    shmem->initializeMutex(shmem->stat_lock_);
    shmem->initializeMutex(shmem->init_lock_);
  } else {
    RELEASE_ASSERT(shmem->size_ == total_size);
    RELEASE_ASSERT(shmem->version_ == VERSION);
    RELEASE_ASSERT(shmem->max_stats_ == options.maxStats());
    RELEASE_ASSERT(shmem->entry_size_ == entry_size);
  }

  // Stats::RawStatData must be naturally aligned for atomics to work properly.
  RELEASE_ASSERT((reinterpret_cast<uintptr_t>(shmem->stats_set_data_) % alignof(RawStatDataSet)) ==
                 0);

  // Here we catch the case where a new Envoy starts up when the current Envoy has not yet fully
  // initialized. The startup logic is quite complicated, and it's not worth trying to handle this
  // in a finer way. This will cause the startup to fail with an error code early, without
  // affecting any currently running processes. The process runner should try again later with some
  // back off and with the same hot restart epoch number.
  uint64_t old_flags = shmem->flags_.fetch_or(Flags::INITIALIZING);
  if (old_flags & Flags::INITIALIZING) {
    throw EnvoyException("previous envoy process is still initializing");
  }

  return *shmem;
}

void SharedMemory::initializeMutex(pthread_mutex_t& mutex) {
  pthread_mutexattr_t attribute;
  pthread_mutexattr_init(&attribute);
  pthread_mutexattr_setpshared(&attribute, PTHREAD_PROCESS_SHARED);
  pthread_mutexattr_setrobust(&attribute, PTHREAD_MUTEX_ROBUST);
  pthread_mutex_init(&mutex, &attribute);
}

std::string SharedMemory::version(uint64_t max_num_stats, uint64_t max_stat_name_len) {
  return fmt::format("{}.{}.{}.{}", VERSION, sizeof(SharedMemory), max_num_stats,
                     max_stat_name_len);
}

HotRestartImpl::HotRestartImpl(Options& options)
    : options_(options), stats_set_options_(blockMemHashOptions(options.maxStats())),
      shmem_(SharedMemory::initialize(RawStatDataSet::numBytes(stats_set_options_), options)),
      log_lock_(shmem_.log_lock_), access_log_lock_(shmem_.access_log_lock_),
      stat_lock_(shmem_.stat_lock_), init_lock_(shmem_.init_lock_) {
  {
    // We must hold the stat lock when attaching to an existing memory segment
    // because it might be actively written to while we sanityCheck it.
    Thread::LockGuard lock(stat_lock_);
    stats_set_.reset(new RawStatDataSet(stats_set_options_, options.restartEpoch() == 0,
                                        shmem_.stats_set_data_));
  }
  my_domain_socket_ = bindDomainSocket(options.restartEpoch());
  child_address_ = createDomainSocketAddress((options.restartEpoch() + 1));
  initDomainSocketAddress(&parent_address_);
  if (options.restartEpoch() != 0) {
    parent_address_ = createDomainSocketAddress((options.restartEpoch() + -1));
  }

  // If our parent ever goes away just terminate us so that we don't have to rely on ops/launching
  // logic killing the entire process tree. We should never exist without our parent.
  int rc = prctl(PR_SET_PDEATHSIG, SIGTERM);
  RELEASE_ASSERT(rc != -1);
}

Stats::RawStatData* HotRestartImpl::alloc(const std::string& name) {
  // Try to find the existing slot in shared memory, otherwise allocate a new one.
  Thread::LockGuard lock(stat_lock_);
  absl::string_view key = name;
  if (key.size() > Stats::RawStatData::maxNameLength()) {
    key.remove_suffix(key.size() - Stats::RawStatData::maxNameLength());
  }
  auto value_created = stats_set_->insert(key);
  Stats::RawStatData* data = value_created.first;
  if (data == nullptr) {
    return nullptr;
  }
  // For new entries (value-created.second==true), BlockMemoryHashSet calls Value::initialize()
  // automatically, but on recycled entries (value-created.second==false) we need to bump the
  // ref-count.
  if (!value_created.second) {
    ++data->ref_count_;
  }
  return data;
}

void HotRestartImpl::free(Stats::RawStatData& data) {
  // We must hold the lock since the reference decrement can race with an initialize above.
  Thread::LockGuard lock(stat_lock_);
  ASSERT(data.ref_count_ > 0);
  if (--data.ref_count_ > 0) {
    return;
  }
  bool key_removed = stats_set_->remove(data.key());
  ASSERT(key_removed);
  memset(static_cast<void*>(&data), 0, Stats::RawStatData::size());
}

int HotRestartImpl::bindDomainSocket(uint64_t id) {
  Api::OsSysCalls& os_sys_calls = Api::OsSysCallsSingleton::get();
  // This actually creates the socket and binds it. We use the socket in datagram mode so we can
  // easily read single messages.
  int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  sockaddr_un address = createDomainSocketAddress(id);
  int rc = os_sys_calls.bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  if (rc != 0) {
    throw EnvoyException(
        fmt::format("unable to bind domain socket with id={} (see --base-id option)", id));
  }

  return fd;
}

void HotRestartImpl::initDomainSocketAddress(sockaddr_un* address) {
  memset(address, 0, sizeof(*address));
  address->sun_family = AF_UNIX;
}

sockaddr_un HotRestartImpl::createDomainSocketAddress(uint64_t id) {
  // Right now we only allow a maximum of 3 concurrent envoy processes to be running. When the third
  // starts up it will kill the oldest parent.
  const uint64_t MAX_CONCURRENT_PROCESSES = 3;
  id = id % MAX_CONCURRENT_PROCESSES;

  // This creates an anonymous domain socket name (where the first byte of the name of \0).
  sockaddr_un address;
  initDomainSocketAddress(&address);
  StringUtil::strlcpy(&address.sun_path[1],
                      fmt::format("envoy_domain_socket_{}", options_.baseId() + id).c_str(),
                      sizeof(address.sun_path) - 1);
  address.sun_path[0] = 0;
  return address;
}

void HotRestartImpl::drainParentListeners() {
  if (options_.restartEpoch() > 0) {
    // No reply expected.
    RpcBase rpc(RpcMessageType::DrainListenersRequest);
    sendMessage(parent_address_, rpc);
  }

  // At this point we are initialized and a new Envoy can startup if needed.
  shmem_.flags_ &= ~SharedMemory::Flags::INITIALIZING;
}

int HotRestartImpl::duplicateParentListenSocket(const std::string& address) {
  if (options_.restartEpoch() == 0 || parent_terminated_) {
    return -1;
  }

  RpcGetListenSocketRequest rpc;
  ASSERT(address.length() < sizeof(rpc.address_));
  StringUtil::strlcpy(rpc.address_, address.c_str(), sizeof(rpc.address_));
  sendMessage(parent_address_, rpc);
  RpcGetListenSocketReply* reply =
      receiveTypedRpc<RpcGetListenSocketReply, RpcMessageType::GetListenSocketReply>();
  return reply->fd_;
}

void HotRestartImpl::getParentStats(GetParentStatsInfo& info) {
  // There exists a race condition during hot restart involving fetching parent stats. It looks like
  // this:
  // 1) There currently exist 2 Envoy processes (draining has not completed): P0 and P1.
  // 2) New process (P2) comes up and passes the INITIALIZING check.
  // 3) P2 proceeds to the parent admin shutdown phase.
  // 4) This races with P1 fetching parent stats from P0.
  // 5) Calling receiveTypedRpc() below picks up the wrong message.
  //
  // There are not any great solutions to this problem. We could potentially guard this using flags,
  // but this is a legitimate race condition even under normal restart conditions, so exiting P2
  // with an error is not great. We could also rework all of this code so that P0<->P1 and P1<->P2
  // communication occur over different socket pairs. This could work, but is a large change. We
  // could also potentially use connection oriented sockets and accept connections from our child,
  // and connect to our parent, but again, this becomes complicated.
  //
  // Instead, we guard this condition with a lock. However, to avoid deadlock, we must tryLock()
  // in this path, since this call runs in the same thread as the event loop that is receiving
  // messages. If tryLock() fails it is sufficient to not return any parent stats.
  Thread::TryLockGuard lock(init_lock_);
  memset(&info, 0, sizeof(info));
  if (options_.restartEpoch() == 0 || parent_terminated_ || !lock.tryLock()) {
    return;
  }

  RpcBase rpc(RpcMessageType::GetStatsRequest);
  sendMessage(parent_address_, rpc);
  RpcGetStatsReply* reply = receiveTypedRpc<RpcGetStatsReply, RpcMessageType::GetStatsReply>();
  info.memory_allocated_ = reply->memory_allocated_;
  info.num_connections_ = reply->num_connections_;
}

void HotRestartImpl::initialize(Event::Dispatcher& dispatcher, Server::Instance& server) {
  socket_event_ =
      dispatcher.createFileEvent(my_domain_socket_,
                                 [this](uint32_t events) -> void {
                                   ASSERT(events == Event::FileReadyType::Read);
                                   onSocketEvent();
                                 },
                                 Event::FileTriggerType::Edge, Event::FileReadyType::Read);
  server_ = &server;
}

HotRestartImpl::RpcBase* HotRestartImpl::receiveRpc(bool block) {
  // By default the domain socket is non blocking. If we need to block, make it blocking first.
  if (block) {
    int rc = fcntl(my_domain_socket_, F_SETFL, 0);
    RELEASE_ASSERT(rc != -1);
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

  RELEASE_ASSERT(rc != -1);
  RELEASE_ASSERT(message.msg_flags == 0);

  // Turn non-blocking back on if we made it blocking.
  if (block) {
    int rc = fcntl(my_domain_socket_, F_SETFL, O_NONBLOCK);
    RELEASE_ASSERT(rc != -1);
  }

  RpcBase* rpc = reinterpret_cast<RpcBase*>(&rpc_buffer_[0]);
  RELEASE_ASSERT(static_cast<uint64_t>(rc) == rpc->length_);

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
      RELEASE_ASSERT(false);
    }
  }

  return rpc;
}

void HotRestartImpl::sendMessage(sockaddr_un& address, RpcBase& rpc) {
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
  RELEASE_ASSERT(rc != -1);
}

void HotRestartImpl::onGetListenSocket(RpcGetListenSocketRequest& rpc) {
  RpcGetListenSocketReply reply;
  reply.fd_ = -1;

  Network::Address::InstanceConstSharedPtr addr =
      Network::Utility::resolveUrl(std::string(rpc.address_));
  for (const auto& listener : server_->listenerManager().listeners()) {
    if (*listener.get().socket().localAddress() == *addr) {
      reply.fd_ = listener.get().socket().fd();
      break;
    }
  }

  if (reply.fd_ == -1) {
    // In this case there is no fd to duplicate so we just send a normal message.
    sendMessage(child_address_, reply);
  } else {
    iovec iov[1];
    iov[0].iov_base = &reply;
    iov[0].iov_len = reply.length_;

    uint8_t control_buffer[CMSG_SPACE(sizeof(int))];
    memset(control_buffer, 0, CMSG_SPACE(sizeof(int)));

    msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_name = &child_address_;
    message.msg_namelen = sizeof(child_address_);
    message.msg_iov = iov;
    message.msg_iovlen = 1;
    message.msg_control = control_buffer;
    message.msg_controllen = CMSG_SPACE(sizeof(int));

    cmsghdr* control_message = CMSG_FIRSTHDR(&message);
    control_message->cmsg_level = SOL_SOCKET;
    control_message->cmsg_type = SCM_RIGHTS;
    control_message->cmsg_len = CMSG_LEN(sizeof(int));
    *reinterpret_cast<int*>(CMSG_DATA(control_message)) = reply.fd_;

    int rc = sendmsg(my_domain_socket_, &message, 0);
    RELEASE_ASSERT(rc != -1);
  }
}

void HotRestartImpl::onSocketEvent() {
  while (true) {
    RpcBase* base_message = receiveRpc(false);
    if (!base_message) {
      return;
    }

    switch (base_message->type_) {
    case RpcMessageType::ShutdownAdminRequest: {
      server_->shutdownAdmin();
      RpcShutdownAdminReply rpc;
      rpc.original_start_time_ = server_->startTimeFirstEpoch();
      sendMessage(child_address_, rpc);
      break;
    }

    case RpcMessageType::GetListenSocketRequest: {
      RpcGetListenSocketRequest* message =
          reinterpret_cast<RpcGetListenSocketRequest*>(base_message);
      onGetListenSocket(*message);
      break;
    }

    case RpcMessageType::GetStatsRequest: {
      GetParentStatsInfo info;
      server_->getParentStats(info);
      RpcGetStatsReply rpc;
      rpc.memory_allocated_ = info.memory_allocated_;
      rpc.num_connections_ = info.num_connections_;
      sendMessage(child_address_, rpc);
      break;
    }

    case RpcMessageType::DrainListenersRequest: {
      server_->drainListeners();
      break;
    }

    case RpcMessageType::TerminateRequest: {
      ENVOY_LOG(info, "shutting down due to child request");
      kill(getpid(), SIGTERM);
      break;
    }

    default: {
      RpcBase rpc(RpcMessageType::UnknownRequestReply);
      sendMessage(child_address_, rpc);
      break;
    }
    }
  }
}

void HotRestartImpl::shutdownParentAdmin(ShutdownParentAdminInfo& info) {
  // See large comment in getParentStats() on why this operation is locked.
  Thread::LockGuard lock(init_lock_);
  if (options_.restartEpoch() == 0) {
    return;
  }

  RpcBase rpc(RpcMessageType::ShutdownAdminRequest);
  sendMessage(parent_address_, rpc);
  RpcShutdownAdminReply* reply =
      receiveTypedRpc<RpcShutdownAdminReply, RpcMessageType::ShutdownAdminReply>();
  info.original_start_time_ = reply->original_start_time_;
}

void HotRestartImpl::terminateParent() {
  if (options_.restartEpoch() == 0 || parent_terminated_) {
    return;
  }

  RpcBase rpc(RpcMessageType::TerminateRequest);
  sendMessage(parent_address_, rpc);
  parent_terminated_ = true;
}

void HotRestartImpl::shutdown() { socket_event_.reset(); }

std::string HotRestartImpl::version() {
  Thread::LockGuard lock(stat_lock_);
  return versionHelper(shmem_.maxStats(), Stats::RawStatData::maxNameLength(), *stats_set_);
}

// Called from envoy --hot-restart-version -- needs to instantiate a RawStatDataSet so it
// can generate the version string.
std::string HotRestartImpl::hotRestartVersion(uint64_t max_num_stats, uint64_t max_stat_name_len) {
  const BlockMemoryHashSetOptions options = blockMemHashOptions(max_num_stats);
  const uint64_t bytes = RawStatDataSet::numBytes(options);
  std::unique_ptr<uint8_t[]> mem_buffer_for_dry_run_(new uint8_t[bytes]);
  RawStatDataSet stats_set(options, true /* init */, mem_buffer_for_dry_run_.get());

  return versionHelper(max_num_stats, max_stat_name_len, stats_set);
}

std::string HotRestartImpl::versionHelper(uint64_t max_num_stats, uint64_t max_stat_name_len,
                                          RawStatDataSet& stats_set) {
  return SharedMemory::version(max_num_stats, max_stat_name_len) + "." + stats_set.version();
}

} // namespace Server
} // namespace Envoy
