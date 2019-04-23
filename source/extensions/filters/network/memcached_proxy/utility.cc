#include "extensions/filters/network/memcached_proxy/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MemcachedProxy {

std::string BufferHelper::drainString(Buffer::Instance& buffer, uint32_t size) {
  return std::string(static_cast<char*>(buffer.linearize(index)));
}

}
}
}
}
