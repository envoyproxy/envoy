// NOLINT(namespace-envoy)
#include <iostream>
#include <string>

#include "envoy/common/exception.h"

#include "test/tools/schema_validator/validator.h"

int main(int argc, char** argv) {
  Envoy::Options options(argc, argv);

  try {
    Envoy::Validator::validate(options.jsonPath(), options.schemaType());
  } catch (const Envoy::EnvoyException& ex) {
    std::cerr << ex.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
