#include "source/common/quic/envoy_quic_session_cache.h"

#include "absl/strings/escaping.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {
namespace {

constexpr uint32_t Timeout = 1000;
const quic::QuicVersionLabel FakeVersionLabel = 0x01234567;
const quic::QuicVersionLabel FakeVersionLabel2 = 0x89ABCDEF;
const uint64_t FakeIdleTimeoutMilliseconds = 12012;
const uint8_t FakeStatelessResetTokenData[16] = {0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
                                                 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F};
const uint64_t FakeMaxPacketSize = 9001;
const uint64_t FakeInitialMaxData = 101;
const bool FakeDisableMigration = true;
const auto CustomParameter1 = static_cast<quic::TransportParameters::TransportParameterId>(0xffcd);
const char* CustomParameter1Value = "foo";
const auto CustomParameter2 = static_cast<quic::TransportParameters::TransportParameterId>(0xff34);
const char* CustomParameter2Value = "bar";

std::vector<uint8_t> createFakeStatelessResetToken() {
  return std::vector<uint8_t>(FakeStatelessResetTokenData,
                              FakeStatelessResetTokenData + sizeof(FakeStatelessResetTokenData));
}

// Make a TransportParameters that has a few fields set to help test comparison.
std::unique_ptr<quic::TransportParameters> makeFakeTransportParams() {
  auto params = std::make_unique<quic::TransportParameters>();
  params->perspective = quic::Perspective::IS_CLIENT;
  params->version = FakeVersionLabel;
  params->supported_versions.push_back(FakeVersionLabel);
  params->supported_versions.push_back(FakeVersionLabel2);
  params->max_idle_timeout_ms.set_value(FakeIdleTimeoutMilliseconds);
  params->stateless_reset_token = createFakeStatelessResetToken();
  params->max_udp_payload_size.set_value(FakeMaxPacketSize);
  params->initial_max_data.set_value(FakeInitialMaxData);
  params->disable_active_migration = FakeDisableMigration;
  params->custom_parameters[CustomParameter1] = CustomParameter1Value;
  params->custom_parameters[CustomParameter2] = CustomParameter2Value;
  return params;
}

// Generated with SSL_SESSION_to_bytes.
static constexpr char CachedSession[] =
    "3082068702010102020304040213010420b9c2a657e565db0babd09e192a9fc4d768fbd706"
    "9f03f9278a4a0be62392e55b0420d87ed2ab8cafc986fd2e288bd2d654cd57c3a2bed1d532"
    "20726e55fed39d021ea10602045ed16771a205020302a300a382025f3082025b30820143a0"
    "03020102020104300d06092a864886f70d01010b0500302c3110300e060355040a13074163"
    "6d6520436f311830160603550403130f496e7465726d656469617465204341301e170d3133"
    "303130313130303030305a170d3233313233313130303030305a302d3110300e060355040a"
    "130741636d6520436f3119301706035504031310746573742e6578616d706c652e636f6d30"
    "59301306072a8648ce3d020106082a8648ce3d030107034200040526220e77278300d06bc0"
    "86aff4f999a828a2ed5cc75adc2972794befe885aa3a9b843de321b36b0a795289cebff1a5"
    "428bad5e34665ce5e36daad08fb3ffd8a3523050300e0603551d0f0101ff04040302078030"
    "130603551d25040c300a06082b06010505070301300c0603551d130101ff04023000301b06"
    "03551d11041430128210746573742e6578616d706c652e636f6d300d06092a864886f70d01"
    "010b050003820101008c1f1e380831b6437a8b9284d28d4ead38d9503a9fc936db89048aa2"
    "edd6ec2fb830d962ef7a4f384e679504f4d5520f3272e0b9e702b110aff31711578fa5aeb1"
    "11e9d184c994b0f97e7b17d1995f3f477f25bc1258398ec0ec729caed55d594a009f48093a"
    "17f33a7f3bb6e420cc3499838398a421d93c7132efa8bee5ed2645cbc55179c400da006feb"
    "761badd356cac3bd7a0e6b22a511106a355ec62a4c0ac2541d2996adb4a918c866d10c3e31"
    "62039a91d4ce600b276740d833380b37f66866d261bf6efa8855e7ae6c7d12a8a864cd9a1f"
    "4663e07714b0204e51bbc189a2d04c2a5043202379ff1c8cbf30cbb44fde4ee9a1c0c976dc"
    "4943df2c132ca4020400aa7f047d494e534543555245003072020101020203040402130104"
    "000420d87ed2ab8cafc986fd2e288bd2d654cd57c3a2bed1d53220726e55fed39d021ea106"
    "02045ed16771a205020302a300a4020400b20302011db5060404bd909308b807020500ffff"
    "ffffb9050203093a80ba07040568332d3238bb030101ffbc03040100b20302011db3820307"
    "30820303308201eba003020102020102300d06092a864886f70d01010b050030243110300e"
    "060355040a130741636d6520436f3110300e06035504031307526f6f74204341301e170d31"
    "33303130313130303030305a170d3233313233313130303030305a302c3110300e06035504"
    "0a130741636d6520436f311830160603550403130f496e7465726d65646961746520434130"
    "820122300d06092a864886f70d01010105000382010f003082010a0282010100cd3550e70a"
    "6880e52bf0012b93110c50f723e1d8d2ed489aea3b649f82fae4ad2396a8a19b31d1d64ab2"
    "79f1c18003184154a5303a82bd57109cfd5d34fd19d3211bcb06e76640e1278998822dd72e"
    "0d5c059a740d45de325e784e81b4c86097f08b2a8ce057f6b9db5a53641d27e09347d993ee"
    "acf67be7d297b1a6853775ffaaf78fae924e300b5654fd32f99d3cd82e95f56417ff26d265"
    "e2b1786c835d67a4d8ae896b6eb34b35a5b1033c209779ed0bf8de25a13a507040ae9e0475"
    "a26a2f15845b08c3e0554e47dbbc7925b02e580dbcaaa6f2eecde6b8028c5b00b33d44d0a6"
    "bfb3e72e9d4670de45d1bd79bdc0f2470b71286091c29873152db4b1f30203010001a33830"
    "36300e0603551d0f0101ff04040302020430130603551d25040c300a06082b060105050703"
    "01300f0603551d130101ff040530030101ff300d06092a864886f70d01010b050003820101"
    "00bc4f8234860558dd404a626403819bfc759029d625a002143e75ebdb2898d1befdd326c3"
    "4b14dc3507d732bb29af7e6af31552db53052a2be0d950efee5e0f699304231611ed8bf73a"
    "6f216a904c6c2f1a2186d1ed08a8005a7914394d71e7d4b643c808f86365c5fecad8b52934"
    "2d3b3f03447126d278d75b1dab3ed53f23e36e9b3d695f28727916e5ee56ce22d387c81f05"
    "919b2a37bd4981eb67d9f57b7072285dbbb61f48b6b14768c069a092aad5a094cf295dafd2"
    "3ca008f89a5f5ab37a56e5f68df45091c7cb85574677127087a2887ba3baa6d4fc436c6e40"
    "40885e81621d38974f0c7f0d792418c5adebb10e92a165f8d79b169617ff575c0d4a85b506"
    "0404bd909308b603010100b70402020403b807020500ffffffffb9050203093a80ba070405"
    "68332d3238bb030101ff";

class FakeTimeSource : public TimeSource {
public:
  // From TimeSource.
  SystemTime systemTime() override { return fake_time_; }
  MonotonicTime monotonicTime() override {
    // EnvoyQuicSessionCache does not use monotonic time, return empty value.
    ADD_FAILURE() << "Unexpected call to monotonicTime";
    return MonotonicTime();
  }

