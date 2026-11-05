#pragma once

#include <sys/time.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/event/timer.h"

#include "source/common/coroutine/task.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

extern "C" {
// Forward declaration to avoid exposing hiredis headers
struct redisAsyncContext;
}

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

class AsyncRedis {
public:
  static constexpr std::chrono::milliseconds DefaultTimeout{1000};

  static absl::StatusOr<std::unique_ptr<AsyncRedis>>
  open(Event::Dispatcher& dispatcher, const std::string& host, int port,
       std::chrono::milliseconds timeout = DefaultTimeout);

  AsyncRedis(const AsyncRedis&) = delete;
  AsyncRedis& operator=(const AsyncRedis&) = delete;
  ~AsyncRedis();

  Coroutine::Task<absl::Status> connect();
  Coroutine::Task<absl::Status> close();
  Coroutine::Task<absl::Status> ping();
  bool isConnected() const;

  Coroutine::Task<absl::StatusOr<int64_t>> get(absl::string_view key);
  Coroutine::Task<absl::StatusOr<std::vector<int64_t>>> mget(absl::Span<const std::string> keys);
  Coroutine::Task<absl::StatusOr<int64_t>> incrBy(absl::string_view key, int64_t delta = 1);
  Coroutine::Task<absl::Status> expire(absl::string_view key, uint64_t seconds);

private:
  AsyncRedis(redisAsyncContext* context, Event::Dispatcher& dispatcher);

  static void onAddRead(void* privdata);
  static void onDelRead(void* privdata);
  static void onAddWrite(void* privdata);
  static void onDelWrite(void* privdata);
  static void onCleanup(void* privdata);
  static void onScheduleTimer(void* privdata, timeval tv);

  redisAsyncContext* context_;
  Event::Dispatcher& dispatcher_;
  Event::FileEventPtr read_event_;
  Event::FileEventPtr write_event_;
  Event::TimerPtr timer_;
};

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
