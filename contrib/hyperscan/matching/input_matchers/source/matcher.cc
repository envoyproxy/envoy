#include "contrib/hyperscan/matching/input_matchers/source/matcher.h"

#include <numeric>

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Hyperscan {
Matcher::Matcher(
    const envoy::extensions::matching::input_matchers::hyperscan::v3alpha::Hyperscan& config) {
  std::vector<const char*> expressions;
  expressions.reserve(config.regexes_size());
  flags_.reserve(config.regexes_size());
  ids_.reserve(config.regexes_size());
  for (const auto& regex : config.regexes()) {
    expressions.push_back(regex.regex().c_str());
    unsigned int flag = 0;
    if (regex.caseless()) {
      flag |= HS_FLAG_CASELESS;
    }
    if (regex.dot_all()) {
      flag |= HS_FLAG_DOTALL;
    }
    if (regex.multiline()) {
      flag |= HS_FLAG_MULTILINE;
    }
    if (regex.allow_empty()) {
      flag |= HS_FLAG_ALLOWEMPTY;
    }
    if (regex.utf8()) {
      flag |= HS_FLAG_UTF8;
      if (regex.ucp()) {
        flag |= HS_FLAG_UCP;
      }
    }
    if (regex.combination()) {
      flag |= HS_FLAG_COMBINATION;
    }
    if (regex.quiet()) {
      flag |= HS_FLAG_QUIET;
    }
    flags_.push_back(flag);
    ids_.push_back(regex.id());
  }

  hs_compile_error_t* compile_err;
  if (hs_compile_multi(expressions.data(), flags_.data(), ids_.data(), expressions.size(),
                       HS_MODE_BLOCK, nullptr, &database_, &compile_err) != HS_SUCCESS) {
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

  absl::MutexLock lock(&scratch_mutex_);
  bool matched = false;
  const absl::string_view input_str = *input;
  hs_error_t err = hs_scan(
      database_, input_str.data(), input_str.size(), 0, scratch_,
      [](unsigned int, unsigned long long, unsigned long long, unsigned int, void* context) -> int {
        bool* matched = static_cast<bool*>(context);
        *matched = true;

        // Always terminate on the first match.
        return 1;
      },
      &matched);
  if (err != HS_SUCCESS && err != HS_SCAN_TERMINATED) {
    hs_free_scratch(scratch_);
    hs_free_database(database_);

    throw EnvoyException("unable to scan.");
  }

  return matched;
}

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
