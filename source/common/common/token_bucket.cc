#include "common/common/token_bucket.h"

#include <algorithm>

namespace Envoy {

// template <class Rep, class Period> TokenBucket<Rep, Period>::TokenBucket(uint64_t max_tokens,
// MonotonicTime start_time)
//     : max_tokens_(max_tokens), tokens_(max_tokens),
//       last_fill_(start_time) {}

// template <class Rep, class Period> bool TokenBucket<Rep, Period>::consume(uint64_t tokens) {
//   const auto time_now = ProdMonotonicTimeSource::instance_.currentTime();

//   if (tokens_ < max_tokens_) {
//     tokens_ = std::min(std::chrono::duration_cast<Rep, Period>(time_now - last_fill_).count() +
//     tokens_,
//         max_tokens_);
//   }

//   last_fill_ = time_now;
//   if (tokens_ < tokens) {
//     return false;
//   }

//   tokens_ -= tokens;
//   return true;
// }

} // namespace Envoy
