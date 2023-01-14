#include <iostream>

#include "examples/cc/fetch/fetch.h"

#include "source/common/api/api_impl.h"
#include "source/common/common/random_generator.h"
#include "source/common/common/thread.h"
#include "source/common/event/real_time_system.h"
#include "source/common/stats/allocator_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/exe/platform_impl.h"
#include "source/exe/process_wide.h"

#include "library/common/data/utility.h"

#define HERE __FUNCTION__ << ":" << __LINE__ << " "

namespace Envoy {
namespace Platform {

Fetch::Fetch(int argc, char** argv)
  : default_request_headers_(Envoy::Http::RequestHeaderMapImpl::create()) {
  for (int i = 1; i < argc; ++i) {
    urls_.push_back(argv[i]);
  }
}

void Fetch::sendRequest(const absl::string_view url) {
  stream_prototype_ = engine_->streamClient()->newStreamPrototype();

  stream_prototype_->setOnHeaders(
      [this](Platform::ResponseHeadersSharedPtr headers, bool, envoy_stream_intel intel) {
	std::cout << HERE << std::endl;
	const RawHeaderMap raw_headers = headers->allHeaders();
	for (const auto& [key, header] : raw_headers) {
	  for (const auto& val : header) {
	    std::cout << key << ": " << val << "\n";
	  }
	}
	(void)this;
	(void)headers;
	(void)intel;
	//        cc_.on_headers_calls++;
	//        cc_.status = absl::StrCat(headers->httpStatus());
	//        cc_.on_header_consumed_bytes_from_response = intel.consumed_bytes_from_response;
      });
  stream_prototype_->setOnData([this](envoy_data c_data, bool fin) {
    std::cout << HERE << Data::Utility::copyToString(c_data) << " fin: " << (fin ? "true" : "false") << std::endl;
    (void)this;
    release_envoy_data(c_data);
  });
  stream_prototype_->setOnComplete(
      [this](envoy_stream_intel, envoy_final_stream_intel final_intel) {
	std::cout << HERE << std::endl;
	(void)this;
	(void)final_intel;
	//        if (expect_data_streams_) {
	//          validateStreamIntel(final_intel, expect_dns_, upstream_tls_, cc_.on_complete_calls == 0);
	//        }
	//        cc_.on_complete_received_byte_count = final_intel.received_byte_count;
	//        cc_.on_complete_calls++;
	//        cc_.terminal_callback->setReady();
	std::cout << HERE << std::endl;
	full_dispatcher_->exit();
	std::cout << HERE << std::endl;
      });
  stream_prototype_->setOnError(
      [this](Platform::EnvoyErrorSharedPtr, envoy_stream_intel, envoy_final_stream_intel) {
	std::cout << HERE << std::endl;
	(void)this;
	//        cc_.on_error_calls++;
	//        cc_.terminal_callback->setReady();
      });
  stream_prototype_->setOnCancel([this](envoy_stream_intel, envoy_final_stream_intel final_intel) {
    std::cout << HERE << std::endl;
    (void)this;
    (void)final_intel;
    //    EXPECT_NE(-1, final_intel.stream_start_ms);
    //    cc_.on_cancel_calls++;
    //    cc_.terminal_callback->setReady();
  });

  stream_ = (*stream_prototype_).start(/*explicit_flow_control=*/ false);


  Platform::RequestHeadersBuilder builder(Platform::RequestMethod::GET, "https", std::string(url), "/");

  stream_->sendHeaders(std::make_shared<Platform::RequestHeaders>(builder.build()), true);

  //  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  //  envoy_data c_data = Data::Utility::toBridgeData(request_data);
  //  stream_->sendData(c_data);

  /*
  Platform::RequestTrailersBuilder builder;
  std::shared_ptr<Platform::RequestTrailers> trailers =
      std::make_shared<Platform::RequestTrailers>(builder.build());
  stream_->close(trailers);
  */
  //  Buffer::OwnedImpl request_data2 = Buffer::OwnedImpl("request body");
  //  envoy_data c_data2 = Data::Utility::toBridgeData(request_data);
  //  stream_->close(c_data2);
}

void Fetch::threadRoutine(absl::Notification& engine_running) {
  std::cout << "here! A\n";
  builder_.setOnEngineRunning([&]() { engine_running.Notify(); });
  std::cout << "here! B\n";
  engine_ = builder_.build();
  std::cout << "here! C\n";
  full_dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  std::cout << "here! D\n";
}

void Fetch::Run() {
  Envoy::Thread::MutexBasicLockable lock;
  Envoy::Logger::Context logging_context(
      spdlog::level::level_enum::info,
      Envoy::Logger::Logger::DEFAULT_LOG_FORMAT, lock, false);

  // Process-wide params for Envoy connection class.
  Envoy::PlatformImpl platform_impl;
  Envoy::Stats::SymbolTableImpl symbol_table;
  Envoy::Event::RealTimeSystem time_system;
  Envoy::Stats::AllocatorImpl stats_allocator(symbol_table);
  Envoy::Stats::ThreadLocalStoreImpl store_root(stats_allocator);
  Envoy::Random::RandomGeneratorImpl random_generator;
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  api_ = std::make_unique<Envoy::Api::Impl>(
					    platform_impl.threadFactory(), store_root, time_system,
					    platform_impl.fileSystem(), random_generator, bootstrap);

  Envoy::Event::Libevent::Global::initialize();
  full_dispatcher_ = api_->allocateDispatcher("fake_envoy_mobile");

  std::cout << "here! 1\n";
  absl::Notification engine_running;
  std::cout << "here! 2\n";
  envoy_thread_ = api_->threadFactory().createThread(
      [this, &engine_running]() -> void { threadRoutine(engine_running); });
  std::cout << "here! 3\n";
  engine_running.WaitForNotification();
  std::cout << "here! 4\n";
  for (const absl::string_view url : urls_) {
    std::cout << "Sending request to: "<< url << "\n";
    sendRequest(url);
  }
  std::cout << "here! 5\n";
  envoy_thread_->join();
  engine_->terminate();
  std::cout << "here! 6\n";
}

}  // namespace Envoy
}  // namespace Platform
