#pragma once

#include <string>
#include <list>

#include "common/common/matchers.h"

namespace Envoy {
namespace Http{

struct LocalReplyMatcher {  
  std::list<uint32_t> status_;
  Matchers::StringMatcher body_pattern_;
  uint64_t response_flags_;
};

/**
 * Structure which holds rewriter configuration from proto file for LocalReplyConfig.
 */
struct LocalReplyRewriter {
  uint32_t status_;
};

class LocalReplyConfig {
  public:
    LocalReplyConfig(
      std::list<std::pair<LocalReplyMatcher, LocalReplyRewriter>>& match_rewrite_pair_list)
      : match_rewrite_pair_list_(match_rewrite_pair_list){};

  private:
    std::list<std::pair<LocalReplyMatcher, LocalReplyRewriter>> match_rewrite_pair_list_;
}

using LocalReplyConfigConstPtr = std::unique_ptr<const LocalReplyConfig>;

} // namespace Http
} // namespace Envoy
