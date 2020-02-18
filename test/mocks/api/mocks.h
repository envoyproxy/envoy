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

  MOCK_METHOD(Event::Dispatcher*, allocateDispatcher_, (Event::TimeSystem&));
  MOCK_METHOD(Event::Dispatcher*, allocateDispatcher_,
              (Buffer::WatermarkFactoryPtr && watermark_factory, Event::TimeSystem&));
  MOCK_METHOD(Filesystem::Instance&, fileSystem, ());
  MOCK_METHOD(Thread::ThreadFactory&, threadFactory, ());
  MOCK_METHOD(const Stats::Scope&, rootScope, ());
  MOCK_METHOD(ProcessContextOptRef, processContext, ());

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

  MOCK_METHOD(SysCallIntResult, bind, (int sockfd, const sockaddr* addr, socklen_t addrlen));
  MOCK_METHOD(SysCallIntResult, ioctl, (int sockfd, unsigned long int request, void* argp));
  MOCK_METHOD(SysCallIntResult, close, (int));
  MOCK_METHOD(SysCallSizeResult, writev, (int, const iovec*, int));
  MOCK_METHOD(SysCallSizeResult, sendmsg, (int fd, const msghdr* message, int flags));
  MOCK_METHOD(SysCallSizeResult, readv, (int, const iovec*, int));
  MOCK_METHOD(SysCallSizeResult, recv, (int socket, void* buffer, size_t length, int flags));
  MOCK_METHOD(SysCallSizeResult, recvmsg, (int socket, struct msghdr* msg, int flags));
  MOCK_METHOD(SysCallIntResult, ftruncate, (int fd, off_t length));
  MOCK_METHOD(SysCallPtrResult, mmap,
              (void* addr, size_t length, int prot, int flags, int fd, off_t offset));
  MOCK_METHOD(SysCallIntResult, stat, (const char* name, struct stat* stat));
  MOCK_METHOD(SysCallIntResult, chmod, (const std::string& name, mode_t mode));
  MOCK_METHOD(int, setsockopt_,
              (int sockfd, int level, int optname, const void* optval, socklen_t optlen));
  MOCK_METHOD(int, getsockopt_,
              (int sockfd, int level, int optname, void* optval, socklen_t* optlen));
  MOCK_METHOD(SysCallIntResult, socket, (int domain, int type, int protocol));
  MOCK_METHOD(SysCallIntResult, gethostname, (char* name, size_t length));

  // Map from (sockfd,level,optname) to boolean socket option.
  using SockOptKey = std::tuple<int, int, int>;
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
