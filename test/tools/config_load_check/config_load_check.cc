#include <iostream>
#include <stdexcept>
#include <string>

#include "test/config_test/config_test.h"

#include "spdlog/spdlog.h"

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: config_load_check PATH" << std::endl;
    std::cerr << "\nValidate configuration files against json schema" << std::endl;
    std::cerr << "\n\tPATH - root of the path that holds the json files to verify.";
    std::cerr << " The tool recursively searches for json files to validate." << std::endl;
    return EXIT_FAILURE;
  }
  try {
    const uint32_t num_tested = ConfigTest::run(std::string(argv[1]));
    std::cout << fmt::format("Successfully tested: {}", num_tested) << std::endl;
    return EXIT_SUCCESS;
  } catch (const std::runtime_error& e) {
    // catch directory not found runtime exception.
    std::cerr << e.what() << std::endl;
  }
  std::cerr << "Exiting with failure" << std::endl;
  return EXIT_FAILURE;
}
