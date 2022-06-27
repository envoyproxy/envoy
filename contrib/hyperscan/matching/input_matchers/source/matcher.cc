#include "contrib/hyperscan/matching/input_matchers/source/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Hyperscan {

ScratchThreadLocal::ScratchThreadLocal(const hs_database_t* database,
                                       const hs_database_t* som_database) {
  hs_error_t err = hs_alloc_scratch(database, &scratch_);
  if (err != HS_SUCCESS) {
    throw EnvoyException(fmt::format("unable to allocate scratch space, error code {}.", err));
  }
  if (som_database) {
    err = hs_alloc_scratch(som_database, &scratch_);
    if (err != HS_SUCCESS) {
      throw EnvoyException(
          fmt::format("unable to allocate start of match scratch space, error code {}.", err));
    }
  }
}

Matcher::Matcher(const std::vector<const char*>& expressions,
                 const std::vector<unsigned int>& flags, const std::vector<unsigned int>& ids,
                 ThreadLocal::SlotAllocator& tls, bool som)
    : tls_(ThreadLocal::TypedSlot<ScratchThreadLocal>::makeUnique(tls)) {
  ASSERT(expressions.size() == flags.size());
  ASSERT(expressions.size() == ids.size());

  // Compile database.
  hs_compile_error_t* compile_err;
  hs_error_t err =
      hs_compile_multi(expressions.data(), flags.data(), ids.data(), expressions.size(),
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

  // Compile SOM database. The SOM database will report start of matching, works for replaceAll.
  if (som) {
    std::vector<unsigned int> som_flags = flags;
    for (unsigned int& som_flag : som_flags) {
      som_flag = som_flag | HS_FLAG_SOM_LEFTMOST;
    }
    err = hs_compile_multi(expressions.data(), som_flags.data(), ids.data(), expressions.size(),
                           HS_MODE_BLOCK, nullptr, &som_database_, &compile_err);
    if (err != HS_SUCCESS) {
      std::string compile_err_message(compile_err->message);
      int compile_err_expression = compile_err->expression;

      if (compile_err_expression < 0) {
        throw EnvoyException(
            fmt::format("unable to compile SOM database: {}", compile_err_message));
      } else {
        throw EnvoyException(fmt::format("unable to compile SOM pattern '{}': {}",
                                         expressions.at(compile_err_expression),
                                         compile_err_message));
      }
    }
  }

  hs_free_compile_error(compile_err);
  tls_->set([this](Event::Dispatcher&) {
    return std::make_shared<ScratchThreadLocal>(this->database_, this->som_database_);
  });
}

bool Matcher::match(absl::string_view value) const {
  bool matched = false;
  hs_scratch_t* scratch = tls_->get()->scratch_;
  hs_error_t err = hs_scan(
      database_, value.data(), value.size(), 0, scratch,
      [](unsigned int, unsigned long long, unsigned long long, unsigned int, void* context) -> int {
        bool* matched = static_cast<bool*>(context);
        *matched = true;

        // Non-zero if the matching should cease. Always terminate on the first match.
        return 1;
      },
      &matched);
  if (err != HS_SUCCESS && err != HS_SCAN_TERMINATED) {
    ENVOY_LOG_MISC(error, "unable to scan, error code {}", err);
  }

  return matched;
}

std::string Matcher::replaceAll(absl::string_view value, absl::string_view substitution) const {
  // Find matched pair.
  std::vector<Matched> founds;
  hs_scratch_t* scratch_ = tls_->get()->scratch_;
  hs_error_t err = hs_scan(
      som_database_, value.data(), value.size(), 0, scratch_,
      [](unsigned int, unsigned long long from, unsigned long long to, unsigned int,
         void* context) -> int {
        std::vector<std::pair<unsigned long long, unsigned long long>>* founds =
            static_cast<std::vector<std::pair<unsigned long long, unsigned long long>>*>(context);
        founds->push_back({from, to});

        // Continue searching.
        return 0;
      },
      &founds);
  if (err != HS_SUCCESS && err != HS_SCAN_TERMINATED) {
    ENVOY_LOG_MISC(error, "unable to scan, error code {}", err);
    return std::string(value);
  }

  // Sort founds.
  std::sort(founds.begin(), founds.end());

  // Replace matched pair with substitution.
  std::string result;
  unsigned long long pos = 0;
  for (auto& found : founds) {
    if (found.begin_ < pos) {
      continue;
    }

    result += std::string(value.substr(pos, found.begin_ - pos));
    result += std::string(substitution);
    pos = found.end_;
  }
  result += std::string(value.substr(pos));
  return result;
}

bool Matcher::match(absl::optional<absl::string_view> input) {
  if (!input) {
    return false;
  }

  return static_cast<Envoy::Regex::CompiledMatcher*>(this)->match(*input);
}

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
