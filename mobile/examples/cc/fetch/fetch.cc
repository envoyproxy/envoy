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

Fetch::Fetch(int argc, char** argv)
  : logging_context_(spdlog::level::level_enum::info,
		     Envoy::Logger::Logger::DEFAULT_LOG_FORMAT, lock_, false),
    stats_allocator_(symbol_table_),
    store_root_(stats_allocator_),
    api_(std::make_unique<Envoy::Api::Impl>(
        platform_impl_.threadFactory(), store_root_, time_system_,
	platform_impl_.fileSystem(), random_generator_, bootstrap_)) {
  Envoy::Event::Libevent::Global::initialize();
  dispatcher_ = api_->allocateDispatcher("fetch");

  // Start at 1 to skip the command name.
  for (int i = 1; i < argc; ++i) {
    urls_.push_back(argv[i]);
  }
}

void Fetch::fetch() {
  std::cout << "here! 1\n";
  std::cout << "here! 2\n";
  envoy_thread_ = api_->threadFactory().createThread([this]() -> void { runEngine(); });
  std::cout << "here! 3\n";
  engine_running_.WaitForNotification();
  std::cout << "here! 4\n";
  for (const absl::string_view url : urls_) {
    std::cout << "Sending request to: "<< url << "\n";
    sendRequest(url);
  }
  std::cout << "here! 5\n";
  dispatcher_->exit();
  envoy_thread_->join();
  engine_->terminate();
  std::cout << "here! 6\n";
}

void Fetch::sendRequest(const absl::string_view url) {
  stream_prototype_ = engine_->streamClient()->newStreamPrototype();
  absl::Notification request_finished;

  stream_prototype_->setOnHeaders(
      [this](Platform::ResponseHeadersSharedPtr headers, bool, envoy_stream_intel intel) {
	std::cout << HERE << std::endl;
	const Platform::RawHeaderMap raw_headers = headers->allHeaders();
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
				   [this,&request_finished](envoy_stream_intel, envoy_final_stream_intel final_intel) {
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
	request_finished.Notify();
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

  request_finished.WaitForNotification();
}

void Fetch::runEngine() {
  std::cout << "here! A\n";
  builder_.setOnEngineRunning([this]() { engine_running_.Notify(); });
  std::cout << "here! B\n";
  engine_ = builder_.build();
  std::cout << "here! C\n";
  dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
  std::cout << "here! D\n";
}

}  // namespace Envoy
