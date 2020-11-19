#include "twem_cluster_lb.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

void TwemClusterThreadAwareLoadBalancer::Ring::doHash(const std::string& address_string, HashFunction , uint64_t i, std::vector<uint64_t> &hashes) {
  absl::InlinedVector<char, 196> hash_key_buffer;

  hash_key_buffer.assign(address_string.begin(), address_string.end());
  hash_key_buffer.emplace_back('-');

  auto offset_start = hash_key_buffer.end();

  for(int j = 0 ; j < 4; j++) {
    const std::string i_str = absl::StrCat("", i);
    hash_key_buffer.insert(offset_start, i_str.begin(), i_str.end());

    absl::string_view hash_key(static_cast<char*>(hash_key_buffer.data()), hash_key_buffer.size());
    uint64_t hash = TwemHash::hash(hash_key, j);

    ENVOY_LOG(trace, "ring hash: hash_key={} i={} align={} hash={}", hash_key.data(), i, j, hash);

    hashes.push_back(hash);

    hash_key_buffer.erase(offset_start, hash_key_buffer.end());
  }
}

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