  void advance(int seconds) { fake_time_ += std::chrono::seconds(seconds); }

private:
  SystemTime fake_time_{};
};

} // namespace

class EnvoyQuicSessionCacheTest : public ::testing::Test {
public:
  EnvoyQuicSessionCacheTest()
      : ssl_ctx_(SSL_CTX_new(TLS_method())), cache_(time_source_),
        params_(makeFakeTransportParams()) {}

protected:
  bssl::UniquePtr<SSL_SESSION> makeSession(uint32_t timeout = Timeout) {
    std::string cached_session =
        absl::HexStringToBytes(absl::string_view(CachedSession, sizeof(CachedSession)));
    SSL_SESSION* session =
        SSL_SESSION_from_bytes(reinterpret_cast<const uint8_t*>(cached_session.data()),
                               cached_session.size(), ssl_ctx_.get());
    SSL_SESSION_set_time(session, std::chrono::system_clock::to_time_t(time_source_.systemTime()));
    SSL_SESSION_set_timeout(session, timeout);
    return bssl::UniquePtr<SSL_SESSION>(session);
  }

  bssl::UniquePtr<SSL_CTX> ssl_ctx_;
  FakeTimeSource time_source_;
  EnvoyQuicSessionCache cache_;
  std::unique_ptr<quic::TransportParameters> params_;
};

