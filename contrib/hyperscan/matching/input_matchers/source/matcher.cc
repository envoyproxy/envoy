#include "contrib/hyperscan/matching/input_matchers/source/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Hyperscan {
Matcher::Matcher(
    const envoy::extensions::matching::input_matchers::hyperscan::v3alpha::Hyperscan& config) {
  const char* pattern = config.regex().c_str();
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

bool Matcher::match(absl::optional<absl::string_view> input) {
  if (!input) {
    return false;
  }

  bool matched = false;
  const absl::string_view input_str = *input;
  hs_error_t err = hs_scan(database_, input_str.data(), input_str.size(), 0, scratch_,
                           Matcher::eventHandler, &matched);
  if (err != HS_SUCCESS && err != HS_SCAN_TERMINATED) {
    hs_free_scratch(scratch_);
    hs_free_database(database_);

    throw EnvoyException("unable to scan.");
  }

  return matched;
}

int Matcher::eventHandler(unsigned int, unsigned long long, unsigned long long, unsigned int,
                          void* context) {
  bool* matched = static_cast<bool*>(context);
  *matched = true;

  return 1;
}

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
