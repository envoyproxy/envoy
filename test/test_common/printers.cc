#include "printers.h"
#include "test/test_common/printers.h"

#include <iostream>

#include "envoy/redis/codec.h"

#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"

namespace Envoy {
namespace Http {
void PrintTo(const HeaderMapImpl& headers, std::ostream* os) {
  headers.iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        std::ostream* os = static_cast<std::ostream*>(context);
        *os << "{'" << header.key().c_str() << "','" << header.value().c_str() << "'}";
        return HeaderMap::Iterate::Continue;
      },
      os);
}

void PrintTo(const HeaderMapPtr& headers, std::ostream* os) {
  PrintTo(*dynamic_cast<HeaderMapImpl*>(headers.get()), os);
}

void PrintTo(const HeaderMap& headers, std::ostream* os) {
  PrintTo(*dynamic_cast<const HeaderMapImpl*>(&headers), os);
}
} // namespace Http

namespace Buffer {
void PrintTo(const Instance& buffer, std::ostream* os) {
  *os << "buffer with size=" << buffer.length();
}

void PrintTo(const Buffer::OwnedImpl& buffer, std::ostream* os) {
  PrintTo(dynamic_cast<const Buffer::Instance&>(buffer), os);
}
} // namespace Buffer

namespace Redis {
void PrintTo(const RespValue& value, std::ostream* os) { *os << value.toString(); }

void PrintTo(const RespValuePtr& value, std::ostream* os) { *os << value->toString(); }
} // namespace Redis
} // namespace Envoy
