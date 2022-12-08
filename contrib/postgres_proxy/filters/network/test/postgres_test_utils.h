#pragma once

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

void createPostgresMsg(Buffer::Instance& data, std::string type, std::string payload = "");
void createInitialPostgresRequest(Buffer::Instance& data);

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
