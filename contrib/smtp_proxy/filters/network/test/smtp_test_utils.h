#pragma once

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

void createSmtpMsg(Buffer::Instance& data, std::string type, std::string payload = "");
void createInitialSmtpRequest(Buffer::Instance& data);

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
