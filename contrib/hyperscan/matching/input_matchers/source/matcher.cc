#include "contrib/hyperscan/matching/input_matchers/source/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Hyperscan {

ScratchThreadLocal::ScratchThreadLocal(const hs_database_t* database,
                                       const hs_database_t* start_of_match_database) {
  hs_error_t err = hs_alloc_scratch(database, &scratch_);
  if (err != HS_SUCCESS) {
    IS_ENVOY_BUG(fmt::format("unable to allocate scratch space, error code {}.", err));
  }
  if (start_of_match_database) {
    err = hs_alloc_scratch(start_of_match_database, &scratch_);
    if (err != HS_SUCCESS) {
      IS_ENVOY_BUG(
          fmt::format("unable to allocate start of match scratch space, error code {}.", err));
    }
  }
}

ScratchThreadLocal::~ScratchThreadLocal() { hs_free_scratch(scratch_); }

Bound::Bound(uint64_t begin, uint64_t end) : begin_(begin), end_(end) {}

bool Bound::operator<(const Bound& other) const {
  if (begin_ == other.begin_) {
    return end_ > other.end_;
  }
  return begin_ < other.begin_;
}

Matcher::Matcher(const std::vector<const char*>& expressions,
                 const std::vector<unsigned int>& flags, const std::vector<unsigned int>& ids,
                 Event::Dispatcher& main_thread_dispatcher, ThreadLocal::SlotAllocator& tls,
                 bool report_start_of_matching)
    : main_thread_dispatcher_(main_thread_dispatcher),
      tls_(ThreadLocal::TypedSlot<ScratchThreadLocal>::makeUnique(tls)) {
  ASSERT(expressions.size() == flags.size());
  ASSERT(expressions.size() == ids.size());

  // Compile database.
  compile(expressions, flags, ids, &database_);

  // Compile start of match database which will report start of matching, works for replaceAll.
  if (report_start_of_matching) {
    std::vector<unsigned int> start_of_match_flags = flags;
    for (unsigned int& start_of_match_flag : start_of_match_flags) {
      start_of_match_flag = start_of_match_flag | HS_FLAG_SOM_LEFTMOST;
    }
    compile(expressions, start_of_match_flags, ids, &start_of_match_database_);
  }

  tls_->set([this](Event::Dispatcher&) {
    return std::make_shared<ScratchThreadLocal>(database_, start_of_match_database_);
  });
}

Matcher::~Matcher() {
  hs_free_database(database_);
  hs_free_database(start_of_match_database_);
}

bool Matcher::match(absl::string_view value) const {
  bool matched = false;
  ScratchThreadLocalPtr local_scratch;
  hs_scratch_t* scratch = getScratch(local_scratch);
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
    IS_ENVOY_BUG(fmt::format("unable to scan, error code {}", err));
  }

  return matched;
}

std::string Matcher::replaceAll(absl::string_view value, absl::string_view substitution) const {
  // Find matched bounds.
  std::vector<Bound> bounds;
  ScratchThreadLocalPtr local_scratch;
  hs_scratch_t* scratch = getScratch(local_scratch);
  hs_error_t err = hs_scan(
      start_of_match_database_, value.data(), value.size(), 0, scratch,
      [](unsigned int, unsigned long long from, unsigned long long to, unsigned int,
         void* context) -> int {
        std::vector<Bound>* bounds = static_cast<std::vector<Bound>*>(context);
        bounds->push_back({from, to});

        // Continue searching.
        return 0;
      },
      &bounds);
  if (err != HS_SUCCESS && err != HS_SCAN_TERMINATED) {
    IS_ENVOY_BUG(fmt::format("unable to scan, error code {}", err));
    return std::string(value);
  }

  // Sort bounds. Make sure the longest length bound in the front will appear first.
  std::sort(bounds.begin(), bounds.end());

  // Concatenate string and replace matched pair with substitution.
  std::vector<absl::string_view> parts;
  parts.reserve(bounds.size() * 2);
  uint64_t pos = 0;
  for (Bound& bound : bounds) {
    if (bound.begin_ < pos) {
      continue;
    }

    parts.emplace_back(value.substr(pos, bound.begin_ - pos));
    parts.emplace_back(substitution);
    pos = bound.end_;
  }
  parts.emplace_back(value.substr(pos));
  return absl::StrJoin(parts, "");
}

bool Matcher::match(const ::Envoy::Matcher::MatchingDataType& input) {
  if (absl::holds_alternative<absl::monostate>(input)) {
    return false;
  }

  return static_cast<Envoy::Regex::CompiledMatcher*>(this)->match(absl::get<std::string>(input));
}

void Matcher::compile(const std::vector<const char*>& expressions,
                      const std::vector<unsigned int>& flags, const std::vector<unsigned int>& ids,
                      hs_database_t** database) {
  hs_compile_error_t* compile_err;
  hs_error_t err =
      hs_compile_multi(expressions.data(), flags.data(), ids.data(), expressions.size(),
                       HS_MODE_BLOCK, nullptr, database, &compile_err);
  if (err != HS_SUCCESS) {
    std::string compile_err_message(compile_err->message);
    int compile_err_expression = compile_err->expression;
    hs_free_compile_error(compile_err);

    if (compile_err_expression < 0) {
      IS_ENVOY_BUG(fmt::format("unable to compile database: {}", compile_err_message));
    } else {
      throw EnvoyException(fmt::format("unable to compile pattern '{}': {}",
                                       expressions.at(compile_err_expression),
                                       compile_err_message));
    }
  }
  hs_free_compile_error(compile_err);
}

hs_scratch_t* Matcher::getScratch(ScratchThreadLocalPtr& local_scratch) const {
  // Some matchers are constructed before dispatching threads and set() method of thread local slot
  // will only initialize thread local object in existing threads, which may lead to unintialized
  // thread local object in threads which are dispatched later. E.g, stats matchers are constructed
  // before workers while there is chance to use these matchers in working threads. As a result,
  // we have to ask main thread to allocate thread local object again.
  if (!tls_->get().has_value()) {
    main_thread_dispatcher_.post([this]() {
      tls_->set([this](Event::Dispatcher&) {
        return std::make_shared<ScratchThreadLocal>(database_, start_of_match_database_);
      });
    });

    local_scratch = std::make_unique<ScratchThreadLocal>(database_, start_of_match_database_);
    return local_scratch->scratch_;
  }

  return tls_->get()->scratch_;
}

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
