#include "printers.h"
#include "test/test_common/printers.h"

#include <iostream>

#include "common/buffer/buffer_impl.h"
#include "common/http/header_map_impl.h"

namespace Envoy {
namespace Http {
// NOLINTNEXTLINE(readability-identifier-naming)
void PrintTo(const HeaderMapImpl& headers, std::ostream* os) {
  headers.iterate([os](const HeaderEntry& header) -> HeaderMap::Iterate {
    *os << "{'" << header.key().getStringView() << "','" << header.value().getStringView() << "'}";
    return HeaderMap::Iterate::Continue;
  });
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

namespace Network {
namespace Address {
void PrintTo(const Instance& address, std::ostream* os) { *os << address.asString(); }
} // namespace Address
} // namespace Network
} // namespace Envoy
