#include "common/config/well_known_names.h"

namespace Envoy {
namespace Config {

std::vector<std::pair<std::string, std::string>> TagNameValues::getRegexMapping() {
  std::vector<std::pair<std::string, std::string>> name_regex_pairs;

  // Note: the default regexes are defined below in the order that they will typically be matched
  // (see the TagExtractor class definition for an explanation of the iterative matching process).
  // This ordering is roughly from most specific to least specific. Despite the fact that these
  // regexes are defined with a particular ordering in mind, users can customize the ordering of the
  // processing of the default tag extraction regexes and include custom tags with regexes via the
  // bootstrap configuration. Because of this flexibility, these regexes are designed to not
  // interfere with one another no matter the ordering. They are tested in forward and reverse
  // ordering to ensure they will be safe in most ordering configurations.

  // To give a more user-friendly explanation of the intended behavior of each regex, each is
  // preceded by a comment with a simplified notation to explain what the regex is designed to
  // match:
  // - The text that the regex is intended to capture will be enclosed in ().
  // - Other default tags that are expected to exist in the name (and may or may not have been
  // removed before this regex has been applied) are enclosed in [].
  // - Stand-ins for a variable segment of the name (including inside capture groups) will be
  // enclosed in <>.
  // - Typical * notation will be used to denote an arbitrary set of characters.

  // *_rq(_<response_code>)
  name_regex_pairs.push_back({RESPONSE_CODE, "_rq(_(\\d{3}))$"});

  // *_rq_(<response_code_class>)xx
  name_regex_pairs.push_back({RESPONSE_CODE_CLASS, "_rq_(\\d)xx$"});

  // http.[<stat_prefix>.]dynamodb.table.[<table_name>.]capacity.[<operation_name>.](__partition_id=<last_seven_characters_from_partition_id>)
  name_regex_pairs.push_back({DYNAMO_PARTITION_ID, "^http(?=\\.).*?\\.dynamodb\\.table(?=\\.).*?\\."
                                                   "capacity(?=\\.).*?(\\.__partition_id=(\\w{7}))"
                                                   "$"});

  // http.[<stat_prefix>.]dynamodb.operation.(<operation_name>.)<base_stat> or
  // http.[<stat_prefix>.]dynamodb.table.[<table_name>.]capacity.(<operation_name>.)[<partition_id>]
  name_regex_pairs.push_back({DYNAMO_OPERATION, "^http(?=\\.).*?\\.dynamodb.(?:operation|table(?="
                                                "\\.).*?\\.capacity)(\\.(.*?))(?:\\.|$)"});

  // mongo.[<stat_prefix>.]collection.[<collection>.]callsite.(<callsite>.)query.<base_stat>
  name_regex_pairs.push_back(
      {MONGO_CALLSITE,
       "^mongo(?=\\.).*?\\.collection(?=\\.).*?\\.callsite\\.((.*?)\\.).*?query.\\w+?$"});

  // http.[<stat_prefix>.]dynamodb.table.(<table_name>.) or
  // http.[<stat_prefix>.]dynamodb.error.(<table_name>.)*
  name_regex_pairs.push_back(
      {DYNAMO_TABLE, "^http(?=\\.).*?\\.dynamodb.(?:table|error)\\.((.*?)\\.)"});

  // mongo.[<stat_prefix>.]collection.(<collection>.)query.<base_stat>
  name_regex_pairs.push_back(
      {MONGO_COLLECTION, "^mongo(?=\\.).*?\\.collection\\.((.*?)\\.).*?query.\\w+?$"});

  // mongo.[<stat_prefix>.]cmd.(<cmd>.)<base_stat>
  name_regex_pairs.push_back({MONGO_CMD, "^mongo(?=\\.).*?\\.cmd\\.((.*?)\\.)\\w+?$"});

  // cluster.[<route_target_cluster>.]grpc.[<grpc_service>.](<grpc_method>.)<base_stat>
  name_regex_pairs.push_back(
      {GRPC_BRIDGE_METHOD, "^cluster(?=\\.).*?\\.grpc(?=\\.).*\\.((.*?)\\.)\\w+?$"});

  // http.[<stat_prefix>.]user_agent.(<user_agent>.)<base_stat>
  name_regex_pairs.push_back({HTTP_USER_AGENT, "^http(?=\\.).*?\\.user_agent\\.((.*?)\\.)\\w+?$"});

  // vhost.[<virtual host name>.]vcluster.(<virtual_cluster_name>.)<base_stat>
  name_regex_pairs.push_back({VIRTUAL_CLUSTER, "^vhost(?=\\.).*?\\.vcluster\\.((.*?)\\.)\\w+?$"});

  // http.[<stat_prefix>.]fault.(<downstream_cluster>.)<base_stat>
  name_regex_pairs.push_back(
      {FAULT_DOWNSTREAM_CLUSTER, "^http(?=\\.).*?\\.fault\\.((.*?)\\.)\\w+?$"});

  // listener.[<address>.]ssl.cipher.(<cipher>)
  name_regex_pairs.push_back({SSL_CIPHER, "^listener(?=\\.).*?\\.ssl\\.cipher(\\.(.*?))$"});

  // cluster.[<cluster_name>.]ssl.ciphers.(<cipher>)
  name_regex_pairs.push_back({SSL_CIPHER_SUITE, "^cluster(?=\\.).*?\\.ssl\\.ciphers(\\.(.*?))$"});

  // cluster.[<route_target_cluster>.]grpc.(<grpc_service>.)*
  name_regex_pairs.push_back({GRPC_BRIDGE_SERVICE, "^cluster(?=\\.).*?\\.grpc\\.((.*?)\\.)"});

  // tcp.(<stat_prefix>.)<base_stat>
  name_regex_pairs.push_back({TCP_PREFIX, "^tcp\\.((.*?)\\.)\\w+?$"});

  // auth.clientssl.(<stat_prefix>.)<base_stat>
  name_regex_pairs.push_back({CLIENTSSL_PREFIX, "^auth\\.clientssl\\.((.*?)\\.)\\w+?$"});

  // ratelimit.(<stat_prefix>.)<base_stat>
  name_regex_pairs.push_back({RATELIMIT_PREFIX, "^ratelimit\\.((.*?)\\.)\\w+?$"});

  // cluster.(<cluster_name>.)*
  name_regex_pairs.push_back({CLUSTER_NAME, "^cluster\\.((.*?)\\.)"});

  // http.(<stat_prefix>.)* or listener.[<address>.]http.(<stat_prefix>.)*
  name_regex_pairs.push_back(
      {HTTP_CONN_MANAGER_PREFIX, "^(?:|listener(?=\\.).*?\\.)http\\.((.*?)\\.)"});

  // listener.(<address>.)*
  name_regex_pairs.push_back(
      {LISTENER_ADDRESS, "^listener\\.(((?:[_.[:digit:]]*|[_\\[\\]aAbBcCdDeEfF[:digit:]]*))\\.)"});

  // vhost.(<virtual host name>.)*
  name_regex_pairs.push_back({VIRTUAL_HOST, "^vhost\\.((.*?)\\.)"});

  // mongo.(<stat_prefix>.)*
  name_regex_pairs.push_back({MONGO_PREFIX, "^mongo\\.((.*?)\\.)"});

  return name_regex_pairs;
}

} // namespace Config
} // namespace Envoy
