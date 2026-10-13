// NOLINT(namespace-envoy)
#include <iostream>
#include <string>

#include "envoy/common/exception.h"

#include "test/tools/schema_validator/validator.h"

int main(int argc, char** argv) {
  try {
    Envoy::Validator::run(argc, argv);
  } catch (const Envoy::EnvoyException& ex) {
    std::cerr << ex.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
