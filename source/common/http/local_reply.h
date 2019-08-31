#pragma once

#include <string>
#include <list>
#include <vector>


#include "common/common/matchers.h"

namespace Envoy {
namespace Http{

 //  const envoy::type::matcher::StringMatcher& body_pattern,
  //  const envoy::data::accesslog::v2::ResponseFlags& response_flags
struct LocalReplyMatcher {
  LocalReplyMatcher(
    std::vector<uint32_t>& status_codes,
    const envoy::type::matcher::StringMatcher& body_pattern
    ): status_codes_(move(status_codes)), body_pattern_(body_pattern){
    };

  bool match(const absl::string_view value){
    return body_pattern_.match(value);
  }

  std::vector<uint32_t> status_codes_;
  
Matchers::StringMatcherImpl body_pattern_;
  // uint64_t response_flags_;
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
      : match_rewrite_pair_list_(match_rewrite_pair_list){

        LocalReplyMatcher* test = & match_rewrite_pair_list.front().first;
        test->match("test");
        std::cout << "Test" <<  test->match("test") << '\n' ;
      };

  private:
    std::list<std::pair<LocalReplyMatcher, LocalReplyRewriter>> match_rewrite_pair_list_;
};

using LocalReplyConfigConstPtr = std::unique_ptr<const LocalReplyConfig>;

} // namespace Http
} // namespace Envoy
