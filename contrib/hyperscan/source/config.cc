#include "contrib/hyperscan/source/config.h"

#include <hs/hs_common.h>

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Regex {
namespace Hyperscan {
CompiledHyperscanMatcher::CompiledHyperscanMatcher(
    const std::string& regex, const envoy::extensions::hyperscan::v3alpha::Hyperscan& config) {
  const char* pattern = regex.c_str();
  unsigned int flags = 0;
  if (config.caseless()) {
    flags = flags | HS_FLAG_CASELESS;
  }
  if (config.dot_all()) {
    flags = flags | HS_FLAG_DOTALL;
  }
  if (config.multiline()) {
    flags = flags | HS_FLAG_MULTILINE;
  }
  if (config.allow_empty()) {
    flags = flags | HS_FLAG_ALLOWEMPTY;
  }
  if (config.utf8()) {
    flags = flags | HS_FLAG_UTF8;
    if (config.ucp()) {
      flags = flags | HS_FLAG_UCP;
    }
  }
  if (config.combination()) {
    flags = flags | HS_FLAG_COMBINATION;
  }

  hs_compile_error_t* compile_err;
  if (hs_compile(pattern, flags, HS_MODE_BLOCK, nullptr, &database_, &compile_err) != HS_SUCCESS) {
    std::string compile_err_message(compile_err->message);
    hs_free_compile_error(compile_err);

    throw EnvoyException(compile_err_message);
  }

  if (hs_alloc_scratch(database_, &scratch_) != HS_SUCCESS) {
    hs_free_database(database_);

    throw EnvoyException("unable to allocate scratch space.");
  }
}

bool CompiledHyperscanMatcher::match(absl::string_view value) const {
  bool matched = false;
  hs_error_t err = hs_scan(database_, value.data(), value.size(), 0, scratch_,
                           CompiledHyperscanMatcher::eventHandler, &matched);
  if (err != HS_SUCCESS && err != HS_SCAN_TERMINATED) {
    hs_free_scratch(scratch_);
    hs_free_database(database_);

    throw EnvoyException("unable to scan.");
  }

  return matched;
}

int CompiledHyperscanMatcher::eventHandler(unsigned int, unsigned long long, unsigned long long,
                                           unsigned int, void* context) {
  bool* matched = static_cast<bool*>(context);
  *matched = true;

  return 1;
}

REGISTER_FACTORY(CompiledHyperscanMatcherFactory, Envoy::Regex::CompiledMatcherFactory);
} // namespace Hyperscan
} // namespace Regex
} // namespace Extensions
} // namespace Envoy
