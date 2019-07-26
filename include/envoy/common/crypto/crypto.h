#pragma once

#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Common {
namespace Crypto {

class CryptoObject {
public:
  virtual ~CryptoObject() = default;
};

using CryptoObjectUniquePtr = std::unique_ptr<CryptoObject>;

namespace Access {

template <class T> T* getTyped(CryptoObjectUniquePtr* cryptoPtr) {
  return static_cast<T*>(cryptoPtr->get());
}

} // namespace Access

} // namespace Crypto
} // namespace Common
} // namespace Envoy
