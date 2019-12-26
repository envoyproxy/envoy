// NOLINT(namespace-envoy)
#include <iostream>
#include <string>

#include "envoy/common/exception.h"

#include "test/tools/schema_validator/validator.h"

int main(int argc, char** argv) {
  Envoy::Options options(argc, argv);
  Envoy::Validator v;

  try {
    v.validate(options.configPath(), options.schemaType());
  } catch (const Envoy::EnvoyException& ex) {
    std::cerr << ex.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
