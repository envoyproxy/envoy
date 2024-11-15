// NOLINT(namespace-envoy)
#include <iostream>
#include <stdexcept>
#include <string>

#include "source/common/common/fmt.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/config/protobuf_link_hacks.h"
#include "source/common/event/libevent.h"

#include "test/config_test/config_test.h"

#include "gtest/gtest.h"

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: config_load_check PATH\n"
                 "\nValidate configuration files against json schema\n"
                 "\n\tPATH - root of the path that holds the json files to verify."
                 " The tool recursively searches for json files to validate."
              << std::endl;
    return EXIT_FAILURE;
  }
  try {

    Envoy::Event::Libevent::Global::initialize();
    Envoy::Thread::MutexBasicLockable lock;
    Envoy::Logger::Context logging_context(static_cast<spdlog::level::level_enum>(2),
                                           Envoy::Logger::Logger::DEFAULT_LOG_FORMAT, lock, false);

    const uint32_t num_tested = Envoy::ConfigTest::run(std::string(argv[1]));
    std::cout << fmt::format("Configs tested: {}. ", num_tested);
    if (testing::Test::HasFailure()) {
      std::cerr << "There were failures. Please Fix your configuration files." << std::endl;
      return EXIT_FAILURE;
    } else {
      std::cout << "No failures." << std::endl;
      return EXIT_SUCCESS;
    }
  } catch (const std::runtime_error& e) {
    // catch directory not found runtime exception.
    std::cerr << e.what() << std::endl;
  }
  return EXIT_FAILURE;
}
