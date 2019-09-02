#pragma once

#include <string>
#include <list>
#include <vector>


#include "common/common/matchers.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.validate.h"

namespace Envoy {
namespace Http{

 //  const envoy::type::matcher::StringMatcher& body_pattern,
  //  const envoy::data::accesslog::v2::ResponseFlags& response_flags
struct LocalReplyMatcher {
  LocalReplyMatcher(std::vector<uint32_t>& status_codes,
   const envoy::type::matcher::StringMatcher& body_pattern): status_codes_(move(status_codes)),
    body_pattern_(body_pattern){};

  bool match(const absl::string_view value) const{
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
      const envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& config
      ){
      for (const auto& match_rewrite_config : config.send_local_reply_config()) {
        std::vector<uint32_t> status_codes;
        for (const auto code : match_rewrite_config.match().status_codes()) {
          status_codes.emplace_back(code);
        }
        
        std::pair<Http::LocalReplyMatcher, Http::LocalReplyRewriter> pair =
            std::make_pair(
                Http::LocalReplyMatcher{status_codes, match_rewrite_config.match().body_pattern()},
                Http::LocalReplyRewriter{match_rewrite_config.rewrite().status()});
        match_rewrite_pair_list_.emplace_back(std::move(pair));

      }
      };

      bool match(const absl::string_view value) const{
        return match_rewrite_pair_list_.front().first.match(value);
  }

  private:
    std::list<std::pair<LocalReplyMatcher, LocalReplyRewriter>> match_rewrite_pair_list_;
};

using LocalReplyConfigConstPtr = std::unique_ptr<const LocalReplyConfig>;

} // namespace Http
} // namespace Envoy
