#pragma once

#include <memory>
#include <string>

#include "envoy/api/api.h"
#include "envoy/api/os_sys_calls.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

#include "source/common/api/os_sys_calls_impl.h"

#if defined(__linux__)
#include "source/common/api/os_sys_calls_impl_linux.h"
#endif

#include "test/mocks/common.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/test_time.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Api {

class MockApi : public Api {
public:
  MockApi();
  ~MockApi() override;

  // Api::Api
  Event::DispatcherPtr allocateDispatcher(const std::string& name) override;
  Event::DispatcherPtr
  allocateDispatcher(const std::string& name,
                     const Event::ScaledRangeTimerManagerFactory& scaled_timer_factory) override;
  Event::DispatcherPtr allocateDispatcher(const std::string& name,
                                          Buffer::WatermarkFactoryPtr&& watermark_factory) override;
  TimeSource& timeSource() override { return time_system_; }

  MOCK_METHOD(Event::Dispatcher*, allocateDispatcher_, (const std::string&, Event::TimeSystem&));
  MOCK_METHOD(Event::Dispatcher*, allocateDispatcher_,
              (const std::string&,
               const Event::ScaledRangeTimerManagerFactory& scaled_timer_factory,
               Buffer::WatermarkFactoryPtr&& watermark_factory, Event::TimeSystem&));
  MOCK_METHOD(Filesystem::Instance&, fileSystem, ());
  MOCK_METHOD(Thread::ThreadFactory&, threadFactory, ());
  MOCK_METHOD(Stats::Scope&, rootScope, ());
  MOCK_METHOD(Random::RandomGenerator&, randomGenerator, ());
  MOCK_METHOD(const envoy::config::bootstrap::v3::Bootstrap&, bootstrap, (), (const));
  MOCK_METHOD(ProcessContextOptRef, processContext, ());
  MOCK_METHOD(Stats::CustomStatNamespaces&, customStatNamespaces, ());

  testing::NiceMock<Filesystem::MockInstance> file_system_;
  Event::GlobalTimeSystem time_system_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  testing::NiceMock<Random::MockRandomGenerator> random_;
  envoy::config::bootstrap::v3::Bootstrap empty_bootstrap_;
};

class MockOsSysCalls : public OsSysCallsImpl {
public:
  MockOsSysCalls();
  ~MockOsSysCalls() override;

  // Api::OsSysCalls
  SysCallIntResult setsockopt(os_fd_t sockfd, int level, int optname, const void* optval,
                              socklen_t optlen) override;
  SysCallIntResult getsockopt(os_fd_t sockfd, int level, int optname, void* optval,
                              socklen_t* optlen) override;

