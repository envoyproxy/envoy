#pragma once

#include <memory>
#include <string>

#include "envoy/api/api.h"
#include "envoy/api/os_sys_calls.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

#include "common/api/os_sys_calls_impl.h"

#if defined(__linux__)
#include "common/api/os_sys_calls_impl_linux.h"
#endif

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
  Event::DispatcherPtr allocateDispatcher() override;
  Event::DispatcherPtr allocateDispatcher(Buffer::WatermarkFactoryPtr&& watermark_factory) override;
  TimeSource& timeSource() override { return time_system_; }

  MOCK_METHOD1(allocateDispatcher_, Event::Dispatcher*(Event::TimeSystem&));
  MOCK_METHOD2(allocateDispatcher_,
               Event::Dispatcher*(Buffer::WatermarkFactoryPtr&& watermark_factory,
                                  Event::TimeSystem&));
  MOCK_METHOD0(fileSystem, Filesystem::Instance&());
  MOCK_METHOD0(threadFactory, Thread::ThreadFactory&());
  MOCK_METHOD0(rootScope, const Stats::Scope&());

  testing::NiceMock<Filesystem::MockInstance> file_system_;
  Event::GlobalTimeSystem time_system_;
  testing::NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
};

class MockOsSysCalls : public OsSysCallsImpl {
public:
  MockOsSysCalls();
  ~MockOsSysCalls() override;

  // Api::OsSysCalls
  SysCallIntResult setsockopt(int sockfd, int level, int optname, const void* optval,
                              socklen_t optlen) override;
  SysCallIntResult getsockopt(int sockfd, int level, int optname, void* optval,
                              socklen_t* optlen) override;

  MOCK_METHOD3(bind, SysCallIntResult(int sockfd, const sockaddr* addr, socklen_t addrlen));
  MOCK_METHOD3(ioctl, SysCallIntResult(int sockfd, unsigned long int request, void* argp));
  MOCK_METHOD1(close, SysCallIntResult(int));
  MOCK_METHOD3(writev, SysCallSizeResult(int, const iovec*, int));
  MOCK_METHOD3(sendmsg, SysCallSizeResult(int fd, const msghdr* message, int flags));
  MOCK_METHOD3(readv, SysCallSizeResult(int, const iovec*, int));
  MOCK_METHOD4(recv, SysCallSizeResult(int socket, void* buffer, size_t length, int flags));
  MOCK_METHOD6(recvfrom, SysCallSizeResult(int sockfd, void* buffer, size_t length, int flags,
                                           struct sockaddr* addr, socklen_t* addrlen));
  MOCK_METHOD3(recvmsg, SysCallSizeResult(int socket, struct msghdr* msg, int flags));
  MOCK_METHOD2(ftruncate, SysCallIntResult(int fd, off_t length));
  MOCK_METHOD6(mmap, SysCallPtrResult(void* addr, size_t length, int prot, int flags, int fd,
                                      off_t offset));
  MOCK_METHOD2(stat, SysCallIntResult(const char* name, struct stat* stat));
  MOCK_METHOD5(setsockopt_,
               int(int sockfd, int level, int optname, const void* optval, socklen_t optlen));
  MOCK_METHOD5(getsockopt_,
               int(int sockfd, int level, int optname, void* optval, socklen_t* optlen));
  MOCK_METHOD3(socket, SysCallIntResult(int domain, int type, int protocol));

  // Map from (sockfd,level,optname) to boolean socket option.
  using SockOptKey = std::tuple<int, int, int>;
  std::map<SockOptKey, bool> boolsockopts_;
};

#if defined(__linux__)
class MockLinuxOsSysCalls : public LinuxOsSysCallsImpl {
public:
  // Api::LinuxOsSysCalls
  MOCK_METHOD3(sched_getaffinity, SysCallIntResult(pid_t pid, size_t cpusetsize, cpu_set_t* mask));
};
#endif

} // namespace Api
} // namespace Envoy
