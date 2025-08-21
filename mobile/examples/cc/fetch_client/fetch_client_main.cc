#include <iostream>

#include "envoy/http/protocol.h"

#include "source/common/http/utility.h"

#include "absl/synchronization/notification.h"
#include "library/cc/engine.h"
#include "library/cc/engine_builder.h"
#include "library/cc/stream.h"
#include "library/cc/stream_client.h"
#include "library/cc/stream_prototype.h"
#include "library/common/engine_types.h"
#include "library/common/http/header_utility.h"

extern const char build_scm_revision[];
extern const char build_scm_status[];

// These are required by the version library in "source/common/version/version.h".
// TODO(fortuna): set default values in the version library instead.
const char build_scm_revision[] = "0";
const char build_scm_status[] = "test";

Envoy::EnvoyStreamCallbacks
makeLoggingStreamCallbacks(envoy_status_t* status, absl::Notification* request_finished,
                           std::vector<Envoy::Http::Protocol>* used_protocols) {
  Envoy::EnvoyStreamCallbacks stream_callbacks;
  stream_callbacks.on_headers_ = [](const Envoy::Http::ResponseHeaderMap& headers,
                                    bool /* end_stream */, envoy_stream_intel intel) {
    std::cerr << "Received headers on connection: " << intel.connection_id << "with headers:\n"
              << headers << "\n";
  };
  stream_callbacks.on_data_ = [](const Envoy::Buffer::Instance& buffer, uint64_t length,
                                 bool end_stream, envoy_stream_intel) {
    std::string response_body(length, ' ');
    buffer.copyOut(0, length, response_body.data());
    std::cerr << response_body << "\n";
    if (end_stream) {
      std::cerr << "Received final data\n";
    }
  };
  stream_callbacks.on_complete_ = [request_finished, used_protocols](
                                      envoy_stream_intel, envoy_final_stream_intel final_intel) {
    std::cerr << "Request finished after "
              << final_intel.stream_end_ms - final_intel.stream_start_ms << "ms\n";
    used_protocols->push_back(static_cast<Envoy::Http::Protocol>(final_intel.upstream_protocol));
    request_finished->Notify();
  };
  stream_callbacks.on_error_ = [request_finished, status](const Envoy::EnvoyError& error,
                                                          envoy_stream_intel,
                                                          envoy_final_stream_intel final_intel) {
    *status = ENVOY_FAILURE;
    std::cerr << "Request failed after " << final_intel.stream_end_ms - final_intel.stream_start_ms
              << "ms with error message: " << error.message_ << "\n";
    request_finished->Notify();
  };
  stream_callbacks.on_cancel_ = [request_finished](envoy_stream_intel,
                                                   envoy_final_stream_intel final_intel) {
    std::cerr << "Request cancelled after "
              << final_intel.stream_end_ms - final_intel.stream_start_ms << "ms\n";
    request_finished->Notify();
  };
  return stream_callbacks;
}

// Fetches each URL specified on the command line in series,
// and prints the contents to standard out.
int main(int argc, char** argv) {
  std::vector<absl::string_view> urls;
  // Start at 1 to skip the command name.
  for (int i = 1; i < argc; ++i) {
    urls.push_back(argv[i]);
  }

  // Build and run engine.
  absl::Notification engine_running;
  Envoy::Platform::EngineSharedPtr engine =
      Envoy::Platform::EngineBuilder()
          .setLogLevel(Envoy::Logger::Logger::trace)
          .addRuntimeGuard("dns_cache_set_ip_version_to_remove", true)
          .addRuntimeGuard("quic_no_tcp_delay", true)
          .setOnEngineRunning([&engine_running]() { engine_running.Notify(); })
          .build();
  engine_running.WaitForNotification();

  // Iterate over the input URLs, reusing the engine.
  for (const auto& url_string : urls) {
    Envoy::Http::Utility::Url url;
    if (!url.initialize(url_string, /*is_connect_request=*/false)) {
      std::cerr << "Unable to parse url: '" << url_string << "'\n";
      return ENVOY_FAILURE;
    }
    std::cout << "Fetching url: " << url.toString() << "\n";

    // Create stream.
    envoy_status_t status = ENVOY_SUCCESS;
    absl::Notification request_finished;
    std::vector<Envoy::Http::Protocol> used_protocols;
    Envoy::Platform::StreamSharedPtr stream = engine->streamClient()->newStreamPrototype()->start(
        makeLoggingStreamCallbacks(&status, &request_finished, &used_protocols),
        /*explicit_flow_control=*/false);

    // Send request
    auto headers = Envoy::Http::Utility::createRequestHeaderMapPtr();
    headers->addCopy(Envoy::Http::LowerCaseString(":method"), "GET");
    headers->addCopy(Envoy::Http::LowerCaseString(":scheme"), "https");
    headers->addCopy(Envoy::Http::LowerCaseString(":authority"), url.hostAndPort());
    headers->addCopy(Envoy::Http::LowerCaseString(":path"), url.pathAndQueryParams());
    stream->sendHeaders(std::move(headers), true);

    // Wait for it to be done.
    request_finished.WaitForNotification();
  }

  engine->terminate();

  exit(0);
}
