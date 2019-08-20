#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Common {
namespace Crypto {

class CryptoObject {
public:
  virtual ~CryptoObject() = default;
};

using CryptoObjectPtr = std::unique_ptr<CryptoObject>;

namespace Access {

template <class T> T* getTyped(CryptoObjectPtr& cryptoPtr) {
  return dynamic_cast<T*>(cryptoPtr.get());
}

} // namespace Access

} // namespace Crypto
} // namespace Common
} // namespace Envoy
