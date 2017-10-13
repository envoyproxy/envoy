#include "common/config/well_known_names.h"

namespace Envoy {
namespace Config {

std::vector<std::pair<std::string, std::string>> TagNameValues::getRegexMapping() {
  std::vector<std::pair<std::string, std::string>> name_regex_pairs;

  // cluster.<cluster_name>.
  name_regex_pairs.push_back({CLUSTER_NAME, "^cluster\\.((.*?)\\.)"});

  // listener.<port>.
  name_regex_pairs.push_back({LISTENER_PORT, "^listener\\.((\\d+?)\\.)"});

  // http.<stat_prefix>.
  name_regex_pairs.push_back({HTTP_CONN_MANAGER_PREFIX, "^http\\.((.*?)\\.)"});

  // vhost.<virtual host name>.
  name_regex_pairs.push_back({VIRTUAL_HOST, "^vhost\\.((.*?)\\.)"});

  // mongo.<stat_prefix>.
  name_regex_pairs.push_back({MONGO_PREFIX, "^mongo\\.((.*?)\\.)"});

  // tcp.<stat_prefix>.
  name_regex_pairs.push_back({TCP_PREFIX, "^tcp\\.((.*?)\\.)"});

  // auth.clientssl.<stat_prefix>.
  name_regex_pairs.push_back({CLIENTSSL_PREFIX, "^auth\\.clientssl\\.((.*?)\\.)"});

  // ratelimit.<stat_prefix>.
  name_regex_pairs.push_back({RATELIMIT_PREFIX, "^ratelimit\\.((.*?)\\.)"});

  // http.<stat_prefix>.user_agent.<user_agent>.[base_stat]
  name_regex_pairs.push_back({HTTP_USER_AGENT, "^http(?=\\.).*?\\.user_agent\\.((.*?)\\.)\\w+?$"});

  // listener.<port>.ssl.cipher.<cipher>
  name_regex_pairs.push_back({SSL_CIPHER, "^listener(?=\\.).*?\\.ssl\\.cipher(\\.(.*?))$"});

  // mongo.<stat_prefix>.cmd.<cmd>.[base_stat]
  name_regex_pairs.push_back({MONGO_CMD, "^mongo(?=\\.).*?\\.cmd\\.((.*?)\\.)\\w+?$"});

  // mongo.<stat_prefix>.collection.<collection>.query.[base_stat]
  name_regex_pairs.push_back(
      {MONGO_COLLECTION, "^mongo(?=\\.).*?\\.collection\\.((.*?)\\.).*?query.\\w+?$"});

  // mongo.<stat_prefix>.collection.<collection>.callsite.<callsite>.query.[base_stat]
  name_regex_pairs.push_back(
      {MONGO_CALLSITE, "^mongo(?=\\.).*?\\.callsite\\.((.*?)\\.).*?query.\\w+?$"});

  // http.<stat_prefix>.fault.<downstream-cluster>.
  name_regex_pairs.push_back({FAULT_DOWNSTREAM_CLUSTER, "^http(?=\\.).*?\\.fault\\.((.*?)\\.)"});

  // http.<stat_prefix>.dynamodb.(operation or table.<table_name>.capacity).<operation_name>.
  name_regex_pairs.push_back(
      {DYNAMO_OPERATION,
       "^http(?=\\.).*?\\.dynamodb.(?:operation|table(?=\\.).*?\\.capacity)(\\.(.*?))(?:\\.|$)"});

  // http.<stat_prefix>.dynamodb.(table or error).<table_name>.
  name_regex_pairs.push_back(
      {DYNAMO_TABLE, "^http(?=\\.).*?\\.dynamodb.(?:table|error)\\.((.*?)\\.)"});

  // http.<stat_prefix>.dynamodb.table.<table_name>.capacity.<operation_name>.__partition_id=<last_seven_characters_from_partition_id>
  name_regex_pairs.push_back(
      {DYNAMO_PARTITION_ID, "^http(?=\\.).*?\\.dynamodb\\..+?(\\.__partition_id=(\\w{7}))$"});

  // cluster.<route target cluster>.grpc.<grpc service>.
  name_regex_pairs.push_back({GRPC_BRIDGE_SERVICE, "^cluster(?=\\.).*?\\.grpc\\.((.*?)\\.)"});

  // cluster.<route target cluster>.grpc.<grpc service>.<grpc method>.[base_stat]
  name_regex_pairs.push_back(
      {GRPC_BRIDGE_METHOD, "^cluster(?=\\.).*?\\.grpc(?=\\.).*\\.((.*?)\\.)\\w+?$"});

  // vhost.<virtual host name>.vcluster.<virtual cluster name>.[base_stat]
  name_regex_pairs.push_back({VIRTUAL_CLUSTER, "^vhost(?=\\.).*?\\.vcluster\\.((.*?)\\.)\\w+?$"});

  // *_rq_<response_code>
  name_regex_pairs.push_back({RESPONSE_CODE, "_rq(_(\\d{3}))$"});

  // *_rq_<response_code_class>xx
  name_regex_pairs.push_back({RESPONSE_CODE_CLASS, "_rq(_(\\dxx))$"});

  return name_regex_pairs;
}

} // namespace Config
} // namespace Envoy
