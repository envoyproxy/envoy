#include "common/config/well_known_names.h"

namespace Envoy {
namespace Config {

std::unordered_map<std::string, std::string> TagNameValues::getRegexMapping() {
  std::unordered_map<std::string, std::string> regex_mapping;

  // cluster.<cluster_name>.
  regex_mapping[CLUSTER_NAME] = "^cluster\\.((.*?)\\.)";

  // listener.<port>.
  regex_mapping[LISTENER_PORT] = "^listener\\.((\\d+?)\\.)";

  // http.<stat_prefix>.
  regex_mapping[HTTP_CONN_MANAGER_PREFIX] = "^http\\.((.*?)\\.)";

  // http.<stat_prefix>.user_agent.<user_agent>.[base_stat] =
  regex_mapping[HTTP_USER_AGENT] = "^http(?=\\.).*?\\.user_agent\\.((.+?)\\.)\\w+?$";

  // listener.<port>.ssl.cipher.<cipher>
  regex_mapping[SSL_CIPHER] = "^listener(?=\\.).*?\\.ssl\\.cipher(\\.(.+?))$";

  // auth.clientssl.<stat_prefix>.
  regex_mapping[CLIENTSSL_PREFIX] = "^auth\\.clientssl\\.((.+?)\\.)";

  // mongo.<stat_prefix>.
  regex_mapping[MONGO_PREFIX] = "^mongo\\.((.*?)\\.)";

  // mongo.<stat_prefix>.cmd.<cmd>.[base_stat] =
  regex_mapping[MONGO_CMD] = "^mongo(?=\\.).*?\\.cmd\\.((.+?)\\.)\\w+?$";

  // mongo.<stat_prefix>.collection.<collection>.query.[base_stat] =
  regex_mapping[MONGO_COLLECTION] = "^mongo(?=\\.).*?\\.collection\\.((.+?)\\.).*?query.\\w+?$";

  // mongo.<stat_prefix>.collection.<collection>.callsite.<callsite>.query.[base_stat] =
  regex_mapping[MONGO_CALLSITE] = "^mongo(?=\\.).*?\\.callsite\\.((.+?)\\.).*?query.\\w+?$";

  // ratelimit.<stat_prefix>.
  regex_mapping[RATELIMIT_PREFIX] = "^ratelimit\\.((.*?)\\.)";

  // tcp.<stat_prefix>.
  regex_mapping[TCP_PREFIX] = "^tcp\\.((.*?)\\.)";

  // http.<stat_prefix>.fault.<downstream-cluster>.
  regex_mapping[FAULT_DOWNSTREAM_CLUSTER] = "^http(?=\\.).*?\\.fault\\.((.+?)\\.)";

  // http.<stat_prefix>.dynamodb.(operation or table.<table_name>.capacity).<operation_name>.
  regex_mapping[DYNAMO_OPERATION] =
      "^http(?=\\.).*?\\.dynamodb.(?:operation|table(?=\\.).*?\\.capacity)(\\.(.+?))(?:\\."
      "|$)";

  // http.<stat_prefix>.dynamodb.(table or error).<table_name>.
  regex_mapping[DYNAMO_TABLE] = "^http(?=\\.).*?\\.dynamodb.(?:table|error)\\.((.+?)\\.)";

  // http.<stat_prefix>.dynamodb.table.<table_name>.capacity.<operation_name>.__partition_id=<last_seven_characters_from_partition_id>
  regex_mapping[DYNAMO_PARTITION_ID] =
      "^http(?=\\.).*?\\.dynamodb\\..+?(\\.__partition_id=(\\w{7}))$";

  // cluster.<route target cluster>.grpc.<grpc service>.
  regex_mapping[GRPC_BRIDGE_SERVICE] = "^cluster(?=\\.).*?\\.grpc\\.((.+?)\\.)";

  // cluster.<route target cluster>.grpc.<grpc service>.<grpc method>.[base_stat] =
  regex_mapping[GRPC_BRIDGE_METHOD] = "^cluster(?=\\.).*?\\.grpc(?=\\.).*\\.((.+?)\\.)\\w+?$";

  // vhost.<virtual host name>.
  regex_mapping[VIRTUAL_HOST] = "^vhost\\.((.+?)\\.)";

  // vhost.<virtual host name>.vcluster.<virtual cluster name>.[base_stat] =
  regex_mapping[VIRTUAL_CLUSTER] = "^vhost(?=\\.).*?\\.vcluster\\.((.+?)\\.)\\w+?$";

  // *_rq_<response_code>
  regex_mapping[RESPONSE_CODE] = "_rq(_(\\d{3}))$";

  // *_rq_<response_code_class>xx
  regex_mapping[RESPONSE_CODE_CLASS] = "_rq(_(\\dxx))$";

  return regex_mapping;
}

} // namespace Config
} // namespace Envoy
