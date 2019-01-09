#pragma once

namespace http2 {

template <typename T, typename... Args>
std::unique_ptr<T> Http2MakeUniqueImpl(Args&&... args) {
  return nullptr;
}

}  // namespace http2
