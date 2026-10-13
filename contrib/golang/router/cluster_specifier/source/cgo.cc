#include "contrib/golang/router/cluster_specifier/source/golang_cluster_specifier.h"

namespace Envoy {
namespace Router {
namespace Golang {

//
// These functions should only be invoked in the current Envoy worker thread.
//

enum GetHeaderResult {
  Mising = 0,
  Found = 1,
};

absl::string_view referGoString(void* str) {
  if (str == nullptr) {
    return "";
  }
  auto go_str = reinterpret_cast<GoString*>(str);
  return absl::string_view(go_str->p, go_str->n); // NOLINT(modernize-return-braced-init-list)
}

#ifdef __cplusplus
extern "C" {
#endif

// Assigns the number of request headers and their total byte size to the provided uint64 pointers.
void envoyGoClusterSpecifierGetNumHeadersAndByteSize(unsigned long long header_ptr,
                                                     void* header_num, void* byte_size) {
  auto header = reinterpret_cast<Http::RequestHeaderMap*>(header_ptr);
  auto go_header_num = reinterpret_cast<GoUint64*>(header_num);
  auto go_byte_size = reinterpret_cast<GoUint64*>(byte_size);
  *go_header_num = header->size();
  *go_byte_size = header->byteSize();
}

// Get the value of the specified header key from the request header map.
// Only use the first value when there are multiple values associated with the key.
int envoyGoClusterSpecifierGetHeader(unsigned long long header_ptr, void* key, void* value) {
  auto header = reinterpret_cast<Http::RequestHeaderMap*>(header_ptr);
  auto key_str = referGoString(key);
  auto go_value = reinterpret_cast<GoString*>(value);
  auto result = header->get(Http::LowerCaseString(key_str));

  if (!result.empty()) {
    auto str = result[0]->value().getStringView();
    go_value->p = str.data();
    go_value->n = str.length();
    return static_cast<int>(GetHeaderResult::Found);
  }
  return static_cast<int>(GetHeaderResult::Mising);
}

// Copies the request headers from `header_ptr` into the provided buffer `buf` and populates `strs`
// with the string metadata of each key and value. `strs` points into `buf` which stores
// concatenated raw header bytes in the format `key1val1key2val2...`. Note the buffer should
// be allocated with sufficient memory to store the headers and `strs` is expected to be of length
// twice the number of headers.
void envoyGoClusterSpecifierGetAllHeaders(unsigned long long header_ptr, void* strs, void* buf) {
  auto headers = reinterpret_cast<Http::RequestHeaderMap*>(header_ptr);
  auto go_strs = reinterpret_cast<GoString*>(strs);
  auto go_buf = reinterpret_cast<char*>(buf);
  auto i = 0;
  headers->iterate(
      [&i, &go_strs, &go_buf](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
        auto key = header.key().getStringView();
        auto value = header.value().getStringView();

        auto len = key.length();
        go_strs[i].n = len;
        go_strs[i].p = go_buf;
        // go_buf is allocated in go heap memory with sufficient space for all headers.
        memcpy(go_buf, key.data(), len); // NOLINT(safe-memcpy)
        go_buf += len;
        i++;

        len = value.length();
        go_strs[i].n = len;
        if (len > 0) {
          go_strs[i].p = go_buf;
          memcpy(go_buf, value.data(), len); // NOLINT(safe-memcpy)
          go_buf += len;
        }
        i++;
        return Http::HeaderMap::Iterate::Continue;
      });
}

// Log the message with the error level.
void envoyGoClusterSpecifierLogError(unsigned long long plugin_ptr, void* msg) {
  auto msgStr = referGoString(msg);
  auto plugin = reinterpret_cast<GolangClusterSpecifierPlugin*>(plugin_ptr);
  plugin->log(msgStr);
}

#ifdef __cplusplus
}
#endif

} // namespace Golang
} // namespace Router
} // namespace Envoy
