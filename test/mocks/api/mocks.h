#pragma once

#include <memory>
#include <string>

#include "envoy/api/api.h"
#include "envoy/api/os_sys_calls.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/stats/store.h"

#include "common/api/os_sys_calls_impl.h"

#include "test/mocks/filesystem/mocks.h"
#include "test/test_common/test_time_system.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Api {

class MockApi : public Api {
public:
  MockApi();
  ~MockApi();

  // Api::Api
  Event::DispatcherPtr allocateDispatcher(Event::TimeSystem& time_system) override {
    return Event::DispatcherPtr{allocateDispatcher_(time_system)};
  }

  MOCK_METHOD1(allocateDispatcher_, Event::Dispatcher*(Event::TimeSystem&));
  MOCK_METHOD3(createFile,
               Filesystem::FileSharedPtr(const std::string& path, Event::Dispatcher& dispatcher,
                                         Thread::BasicLockable& lock));
  MOCK_METHOD1(fileExists, bool(const std::string& path));
  MOCK_METHOD1(fileReadToEnd, std::string(const std::string& path));
  MOCK_METHOD1(createThread, Thread::ThreadPtr(std::function<void()> thread_routine));

  std::shared_ptr<Filesystem::MockFile> file_{new Filesystem::MockFile()};
};

class MockOsSysCalls : public OsSysCallsImpl {
public:
  MockOsSysCalls();
  ~MockOsSysCalls();

  // Api::OsSysCalls
  SysCallSizeResult write(int fd, const void* buffer, size_t num_bytes) override;
  SysCallIntResult open(const std::string& full_path, int flags, int mode) override;
  SysCallIntResult setsockopt(int sockfd, int level, int optname, const void* optval,
                              socklen_t optlen) override;
  SysCallIntResult getsockopt(int sockfd, int level, int optname, void* optval,
                              socklen_t* optlen) override;

  MOCK_METHOD3(bind, SysCallIntResult(int sockfd, const sockaddr* addr, socklen_t addrlen));
  MOCK_METHOD3(ioctl, SysCallIntResult(int sockfd, unsigned long int request, void* argp));
  MOCK_METHOD1(close, SysCallIntResult(int));
  MOCK_METHOD3(open_, int(const std::string& full_path, int flags, int mode));
  MOCK_METHOD3(write_, ssize_t(int, const void*, size_t));
  MOCK_METHOD3(writev, SysCallSizeResult(int, const iovec*, int));
  MOCK_METHOD3(readv, SysCallSizeResult(int, const iovec*, int));
  MOCK_METHOD4(recv, SysCallSizeResult(int socket, void* buffer, size_t length, int flags));

  MOCK_METHOD3(shmOpen, SysCallIntResult(const char*, int, mode_t));
  MOCK_METHOD1(shmUnlink, SysCallIntResult(const char*));
  MOCK_METHOD2(ftruncate, SysCallIntResult(int fd, off_t length));
  MOCK_METHOD6(mmap, SysCallPtrResult(void* addr, size_t length, int prot, int flags, int fd,
                                      off_t offset));
  MOCK_METHOD2(stat, SysCallIntResult(const char* name, struct stat* stat));
  MOCK_METHOD5(setsockopt_,
               int(int sockfd, int level, int optname, const void* optval, socklen_t optlen));
  MOCK_METHOD5(getsockopt_,
               int(int sockfd, int level, int optname, void* optval, socklen_t* optlen));
  MOCK_METHOD3(socket, SysCallIntResult(int domain, int type, int protocol));

  size_t num_writes_;
  size_t num_open_;
  Thread::MutexBasicLockable write_mutex_;
  Thread::MutexBasicLockable open_mutex_;
  Thread::CondVar write_event_;
  Thread::CondVar open_event_;
  // Map from (sockfd,level,optname) to boolean socket option.
  using SockOptKey = std::tuple<int, int, int>;
  std::map<SockOptKey, bool> boolsockopts_;
};

} // namespace Api
} // namespace Envoy