  MOCK_METHOD(SysCallSocketResult, accept, (os_fd_t sockfd, sockaddr* addr, socklen_t* addrlen));
  MOCK_METHOD(SysCallIntResult, bind, (os_fd_t sockfd, const sockaddr* addr, socklen_t addrlen));
  MOCK_METHOD(SysCallIntResult, ioctl, (os_fd_t sockfd, unsigned long int request, void* argp));
  MOCK_METHOD(SysCallIntResult, ioctl,
              (os_fd_t sockfd, unsigned long control_code, void* in_buffer,
               unsigned long in_buffer_len, void* out_buffer, unsigned long out_buffer_len,
               unsigned long* bytes_returned));
  MOCK_METHOD(SysCallIntResult, close, (os_fd_t));
  MOCK_METHOD(SysCallSizeResult, writev, (os_fd_t, const iovec*, int));
  MOCK_METHOD(SysCallSizeResult, sendmsg, (os_fd_t fd, const msghdr* msg, int flags));
  MOCK_METHOD(SysCallSizeResult, readv, (os_fd_t, const iovec*, int));
  MOCK_METHOD(SysCallSizeResult, pwrite,
              (os_fd_t fd, const void* buffer, size_t length, off_t offset), (const));
  MOCK_METHOD(SysCallSizeResult, pread, (os_fd_t fd, void* buffer, size_t length, off_t offset),
              (const));
  MOCK_METHOD(SysCallSizeResult, send, (os_fd_t socket, void* buffer, size_t length, int flags));
  MOCK_METHOD(SysCallSizeResult, recv, (os_fd_t socket, void* buffer, size_t length, int flags));
  MOCK_METHOD(SysCallSizeResult, recvmsg, (os_fd_t socket, msghdr* msg, int flags));
  MOCK_METHOD(SysCallIntResult, recvmmsg,
              (os_fd_t socket, struct mmsghdr* msgvec, unsigned int vlen, int flags,
               struct timespec* timeout));
  MOCK_METHOD(SysCallIntResult, ftruncate, (int fd, off_t length));
  MOCK_METHOD(SysCallPtrResult, mmap,
              (void* addr, size_t length, int prot, int flags, int fd, off_t offset));
  MOCK_METHOD(SysCallIntResult, stat, (const char* name, struct stat* stat));
  MOCK_METHOD(SysCallIntResult, fstat, (os_fd_t fd, struct stat* stat));
  MOCK_METHOD(SysCallIntResult, chmod, (const std::string& name, mode_t mode));
  MOCK_METHOD(int, setsockopt_,
              (os_fd_t sockfd, int level, int optname, const void* optval, socklen_t optlen));
  MOCK_METHOD(int, getsockopt_,
              (os_fd_t sockfd, int level, int optname, void* optval, socklen_t* optlen));
  MOCK_METHOD(SysCallSocketResult, socket, (int domain, int type, int protocol));
  MOCK_METHOD(SysCallIntResult, gethostname, (char* name, size_t length));
  MOCK_METHOD(SysCallIntResult, getsockname, (os_fd_t sockfd, sockaddr* name, socklen_t* namelen));
  MOCK_METHOD(SysCallIntResult, getpeername, (os_fd_t sockfd, sockaddr* name, socklen_t* namelen));
  MOCK_METHOD(SysCallIntResult, setsocketblocking, (os_fd_t sockfd, bool block));
  MOCK_METHOD(SysCallIntResult, connect, (os_fd_t sockfd, const sockaddr* addr, socklen_t addrlen));
  MOCK_METHOD(SysCallIntResult, open, (const char* pathname, int flags), (const));
  MOCK_METHOD(SysCallIntResult, open, (const char* pathname, int flags, mode_t mode), (const));
  MOCK_METHOD(SysCallIntResult, unlink, (const char* pathname), (const));
  MOCK_METHOD(SysCallIntResult, linkat,
              (os_fd_t olddirfd, const char* oldpath, os_fd_t newdirfd, const char* newpath,
               int flags),
              (const));
  MOCK_METHOD(SysCallIntResult, mkstemp, (char* tmplate), (const));
  MOCK_METHOD(bool, supportsAllPosixFileOperations, (), (const));
  MOCK_METHOD(SysCallIntResult, shutdown, (os_fd_t sockfd, int how));
  MOCK_METHOD(SysCallIntResult, socketpair, (int domain, int type, int protocol, os_fd_t sv[2]));
  MOCK_METHOD(SysCallIntResult, listen, (os_fd_t sockfd, int backlog));
  MOCK_METHOD(SysCallSocketResult, duplicate, (os_fd_t sockfd));
  MOCK_METHOD(SysCallSizeResult, write, (os_fd_t sockfd, const void* buffer, size_t length));
  MOCK_METHOD(SysCallBoolResult, socketTcpInfo, (os_fd_t sockfd, EnvoyTcpInfo* tcp_info));
  MOCK_METHOD(bool, supportsMmsg, (), (const));
  MOCK_METHOD(bool, supportsUdpGro, (), (const));
  MOCK_METHOD(bool, supportsIpTransparent, (Network::Address::IpVersion version), (const));
  MOCK_METHOD(bool, supportsMptcp, (), (const));
  MOCK_METHOD(bool, supportsGetifaddrs, (), (const));
  MOCK_METHOD(SysCallIntResult, getifaddrs, (InterfaceAddressVector & interfaces));
  MOCK_METHOD(SysCallIntResult, getaddrinfo,
              (const char* node, const char* service, const addrinfo* hints, addrinfo** res));
  MOCK_METHOD(void, freeaddrinfo, (addrinfo * res));

  // Map from (sockfd,level,optname) to boolean socket option.
  using SockOptKey = std::tuple<os_fd_t, int, int>;
  std::map<SockOptKey, bool> boolsockopts_;
};

#if defined(__linux__)
class MockLinuxOsSysCalls : public LinuxOsSysCallsImpl {
public:
  // Api::LinuxOsSysCalls
  MOCK_METHOD(SysCallIntResult, sched_getaffinity, (pid_t pid, size_t cpusetsize, cpu_set_t* mask));
};
#endif

} // namespace Api
} // namespace Envoy