// Tests that simple insertion and lookup work correctly.
TEST_F(EnvoyQuicSessionCacheTest, SingleSession) {
  bssl::UniquePtr<SSL_SESSION> session = makeSession();
  quic::QuicServerId id1("a.com", 443);

  std::unique_ptr<quic::TransportParameters> params2 = makeFakeTransportParams();
  bssl::UniquePtr<SSL_SESSION> session2 = makeSession();
  SSL_SESSION* unowned2 = session2.get();
  quic::QuicServerId id2("b.com", 443);

  EXPECT_EQ(nullptr, cache_.Lookup(id1, ssl_ctx_.get()));
  EXPECT_EQ(nullptr, cache_.Lookup(id2, ssl_ctx_.get()));
  EXPECT_EQ(0u, cache_.size());

  cache_.Insert(id1, std::move(session), *params_, nullptr);
  EXPECT_EQ(1u, cache_.size());
  std::unique_ptr<quic::QuicResumptionState> resumption_state = cache_.Lookup(id1, ssl_ctx_.get());
  ASSERT_NE(resumption_state, nullptr);
  ASSERT_NE(resumption_state->transport_params, nullptr);
  EXPECT_EQ(*params_, *(resumption_state->transport_params));
  EXPECT_EQ(nullptr, cache_.Lookup(id2, ssl_ctx_.get()));
  // No session is available for id1, even though the entry exists.
  EXPECT_EQ(1u, cache_.size());
  EXPECT_EQ(nullptr, cache_.Lookup(id1, ssl_ctx_.get()));
  // Lookup() will trigger a deletion of invalid entry.
  EXPECT_EQ(0u, cache_.size());

  bssl::UniquePtr<SSL_SESSION> session3 = makeSession();
  SSL_SESSION* unowned3 = session3.get();
  quic::QuicServerId id3("c.com", 443);
  cache_.Insert(id3, std::move(session3), *params_, nullptr);
  cache_.Insert(id2, std::move(session2), *params2, nullptr);
  EXPECT_EQ(2u, cache_.size());
  std::unique_ptr<quic::QuicResumptionState> resumption_state2 = cache_.Lookup(id2, ssl_ctx_.get());
  ASSERT_NE(resumption_state2, nullptr);
  EXPECT_EQ(unowned2, resumption_state2->tls_session.get());
  std::unique_ptr<quic::QuicResumptionState> resumption_state3 = cache_.Lookup(id3, ssl_ctx_.get());
  ASSERT_NE(resumption_state3, nullptr);
  EXPECT_EQ(unowned3, resumption_state3->tls_session.get());

  // Verify that the cache is cleared after Lookups.
  EXPECT_EQ(nullptr, cache_.Lookup(id1, ssl_ctx_.get()));
  EXPECT_EQ(nullptr, cache_.Lookup(id2, ssl_ctx_.get()));
  EXPECT_EQ(nullptr, cache_.Lookup(id3, ssl_ctx_.get()));
  EXPECT_EQ(0u, cache_.size());
}

