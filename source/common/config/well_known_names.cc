#include "common/config/well_known_names.h"

namespace Envoy {
namespace Config {

TagNameValues::TagNameValues() {
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
  addRegex(RESPONSE_CODE, "_rq(_(\\d{3}))$", "_rq_");

  // *_rq_(<response_code_class>)xx
  addRegex(RESPONSE_CODE_CLASS, "_rq_(\\d)xx$", "_rq_");

  // http.[<stat_prefix>.]dynamodb.table.[<table_name>.]capacity.[<operation_name>.](__partition_id=<last_seven_characters_from_partition_id>)
  addRegex(DYNAMO_PARTITION_ID,
           "^http(?=\\.).*?\\.dynamodb\\.table(?=\\.).*?\\."
           "capacity(?=\\.).*?(\\.__partition_id=(\\w{7}))$",
           ".dynamodb.table.");

  // http.[<stat_prefix>.]dynamodb.operation.(<operation_name>.)<base_stat> or
  // http.[<stat_prefix>.]dynamodb.table.[<table_name>.]capacity.(<operation_name>.)[<partition_id>]
  addRegex(DYNAMO_OPERATION,
           "^http(?=\\.).*?\\.dynamodb.(?:operation|table(?="
           "\\.).*?\\.capacity)(\\.(.*?))(?:\\.|$)",
           ".dynamodb.");

  // mongo.[<stat_prefix>.]collection.[<collection>.]callsite.(<callsite>.)query.<base_stat>
  addRegex(MONGO_CALLSITE,
           R"(^mongo(?=\.).*?\.collection(?=\.).*?\.callsite\.((.*?)\.).*?query.\w+?$)",
           ".collection.");

  // http.[<stat_prefix>.]dynamodb.table.(<table_name>.) or
  // http.[<stat_prefix>.]dynamodb.error.(<table_name>.)*
  addRegex(DYNAMO_TABLE, R"(^http(?=\.).*?\.dynamodb.(?:table|error)\.((.*?)\.))", ".dynamodb.");

  // mongo.[<stat_prefix>.]collection.(<collection>.)query.<base_stat>
  addRegex(MONGO_COLLECTION, R"(^mongo(?=\.).*?\.collection\.((.*?)\.).*?query.\w+?$)",
           ".collection.");

  // mongo.[<stat_prefix>.]cmd.(<cmd>.)<base_stat>
  addRegex(MONGO_CMD, R"(^mongo(?=\.).*?\.cmd\.((.*?)\.)\w+?$)", ".cmd.");

  // cluster.[<route_target_cluster>.]grpc.[<grpc_service>.](<grpc_method>.)<base_stat>
  addRegex(GRPC_BRIDGE_METHOD, R"(^cluster(?=\.).*?\.grpc(?=\.).*\.((.*?)\.)\w+?$)", ".grpc.");

  // http.[<stat_prefix>.]user_agent.(<user_agent>.)<base_stat>
  addRegex(HTTP_USER_AGENT, R"(^http(?=\.).*?\.user_agent\.((.*?)\.)\w+?$)", ".user_agent.");

  // vhost.[<virtual host name>.]vcluster.(<virtual_cluster_name>.)<base_stat>
  addRegex(VIRTUAL_CLUSTER, R"(^vhost(?=\.).*?\.vcluster\.((.*?)\.)\w+?$)", ".vcluster.");

  // http.[<stat_prefix>.]fault.(<downstream_cluster>.)<base_stat>
  addRegex(FAULT_DOWNSTREAM_CLUSTER, R"(^http(?=\.).*?\.fault\.((.*?)\.)\w+?$)", ".fault.");

  // listener.[<address>.]ssl.cipher.(<cipher>)
  addRegex(SSL_CIPHER, R"(^listener(?=\.).*?\.ssl\.cipher(\.(.*?))$)");

  // cluster.[<cluster_name>.]ssl.ciphers.(<cipher>)
  addRegex(SSL_CIPHER_SUITE, R"(^cluster(?=\.).*?\.ssl\.ciphers(\.(.*?))$)", ".ssl.ciphers.");

  // cluster.[<route_target_cluster>.]grpc.(<grpc_service>.)*
  addRegex(GRPC_BRIDGE_SERVICE, R"(^cluster(?=\.).*?\.grpc\.((.*?)\.))", ".grpc.");

  // tcp.(<stat_prefix>.)<base_stat>
  addRegex(TCP_PREFIX, R"(^tcp\.((.*?)\.)\w+?$)");

  // auth.clientssl.(<stat_prefix>.)<base_stat>
  addRegex(CLIENTSSL_PREFIX, R"(^auth\.clientssl\.((.*?)\.)\w+?$)");

  // ratelimit.(<stat_prefix>.)<base_stat>
  addRegex(RATELIMIT_PREFIX, R"(^ratelimit\.((.*?)\.)\w+?$)");

  // cluster.(<cluster_name>.)*
  addRegex(CLUSTER_NAME, "^cluster\\.((.*?)\\.)");

  // listener.[<address>.]http.(<stat_prefix>.)*
  addRegex(HTTP_CONN_MANAGER_PREFIX, R"(^listener(?=\.).*?\.http\.((.*?)\.))", ".http.");

  // http.(<stat_prefix>.)*
  addRegex(HTTP_CONN_MANAGER_PREFIX, "^http\\.((.*?)\\.)");

  // listener.(<address>.)*
  addRegex(LISTENER_ADDRESS,
           R"(^listener\.(((?:[_.[:digit:]]*|[_\[\]aAbBcCdDeEfF[:digit:]]*))\.))");

  // vhost.(<virtual host name>.)*
  addRegex(VIRTUAL_HOST, "^vhost\\.((.*?)\\.)");

  // mongo.(<stat_prefix>.)*
  addRegex(MONGO_PREFIX, "^mongo\\.((.*?)\\.)");

  // http.[<stat_prefix>.]rds.(<route_config_name>.)<base_stat>
  addRegex(RDS_ROUTE_CONFIG, R"(^http(?=\.).*?\.rds\.((.*?)\.)\w+?$)", ".rds.");

  // listener_manager.(worker_<id>.)*
  addRegex(WORKER_ID, R"(^listener_manager\.((worker_\d+)\.))", "listener_manager.worker_");
}

void TagNameValues::addRegex(const std::string& name, const std::string& regex,
                             const std::string& substr) {
  descriptor_vec_.emplace_back(Descriptor(name, regex, substr));
}

} // namespace Config
} // namespace Envoy
