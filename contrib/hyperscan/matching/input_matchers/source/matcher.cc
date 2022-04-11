#include "contrib/hyperscan/matching/input_matchers/source/matcher.h"

#include <numeric>

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Hyperscan {

ScratchThreadLocal::ScratchThreadLocal(const hs_database_t* database) {
  hs_error_t err = hs_alloc_scratch(database, &scratch_);
  if (err != HS_SUCCESS) {
    throw EnvoyException(fmt::format("unable to allocate scratch space, error code {}.", err));
  }
}

Matcher::Matcher(
    const envoy::extensions::matching::input_matchers::hyperscan::v3alpha::Hyperscan& config,
    ThreadLocal::SlotAllocator& tls)
    : tls_(ThreadLocal::TypedSlot<ScratchThreadLocal>::makeUnique(tls)) {
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
  hs_error_t err =
      hs_compile_multi(expressions.data(), flags_.data(), ids_.data(), expressions.size(),
                       HS_MODE_BLOCK, nullptr, &database_, &compile_err);
  if (err != HS_SUCCESS) {
    std::string compile_err_message(compile_err->message);
    int compile_err_expression = compile_err->expression;
    hs_free_compile_error(compile_err);

    if (compile_err_expression < 0) {
      throw EnvoyException(fmt::format("unable to compile database: {}", compile_err_message));
    } else {
      throw EnvoyException(fmt::format("unable to compile pattern '{}': {}",
                                       expressions.at(compile_err_expression),
                                       compile_err_message));
    }
  }

  tls_->set(
      [this](Event::Dispatcher&) { return std::make_shared<ScratchThreadLocal>(this->database_); });
}

bool Matcher::match(absl::optional<absl::string_view> input) {
  if (!input) {
    return false;
  }

  bool matched = false;
  const absl::string_view input_str = *input;
  hs_scratch_t* scratch = tls_->get()->scratch_;
  hs_error_t err = hs_scan(
      database_, input_str.data(), input_str.size(), 0, scratch,
      [](unsigned int, unsigned long long, unsigned long long, unsigned int, void* context) -> int {
        bool* matched = static_cast<bool*>(context);
        *matched = true;

        // Non-zero if the matching should cease. Always terminate on the first match.
        return 1;
      },
      &matched);
  if (err != HS_SUCCESS && err != HS_SCAN_TERMINATED) {
    ENVOY_LOG_MISC(error, "unable to scan, error code {}.", err);
  }

  return matched;
}

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