TEST_F(EnvoyQuicSessionCacheTest, MultipleSessions) {
  bssl::UniquePtr<SSL_SESSION> session = makeSession();
  quic::QuicServerId id1("a.com", 443);
  bssl::UniquePtr<SSL_SESSION> session2 = makeSession();
  SSL_SESSION* unowned2 = session2.get();
  bssl::UniquePtr<SSL_SESSION> session3 = makeSession();
  SSL_SESSION* unowned3 = session3.get();

  cache_.Insert(id1, std::move(session), *params_, nullptr);
  cache_.Insert(id1, std::move(session2), *params_, nullptr);
  cache_.Insert(id1, std::move(session3), *params_, nullptr);
  // The latest session is popped first.
  std::unique_ptr<quic::QuicResumptionState> resumption_state1 = cache_.Lookup(id1, ssl_ctx_.get());
  ASSERT_NE(resumption_state1, nullptr);
  EXPECT_EQ(unowned3, resumption_state1->tls_session.get());
  std::unique_ptr<quic::QuicResumptionState> resumption_state2 = cache_.Lookup(id1, ssl_ctx_.get());
  ASSERT_NE(resumption_state2, nullptr);
  EXPECT_EQ(unowned2, resumption_state2->tls_session.get());
  // Only two sessions are cache.
  EXPECT_EQ(nullptr, cache_.Lookup(id1, ssl_ctx_.get()));
}

// Test that when a different TransportParameter is inserted for
// the same server id, the existing entry is removed.
TEST_F(EnvoyQuicSessionCacheTest, DifferentTransportParams) {
  bssl::UniquePtr<SSL_SESSION> session = makeSession();
  quic::QuicServerId id1("a.com", 443);
  bssl::UniquePtr<SSL_SESSION> session2 = makeSession();
  bssl::UniquePtr<SSL_SESSION> session3 = makeSession();
  SSL_SESSION* unowned3 = session3.get();

  cache_.Insert(id1, std::move(session), *params_, nullptr);
  cache_.Insert(id1, std::move(session2), *params_, nullptr);
  // tweak the transport parameters a little bit.
  params_->perspective = quic::Perspective::IS_SERVER;
  cache_.Insert(id1, std::move(session3), *params_, nullptr);
  std::unique_ptr<quic::QuicResumptionState> resumption_state = cache_.Lookup(id1, ssl_ctx_.get());
  ASSERT_NE(resumption_state, nullptr);
  EXPECT_EQ(unowned3, resumption_state->tls_session.get());
  EXPECT_EQ(*params_.get(), *resumption_state->transport_params);
  EXPECT_EQ(nullptr, cache_.Lookup(id1, ssl_ctx_.get()));
}

TEST_F(EnvoyQuicSessionCacheTest, DifferentApplicationState) {
  bssl::UniquePtr<SSL_SESSION> session = makeSession();
  quic::QuicServerId id1("a.com", 443);
  bssl::UniquePtr<SSL_SESSION> session2 = makeSession();
  bssl::UniquePtr<SSL_SESSION> session3 = makeSession();
  SSL_SESSION* unowned3 = session3.get();
  quic::ApplicationState state;
  state.push_back('a');

  cache_.Insert(id1, std::move(session), *params_, &state);
  cache_.Insert(id1, std::move(session2), *params_, &state);
  cache_.Insert(id1, std::move(session3), *params_, nullptr);
  std::unique_ptr<quic::QuicResumptionState> resumption_state = cache_.Lookup(id1, ssl_ctx_.get());
  ASSERT_NE(resumption_state, nullptr);
  EXPECT_EQ(unowned3, resumption_state->tls_session.get());
  EXPECT_EQ(nullptr, resumption_state->application_state);
  EXPECT_EQ(nullptr, cache_.Lookup(id1, ssl_ctx_.get()));
}

TEST_F(EnvoyQuicSessionCacheTest, BothStatesDifferent) {
  bssl::UniquePtr<SSL_SESSION> session = makeSession();
  quic::QuicServerId id1("a.com", 443);
  bssl::UniquePtr<SSL_SESSION> session2 = makeSession();
  bssl::UniquePtr<SSL_SESSION> session3 = makeSession();
  SSL_SESSION* unowned3 = session3.get();
  quic::ApplicationState state;
  state.push_back('a');

  cache_.Insert(id1, std::move(session), *params_, &state);
  cache_.Insert(id1, std::move(session2), *params_, &state);
  params_->perspective = quic::Perspective::IS_SERVER;
  cache_.Insert(id1, std::move(session3), *params_, nullptr);
  std::unique_ptr<quic::QuicResumptionState> resumption_state = cache_.Lookup(id1, ssl_ctx_.get());
  ASSERT_NE(resumption_state, nullptr);
  EXPECT_EQ(unowned3, resumption_state->tls_session.get());
  EXPECT_EQ(*params_, *resumption_state->transport_params);
  EXPECT_EQ(nullptr, resumption_state->application_state);
  EXPECT_EQ(nullptr, cache_.Lookup(id1, ssl_ctx_.get()));
}

