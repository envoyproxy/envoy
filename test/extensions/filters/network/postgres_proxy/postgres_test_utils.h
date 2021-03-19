#include "common/buffer/buffer_impl.h"
#include "extensions/filters/network/postgres_proxy/postgres_decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

void createPostgresMsg(Buffer::Instance& data, std::string type, std::string payload = "");


template <typename T> class IntWriter : public Int<T> {
  public:

  bool write(const Buffer::Instance& data, uint64_t& , uint64_t&) {
  data.writeBEInt<T>(10);
  return true;
  }
};

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
