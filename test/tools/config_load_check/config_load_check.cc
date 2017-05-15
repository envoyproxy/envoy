#include <iostream>
#include <string>
#include <stdexcept>

#include "test/config_test/config_test.h"

#include "spdlog/spdlog.h"

int main(int argc, char* argv[]) {
  if (argc != 2) {
    return EXIT_FAILURE;
  }
  try {
    uint32_t num_tested = ConfigTest::run(std::string(argv[1]));
    std::cout << fmt::format("Successfully tested: {}", num_tested) << std::endl;
    return EXIT_SUCCESS;
  } catch (const std::runtime_error& e) {
    std::cerr << e.what() << std::endl;
  }
  std::cerr << "Exiting with failure" << std::endl;
  return EXIT_FAILURE;
}
