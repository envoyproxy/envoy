#pragma once

#include <memory>

namespace Envoy {
namespace Common {
namespace Crypto {

class CryptoObject {
public:
  virtual ~CryptoObject() = default;
};

using CryptoObjectPtr = std::unique_ptr<CryptoObject>;

namespace Access {

template <class T> T* getTyped(CryptoObject& crypto) { return dynamic_cast<T*>(&crypto); }

} // namespace Access

} // namespace Crypto
} // namespace Common
} // namespace Envoy
