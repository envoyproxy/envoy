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
#include "library/common/bridge/utility.h"
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

envoy_status_t Fetch::fetch(const std::vector<absl::string_view>& urls,
                            const std::vector<absl::string_view>& quic_hints,
                            std::vector<Http::Protocol>& protocols) {
  absl::Notification engine_running;
  dispatcher_ = api_->allocateDispatcher("fetch_client");
  Thread::ThreadPtr envoy_thread = api_->threadFactory().createThread(
      [this, &engine_running, &quic_hints]() -> void { runEngine(engine_running, quic_hints); });
  engine_running.WaitForNotification();
  envoy_status_t status = ENVOY_SUCCESS;
  for (const absl::string_view url : urls) {
    status = sendRequest(url, protocols);
    if (status == ENVOY_FAILURE) {
      break;
    }
  }
  dispatcher_->exit();
  envoy_thread->join();
  {
    absl::MutexLock lock(&engine_mutex_);
    engine_->terminate();
  }
  return status;
}

envoy_status_t Fetch::sendRequest(absl::string_view url_string,
                                  std::vector<Http::Protocol>& protocols) {
  Http::Utility::Url url;
  if (!url.initialize(url_string, /*is_connect_request=*/false)) {
    std::cerr << "Unable to parse url: '" << url_string << "'\n";
    return ENVOY_FAILURE;
  }
  std::cout << "Fetching url: " << url.toString() << "\n";

  absl::Notification request_finished;
  Platform::StreamPrototypeSharedPtr stream_prototype;
  {
    absl::MutexLock lock(&engine_mutex_);
    stream_prototype = engine_->streamClient()->newStreamPrototype();
  }
  envoy_status_t status = ENVOY_SUCCESS;
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
  stream_callbacks.on_complete_ =
      [&request_finished, &protocols](envoy_stream_intel, envoy_final_stream_intel final_intel) {
        std::cerr << "Request finished after "
                  << final_intel.stream_end_ms - final_intel.stream_start_ms << "ms\n";
        protocols.push_back(static_cast<Http::Protocol>(final_intel.upstream_protocol));
        request_finished.Notify();
      };
  stream_callbacks.on_error_ = [&request_finished, &status](const EnvoyError& error,
                                                            envoy_stream_intel,
                                                            envoy_final_stream_intel final_intel) {
    status = ENVOY_FAILURE;
    std::cerr << "Request failed after " << final_intel.stream_end_ms - final_intel.stream_start_ms
              << "ms with error message: " << error.message_ << "\n";
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
  return status;
}

void Fetch::runEngine(absl::Notification& engine_running,
                      const std::vector<absl::string_view>& quic_hints) {
  Platform::EngineBuilder engine_builder;
  engine_builder.setLogLevel(Logger::Logger::trace);
  engine_builder.addRuntimeGuard("dns_cache_set_ip_version_to_remove", true);
  engine_builder.setOnEngineRunning([&engine_running]() { engine_running.Notify(); });
  if (!quic_hints.empty()) {
    engine_builder.enableHttp3(true);
    for (const auto& quic_hint : quic_hints) {
      engine_builder.addQuicHint(std::string(quic_hint), 443);
    }
  }

  {
    absl::MutexLock lock(&engine_mutex_);
    engine_ = engine_builder.build();
  }

  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

} // namespace Envoy
