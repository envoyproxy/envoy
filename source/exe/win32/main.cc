#include <string>

#include "common/common/thread_impl.h"

#include "exe/main_common_base.h"

#include "absl/debugging/symbolize.h"

// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
// clang-format on

// NOLINT(namespace-envoy)

/**
 * Basic Site-Specific main()
 *
 * This should be used to do setup tasks specific to a particular site's
 * deployment such as initializing signal handling. It calls main_common
 * after setting up command line options.
 */

namespace {
class WinsockCleanup {
public:
  ~WinsockCleanup() { WSACleanup(); }
};
} // namespace

int main(int argc, char** argv) {
  // absl::Symbolize mostly works without this, but this improves corner case
  // handling, such as running in a chroot jail.
  absl::InitializeSymbolizer(argv[0]);

  const WORD wVersionRequested = MAKEWORD(2, 2);
  WSADATA wsaData;

  const int rc = WSAStartup(wVersionRequested, &wsaData);
  if (rc != 0) {
    printf("WSAStartup failed with error: %d\n", rc);
    return 1;
  }
  WinsockCleanup winsock_cleanup;

  std::unique_ptr<Envoy::MainCommonBase> main_common_base;
  std::unique_ptr<Envoy::OptionsImpl> options;
  Envoy::Thread::ThreadFactoryImplWin32 thread_factory;
  auto hotRestartVersion = [](uint64_t, uint64_t, bool) -> std::string { return "disabled"; };

  // Initialize the server's main context under a try/catch loop and simply return EXIT_FAILURE
  // as needed. Whatever code in the initialization path that fails is expected to log an error
  // message so the user can diagnose.
  try {
    options =
        std::make_unique<Envoy::OptionsImpl>(argc, argv, hotRestartVersion, spdlog::level::info);
    main_common_base = std::make_unique<Envoy::MainCommonBase>(*options, thread_factory);
  } catch (const Envoy::NoServingException& e) {
    return EXIT_SUCCESS;
  } catch (const Envoy::MalformedArgvException& e) {
    return EXIT_FAILURE;
  } catch (const Envoy::EnvoyException& e) {
    return EXIT_FAILURE;
  }

  // Run the server listener loop outside try/catch blocks, so that unexpected exceptions
  // show up as a core-dumps for easier diagnostis.
  return main_common_base->run() ? EXIT_SUCCESS : EXIT_FAILURE;
}