// When the size limit is exceeded, the oldest entry should be erased.
TEST_F(EnvoyQuicSessionCacheTest, SizeLimit) {
  constexpr size_t size_limit = 1024;
  std::array<SSL_SESSION*, size_limit + 1> unowned_sessions;
  for (size_t i = 0; i <= size_limit; i++) {
    time_source_.advance(1);
    bssl::UniquePtr<SSL_SESSION> session = makeSession(/*timeout=*/10000);
    unowned_sessions[i] = session.get();
    quic::QuicServerId id(absl::StrCat("domain", i, ".example.com"), 443);
    cache_.Insert(id, std::move(session), *params_, nullptr);
  }
  EXPECT_EQ(cache_.size(), size_limit);
  // First entry has been removed.
  quic::QuicServerId id0("domain0.example.com", 443);
  EXPECT_EQ(nullptr, cache_.Lookup(id0, ssl_ctx_.get()));
  // All other entries all present.
  for (size_t i = 1; i <= size_limit; i++) {
    quic::QuicServerId id(absl::StrCat("domain", i, ".example.com"), 443);
    std::unique_ptr<quic::QuicResumptionState> resumption_state = cache_.Lookup(id, ssl_ctx_.get());
    ASSERT_NE(resumption_state, nullptr) << i;
    EXPECT_EQ(resumption_state->tls_session.get(), unowned_sessions[i]);
  }
}

TEST_F(EnvoyQuicSessionCacheTest, ClearEarlyData) {
  SSL_CTX_set_early_data_enabled(ssl_ctx_.get(), 1);
  bssl::UniquePtr<SSL_SESSION> session = makeSession();
  quic::QuicServerId id1("a.com", 443);
  bssl::UniquePtr<SSL_SESSION> session2 = makeSession();

  EXPECT_TRUE(SSL_SESSION_early_data_capable(session.get()));
  EXPECT_TRUE(SSL_SESSION_early_data_capable(session2.get()));

  cache_.Insert(id1, std::move(session), *params_, nullptr);
  cache_.Insert(id1, std::move(session2), *params_, nullptr);

  cache_.ClearEarlyData(id1);

  std::unique_ptr<quic::QuicResumptionState> resumption_state = cache_.Lookup(id1, ssl_ctx_.get());
  ASSERT_NE(resumption_state, nullptr);
  EXPECT_FALSE(SSL_SESSION_early_data_capable(resumption_state->tls_session.get()));
  resumption_state = cache_.Lookup(id1, ssl_ctx_.get());
  EXPECT_FALSE(SSL_SESSION_early_data_capable(resumption_state->tls_session.get()));
  EXPECT_EQ(nullptr, cache_.Lookup(id1, ssl_ctx_.get()));
}

// Expired session isn't considered valid and nullptr will be returned upon
// Lookup.
TEST_F(EnvoyQuicSessionCacheTest, Expiration) {
  bssl::UniquePtr<SSL_SESSION> session = makeSession();
  quic::QuicServerId id1("a.com", 443);

  bssl::UniquePtr<SSL_SESSION> session2 = makeSession(3 * Timeout);
  SSL_SESSION* unowned2 = session2.get();
  quic::QuicServerId id2("b.com", 443);

  cache_.Insert(id1, std::move(session), *params_, nullptr);
  cache_.Insert(id2, std::move(session2), *params_, nullptr);

  EXPECT_EQ(2u, cache_.size());
  // Expire the session.
  time_source_.advance(Timeout * 2);
  // The entry has not been removed yet.
  EXPECT_EQ(2u, cache_.size());

  EXPECT_EQ(nullptr, cache_.Lookup(id1, ssl_ctx_.get()));
  EXPECT_EQ(1u, cache_.size());
  std::unique_ptr<quic::QuicResumptionState> resumption_state = cache_.Lookup(id2, ssl_ctx_.get());
  ASSERT_NE(resumption_state, nullptr);
  EXPECT_EQ(unowned2, resumption_state->tls_session.get());
  EXPECT_EQ(1u, cache_.size());
}

} // namespace Quic
} // namespace Envoy
