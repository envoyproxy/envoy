#include "contrib/mysql_proxy/filters/network/source/mysql_codec_switch_resp.h"

#include "envoy/buffer/buffer.h"

#include "source/common/common/logger.h"

#include "contrib/mysql_proxy/filters/network/source/mysql_codec.h"
#include "contrib/mysql_proxy/filters/network/source/mysql_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

DecodeStatus ClientSwitchResponse::parseMessage(Buffer::Instance& buffer, uint32_t remain_len) {
  if (BufferHelper::readVectorBySize(buffer, remain_len, auth_plugin_resp_) !=
      DecodeStatus::Success) {
    ENVOY_LOG(debug, "error when parsing auth plugin data of client switch response");
    return DecodeStatus::Failure;
  }
  return DecodeStatus::Success;
}

void ClientSwitchResponse::encode(Buffer::Instance& out) const {
  BufferHelper::addVector(out, auth_plugin_resp_);
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
