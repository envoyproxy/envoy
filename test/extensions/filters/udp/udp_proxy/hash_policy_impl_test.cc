#include <string>

#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"

#include "source/common/common/hash.h"
#include "source/common/network/utility.h"
#include "source/extensions/filters/udp/udp_proxy/hash_policy_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace {

using namespace envoy::extensions::filters::udp::udp_proxy::v3;

class HashPolicyImplBaseTest : public testing::Test {
public:
  HashPolicyImplBaseTest()
      : HashPolicyImplBaseTest(
            Network::Utility::parseInternetAddressAndPortNoThrow("10.0.0.1:1000")) {}

  HashPolicyImplBaseTest(Network::Address::InstanceConstSharedPtr&& peer_address)
      : peer_address_(std::move(peer_address)) {}

  void setup() {
    hash_policy_config_ = config_.add_hash_policies();
    hash_policy_config_->clear_policy_specifier();
    additionalSetup();

    hash_policy_ = std::make_unique<HashPolicyImpl>(config_.hash_policies());
  }

  virtual void additionalSetup(){
      // Nothing to do here.
  };

  std::unique_ptr<const HashPolicyImpl> hash_policy_;
  UdpProxyConfig config_;
  UdpProxyConfig::HashPolicy* hash_policy_config_;
  const Network::Address::InstanceConstSharedPtr peer_address_;
};

class HashPolicyImplSourceIpTest : public HashPolicyImplBaseTest {
public:
  HashPolicyImplSourceIpTest() : pipe_address_(*Network::Utility::resolveUrl("unix://test_pipe")) {}

  void additionalSetup() override { hash_policy_config_->set_source_ip(true); }

  const Network::Address::InstanceConstSharedPtr pipe_address_;
};

class HashPolicyImplKeyTest : public HashPolicyImplBaseTest {
public:
  HashPolicyImplKeyTest() : key_("key") {}

  void additionalSetup() override { hash_policy_config_->set_key(key_); }

  const std::string key_;
};

// Check invalid policy type
TEST_F(HashPolicyImplBaseTest, NotSupportedPolicy) {
  EXPECT_DEATH(setup(), ".*panic: corrupted enum.*");
}

// Check if generate correct hash
TEST_F(HashPolicyImplSourceIpTest, SourceIpHash) {
  setup();

  auto generated_hash = HashUtil::xxHash64(peer_address_->ip()->addressAsString());
  auto hash = hash_policy_->generateHash(*peer_address_);

  EXPECT_EQ(generated_hash, hash.value());
}

// Check that returns null hash in case of unix domain socket(pipe) type
TEST_F(HashPolicyImplSourceIpTest, SourceIpWithUnixDomainSocketType) {
  setup();

  auto hash = hash_policy_->generateHash(*pipe_address_);

  EXPECT_FALSE(hash.has_value());
}

// Check if generate correct hash
TEST_F(HashPolicyImplKeyTest, KeyHash) {
  setup();

  auto generated_hash = HashUtil::xxHash64(key_);
  auto hash = hash_policy_->generateHash(*peer_address_);

  EXPECT_EQ(generated_hash, hash.value());
}

} // namespace
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
