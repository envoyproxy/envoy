#include "examples/cc/fetch_client/fetch_client.h"

#include <iostream>

#include "source/common/api/api_impl.h"
#include "source/common/common/thread.h"
#include "source/common/http/utility.h"
#include "source/common/stats/allocator_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/exe/platform_impl.h"

#include "library/cc/engine_builder.h"
#include "library/cc/stream.h"
#include "library/common/data/utility.h"
#include "library/common/http/header_utility.h"
#include "library/common/types/c_types.h"

namespace Envoy {

Fetch::Fetch()
    : logging_context_(spdlog::level::level_enum::info, Envoy::Logger::Logger::DEFAULT_LOG_FORMAT,
                       lock_, false),
      stats_allocator_(symbol_table_), store_root_(stats_allocator_),
      api_(std::make_unique<Envoy::Api::Impl>(platform_impl_.threadFactory(), store_root_,
                                              time_system_, platform_impl_.fileSystem(),
                                              random_generator_, bootstrap_)) {
  Envoy::Event::Libevent::Global::initialize();
}

void Fetch::fetch(const std::vector<absl::string_view>& urls) {
  absl::Notification engine_running;
  dispatcher_ = api_->allocateDispatcher("fetch_client");
  Thread::ThreadPtr envoy_thread = api_->threadFactory().createThread(
      [this, &engine_running]() -> void { runEngine(engine_running); });
  engine_running.WaitForNotification();
  for (const absl::string_view url : urls) {
    sendRequest(url);
  }
  dispatcher_->exit();
  envoy_thread->join();
  {
    absl::MutexLock lock(&engine_mutex_);
    engine_->terminate();
  }
}

void Fetch::sendRequest(absl::string_view url_string) {
  Http::Utility::Url url;
  if (!url.initialize(url_string, /*is_connect_request=*/false)) {
    std::cerr << "Unable to parse url: '" << url_string << "'\n";
    return;
  }
  std::cout << "Fetching url: " << url.toString() << "\n";

  absl::Notification request_finished;
  Platform::StreamPrototypeSharedPtr stream_prototype;
  {
    absl::MutexLock lock(&engine_mutex_);
    stream_prototype = engine_->streamClient()->newStreamPrototype();
  }
  EnvoyStreamCallbacks stream_callbacks;
  stream_callbacks.on_headers_ = [](const Http::ResponseHeaderMap& headers, bool /* end_stream */,
                                    envoy_stream_intel intel) {
    std::cerr << "Received headers on connection: " << intel.connection_id << "with headers:\n"
              << headers << "\n";
  };
  stream_callbacks.on_data_ = [](const Buffer::Instance& buffer, uint64_t length, bool end_stream,
                                 envoy_stream_intel) {
    std::string response_body(length, ' ');
    buffer.copyOut(0, length, response_body.data());
    std::cerr << response_body << "\n";
    if (end_stream) {
      std::cerr << "Received final data\n";
    }
  };
  stream_callbacks.on_complete_ = [&request_finished](envoy_stream_intel,
                                                      envoy_final_stream_intel final_intel) {
    std::cerr << "Request finished after "
              << final_intel.stream_end_ms - final_intel.stream_start_ms << "ms\n";
    request_finished.Notify();
  };
  stream_callbacks.on_error_ = [&request_finished](EnvoyError error, envoy_stream_intel,
                                                   envoy_final_stream_intel final_intel) {
    std::cerr << "Request failed after " << final_intel.stream_end_ms - final_intel.stream_start_ms
              << "ms with error message: " << error.message << "\n";
    request_finished.Notify();
  };
  stream_callbacks.on_cancel_ = [&request_finished](envoy_stream_intel,
                                                    envoy_final_stream_intel final_intel) {
    std::cerr << "Request cancelled after "
              << final_intel.stream_end_ms - final_intel.stream_start_ms << "ms\n";
    request_finished.Notify();
  };
  Platform::StreamSharedPtr stream =
      stream_prototype->start(std::move(stream_callbacks), /*explicit_flow_control=*/false);

  auto headers = Http::Utility::createRequestHeaderMapPtr();
  headers->addCopy(Http::LowerCaseString(":method"), "GET");
  headers->addCopy(Http::LowerCaseString(":scheme"), "https");
  headers->addCopy(Http::LowerCaseString(":authority"), url.hostAndPort());
  headers->addCopy(Http::LowerCaseString(":path"), url.pathAndQueryParams());
  stream->sendHeaders(std::move(headers), true);

  request_finished.WaitForNotification();
}

void Fetch::runEngine(absl::Notification& engine_running) {
  Platform::EngineBuilder engine_builder;
  engine_builder.setLogLevel(Logger::Logger::debug);
  engine_builder.setOnEngineRunning([&engine_running]() { engine_running.Notify(); });

  {
    absl::MutexLock lock(&engine_mutex_);
    engine_ = engine_builder.build();
  }

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

} // namespace Envoy
