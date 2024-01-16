#include "examples/cc/fetch_client/fetch_client.h"

#include <iostream>

#include "source/common/api/api_impl.h"
#include "source/common/common/random_generator.h"
#include "source/common/common/thread.h"
#include "source/common/event/real_time_system.h"
#include "source/common/http/utility.h"
#include "source/common/stats/allocator_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/exe/platform_impl.h"
#include "source/exe/process_wide.h"

#include "library/common/data/utility.h"

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

void Fetch::sendRequest(const absl::string_view url_string) {
  Envoy::Http::Utility::Url url;
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
  stream_prototype->setOnHeaders(
      [](Platform::ResponseHeadersSharedPtr headers, bool, envoy_stream_intel intel) {
        std::cerr << "Received headers on connection: " << intel.connection_id << "\n";

        const Platform::RawHeaderMap raw_headers = headers->allHeaders();
        for (const auto& [key, header] : raw_headers) {
          for (const auto& val : header) {
            std::cout << key << ": " << val << "\n";
          }
        }
      });
  stream_prototype->setOnData([](envoy_data c_data, bool fin) {
    std::cout << Data::Utility::copyToString(c_data);
    if (fin) {
      std::cout << "Received final data\n";
    }
    release_envoy_data(c_data);
  });
  stream_prototype->setOnComplete(
      [&request_finished](envoy_stream_intel, envoy_final_stream_intel final_intel) {
        std::cerr << "Request finished after "
                  << final_intel.stream_end_ms - final_intel.stream_start_ms << "ms\n";
        request_finished.Notify();
      });
  stream_prototype->setOnError([&request_finished](Platform::EnvoyErrorSharedPtr,
                                                   envoy_stream_intel,
                                                   envoy_final_stream_intel final_intel) {
    std::cerr << "Request failed after " << final_intel.stream_end_ms - final_intel.stream_start_ms
              << "ms\n";
    request_finished.Notify();
  });
  stream_prototype->setOnCancel(
      [&request_finished](envoy_stream_intel, envoy_final_stream_intel final_intel) {
        std::cerr << "Request cancelled after "
                  << final_intel.stream_end_ms - final_intel.stream_start_ms << "ms\n";
        request_finished.Notify();
      });

  Platform::StreamSharedPtr stream = stream_prototype->start(/*explicit_flow_control=*/false);

  Platform::RequestHeadersBuilder builder(Platform::RequestMethod::GET, std::string(url.scheme()),
                                          std::string(url.hostAndPort()),
                                          std::string(url.pathAndQueryParams()));

  stream->sendHeaders(std::make_shared<Platform::RequestHeaders>(builder.build()), true);
  request_finished.WaitForNotification();
}

void Fetch::runEngine(absl::Notification& engine_running) {
  Platform::EngineBuilder engine_builder;
  engine_builder.addLogLevel(Envoy::Platform::LogLevel::debug);
  engine_builder.setOnEngineRunning([&engine_running]() { engine_running.Notify(); });

  {
    absl::MutexLock lock(&engine_mutex_);
    engine_ = engine_builder.build();
  }

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

} // namespace Envoy
