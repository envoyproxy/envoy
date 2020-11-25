#include "common/local_reply/local_reply.h"

#include <string>
#include <vector>

#include "envoy/api/api.h"

#include "common/access_log/access_log_impl.h"
#include "common/common/enum_to_int.h"
#include "common/config/datasource.h"
#include "common/formatter/substitution_format_string.h"
#include "common/formatter/substitution_formatter.h"
#include "common/http/header_map_impl.h"
#include "common/http/utility.h"
#include "common/response_map/response_map.h"
#include "common/router/header_parser.h"

namespace Envoy {
namespace LocalReply {

LocalReplyPtr Factory::createDefault() { return ResponseMap::Factory::createDefault(); }

LocalReplyPtr Factory::create(
    const envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig&
        config,
    Server::Configuration::FactoryContext& context) {
  return ResponseMap::Factory::create(config, context, context.messageValidationVisitor());
}

} // namespace LocalReply
} // namespace Envoy
