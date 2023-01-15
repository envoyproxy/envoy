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

#define HERE __FUNCTION__ << ":" << __LINE__ << " "

namespace Envoy {

Fetch::Fetch(int argc, char** argv)
    : logging_context_(spdlog::level::level_enum::info, Envoy::Logger::Logger::DEFAULT_LOG_FORMAT,
                       lock_, false),
      stats_allocator_(symbol_table_), store_root_(stats_allocator_),
      api_(std::make_unique<Envoy::Api::Impl>(platform_impl_.threadFactory(), store_root_,
                                              time_system_, platform_impl_.fileSystem(),
                                              random_generator_, bootstrap_)) {
  Envoy::Event::Libevent::Global::initialize();
  dispatcher_ = api_->allocateDispatcher("fetch");

  // Start at 1 to skip the command name.
  for (int i = 1; i < argc; ++i) {
    urls_.push_back(argv[i]);
  }
}

void Fetch::fetch() {
  Thread::ThreadPtr envoy_thread =
      api_->threadFactory().createThread([this]() -> void { runEngine(); });
  engine_running_.WaitForNotification();
  for (const absl::string_view url : urls_) {
    sendRequest(url);
  }
  dispatcher_->exit();
  envoy_thread->join();
  engine_->terminate();
}

void Fetch::sendRequest(const absl::string_view url_string) {
  Envoy::Http::Utility::Url url;
  if (!url.initialize(url_string, /*is_connect_request=*/false)) {
    std::cerr << "Unable to parse url: '" << url_string << "'\n";
    return;
  }
  std::cout << "Fetching url: " << url.toString() << "\n";

  absl::Notification request_finished;
  stream_prototype_ = engine_->streamClient()->newStreamPrototype();
  stream_prototype_->setOnHeaders(
      [](Platform::ResponseHeadersSharedPtr headers, bool, envoy_stream_intel intel) {
        std::cerr << "Received headers on connection: " << intel.connection_id << "\n";

        const Platform::RawHeaderMap raw_headers = headers->allHeaders();
        for (const auto& [key, header] : raw_headers) {
          for (const auto& val : header) {
            std::cout << key << ": " << val << "\n";
          }
        }
      });
  stream_prototype_->setOnData([](envoy_data c_data, bool fin) {
    std::cout << Data::Utility::copyToString(c_data);
    if (fin) {
      std::cout << "Received final data\n";
    }
    release_envoy_data(c_data);
  });
  stream_prototype_->setOnComplete(
      [&request_finished](envoy_stream_intel, envoy_final_stream_intel final_intel) {
        std::cerr << "Request finished after "
                  << final_intel.stream_end_ms - final_intel.stream_start_ms << "ms\n";
        request_finished.Notify();
      });
  stream_prototype_->setOnError([&request_finished](Platform::EnvoyErrorSharedPtr,
                                                    envoy_stream_intel,
                                                    envoy_final_stream_intel final_intel) {
    std::cerr << "Request failed after " << final_intel.stream_end_ms - final_intel.stream_start_ms
              << "ms\n";
    request_finished.Notify();
  });
  stream_prototype_->setOnCancel(
      [&request_finished](envoy_stream_intel, envoy_final_stream_intel final_intel) {
        std::cerr << "Request cancelled after "
                  << final_intel.stream_end_ms - final_intel.stream_start_ms << "ms\n";
        request_finished.Notify();
      });

  stream_ = (*stream_prototype_).start(/*explicit_flow_control=*/false);

  Platform::RequestHeadersBuilder builder(Platform::RequestMethod::GET, std::string(url.scheme()),
                                          std::string(url.hostAndPort()),
                                          std::string(url.pathAndQueryParams()));

  stream_->sendHeaders(std::make_shared<Platform::RequestHeaders>(builder.build()), true);
  request_finished.WaitForNotification();
}

void Fetch::runEngine() {
  builder_.setOnEngineRunning([this]() { engine_running_.Notify(); });
  engine_ = builder_.build();
  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

} // namespace Envoy
