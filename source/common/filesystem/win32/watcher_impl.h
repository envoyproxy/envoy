#pragma once

#include <concurrent_queue.h>

#include <codecvt>
#include <cstdint>
#include <list>
#include <locale>
#include <string>
#include <unordered_map>

#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/watcher.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/fmt.h"
#include "common/common/logger.h"
#include "common/common/thread_impl.h"
#include "common/filesystem/filesystem_impl.h"

namespace Envoy {
namespace Filesystem {

class WatcherImpl : public Watcher, Logger::Loggable<Logger::Id::file> {
public:
  WatcherImpl(Event::Dispatcher& dispatcher);
  ~WatcherImpl();

  // Filesystem::Watcher
  void addWatch(const std::string& path, uint32_t events, OnChangedCb cb) override;

private:
  static void issueFirstRead(ULONG_PTR param);
  static void directoryChangeCompletion(DWORD err, DWORD num_bytes, LPOVERLAPPED overlapped);
  static void endDirectoryWatch(SOCKET_FD sock, HANDLE hEvent);
  void watchLoop();
  void onDirectoryEvent();

  struct FileWatch {
    // store the wide character string for ReadDirectoryChangesW
    std::wstring file_;
    uint32_t events_;
    OnChangedCb cb_;
  };

  typedef std::function<void(void)> CbClosure;

  struct DirectoryWatch {
    OVERLAPPED op_;
    std::list<FileWatch> watches_;
    HANDLE hDir_;
    std::vector<uint8_t> buffer_;
    WatcherImpl* watcher_;
  };

  typedef std::unique_ptr<DirectoryWatch> DirectoryWatchPtr;

  std::unordered_map<std::string, DirectoryWatchPtr> callback_map_;
  Event::FileEventPtr directory_event_;
  SOCKET_FD event_write_;
  SOCKET_FD event_read_;
  Thread::ThreadPtr watch_thread_;
  Thread::ThreadFactoryImplWin32 thread_factory_;
  HANDLE thread_exit_event_;
  std::vector<HANDLE> dir_watch_complete_events_;
  std::atomic<bool> keep_watching_;
  concurrency::concurrent_queue<CbClosure> active_callbacks_;
  Api::OsSysCallsImpl& os_sys_calls_;
  InstanceImplWin32 file_system_;
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> cvt_;
};

} // namespace Filesystem
} // namespace Envoy
