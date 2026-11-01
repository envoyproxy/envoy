#include "source/extensions/transport_sockets/dynamic_modules/splice_pump.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

#if defined(__linux__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/tls.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace DynamicModules {
namespace {

// TLS record and message type bytes, matching the pump's dispatch.
constexpr uint8_t Alert = 21;
constexpr uint8_t Handshake = 22;
constexpr uint8_t ApplicationData = 23;
constexpr uint8_t ChangeCipherSpec = 20;
constexpr uint8_t NewSessionTicket = 4;
constexpr uint8_t CloseNotify = 0;

// Builds a handshake record payload from coalesced messages, each a type byte, a 3 byte length, and
// a body of that length.
std::string handshakeRecord(const std::vector<std::pair<uint8_t, size_t>>& messages) {
  std::string out;
  for (const auto& [type, len] : messages) {
    out.push_back(static_cast<char>(type));
    out.push_back(static_cast<char>((len >> 16) & 0xff));
    out.push_back(static_cast<char>((len >> 8) & 0xff));
    out.push_back(static_cast<char>(len & 0xff));
    out.append(len, 'x');
  }
  return out;
}

ControlAction classify(uint8_t type, const std::string& data) {
  return classifyKtlsControlRecord(type, reinterpret_cast<const uint8_t*>(data.data()),
                                   data.size());
}

// Compares payloads without printing megabytes on a mismatch.
testing::AssertionResult samePayload(absl::string_view expected, absl::string_view actual) {
  if (expected == actual) {
    return testing::AssertionSuccess();
  }
  const size_t common = std::min(expected.size(), actual.size());
  size_t offset = 0;
  while (offset < common && expected[offset] == actual[offset]) {
    ++offset;
  }
  return testing::AssertionFailure() << "payloads differ at offset " << offset << ", expected "
                                     << expected.size() << " bytes, got " << actual.size();
}

TEST(ClassifyKtlsControlRecord, CloseNotifyAlertIsEof) {
  EXPECT_EQ(ControlAction::Eof, classify(Alert, std::string{1, static_cast<char>(CloseNotify)}));
}

TEST(ClassifyKtlsControlRecord, FatalAlertIsClose) {
  EXPECT_EQ(ControlAction::Close, classify(Alert, std::string{2, 40}));
}

TEST(ClassifyKtlsControlRecord, ShortAlertIsClose) {
  EXPECT_EQ(ControlAction::Close, classify(Alert, std::string{1}));
}

TEST(ClassifyKtlsControlRecord, ChangeCipherSpecIsRetry) {
  EXPECT_EQ(ControlAction::Retry, classify(ChangeCipherSpec, std::string{1}));
}

TEST(ClassifyKtlsControlRecord, NewSessionTicketIsRetry) {
  EXPECT_EQ(ControlAction::Retry, classify(Handshake, handshakeRecord({{NewSessionTicket, 16}})));
}

TEST(ClassifyKtlsControlRecord, CoalescedNewSessionTicketsIsRetry) {
  EXPECT_EQ(ControlAction::Retry,
            classify(Handshake, handshakeRecord({{NewSessionTicket, 4}, {NewSessionTicket, 8}})));
}

TEST(ClassifyKtlsControlRecord, TicketFollowedByOtherMessageIsClose) {
  EXPECT_EQ(ControlAction::Close,
            classify(Handshake, handshakeRecord({{NewSessionTicket, 4}, {1, 4}})));
}

TEST(ClassifyKtlsControlRecord, RenegotiationHandshakeIsClose) {
  // A ClientHello, message type 1, is a renegotiation the splice path cannot service.
  EXPECT_EQ(ControlAction::Close, classify(Handshake, handshakeRecord({{1, 4}})));
}

TEST(ClassifyKtlsControlRecord, TruncatedHandshakeHeaderIsClose) {
  EXPECT_EQ(ControlAction::Close,
            classify(Handshake, std::string{static_cast<char>(NewSessionTicket), 0}));
}

TEST(ClassifyKtlsControlRecord, OverlongHandshakeLengthIsClose) {
  const std::string data{static_cast<char>(NewSessionTicket), 0, 0, static_cast<char>(0xff)};
  EXPECT_EQ(ControlAction::Close, classify(Handshake, data));
}

TEST(ClassifyKtlsControlRecord, ApplicationDataIsClose) {
  EXPECT_EQ(ControlAction::Close, classify(ApplicationData, "data"));
}

TEST(ClassifyKtlsControlRecord, UnknownRecordIsClose) {
  EXPECT_EQ(ControlAction::Close, classify(99, "data"));
}

#if defined(__linux__)

// Connects two non-blocking loopback TCP sockets.
void connectPair(int& a, int& b) {
  const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(listener, 0);
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(0, ::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));
  ASSERT_EQ(0, ::listen(listener, 1));
  socklen_t len = sizeof(addr);
  ASSERT_EQ(0, ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len));
  a = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(a, 0);
  ASSERT_EQ(0, ::connect(a, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));
  b = ::accept(listener, nullptr, nullptr);
  ASSERT_GE(b, 0);
  ::close(listener);
  ASSERT_EQ(0, ::fcntl(a, F_SETFL, O_NONBLOCK));
  ASSERT_EQ(0, ::fcntl(b, F_SETFL, O_NONBLOCK));
}

// Caps the kernel buffers of fd so a peer that stops reading fills the whole path quickly.
void shrinkBuffers(int fd) {
  const int size = 64 << 10;
  ASSERT_EQ(0, ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)));
  ASSERT_EQ(0, ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)));
}

// Installs kernel TLS with a fixed key on fd, for sending and, unless told otherwise, for
// receiving. Both ends of a pair get the same key so the kernel encrypts on one end and decrypts on
// the other.
bool installKernelTls(int fd, bool receive = true) {
  if (::setsockopt(fd, IPPROTO_TCP, TCP_ULP, "tls", sizeof("tls")) != 0) {
    return false;
  }
  tls12_crypto_info_aes_gcm_128 info = {};
  info.info.version = TLS_1_2_VERSION;
  info.info.cipher_type = TLS_CIPHER_AES_GCM_128;
  return ::setsockopt(fd, SOL_TLS, TLS_TX, &info, sizeof(info)) == 0 &&
         (!receive || ::setsockopt(fd, SOL_TLS, TLS_RX, &info, sizeof(info)) == 0);
}

// Sends one TLS record of the given type on a kernel TLS socket.
void sendTlsRecord(int fd, uint8_t record_type, const std::string& payload) {
  alignas(cmsghdr) char control[CMSG_SPACE(sizeof(uint8_t))];
  iovec iov = {const_cast<char*>(payload.data()), payload.size()};
  msghdr msg = {};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control;
  msg.msg_controllen = sizeof(control);
  cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_TLS;
  cmsg->cmsg_type = TLS_SET_RECORD_TYPE;
  cmsg->cmsg_len = CMSG_LEN(sizeof(uint8_t));
  *CMSG_DATA(cmsg) = record_type;
  ASSERT_EQ(static_cast<ssize_t>(payload.size()), ::sendmsg(fd, &msg, 0));
}

// Bytes that depend on their offset so a reordered or truncated stream does not compare equal.
std::string pattern(size_t size, uint8_t seed = 7) {
  std::string out(size, '\0');
  for (size_t i = 0; i < size; ++i) {
    out[i] = static_cast<char>((i * 31 + seed) % 251);
  }
  return out;
}

// Drives a SplicePump over real loopback sockets with a real dispatcher. The pump owns one end of a
// downstream and an upstream socket pair, and the test plays the client and the backend on the
// other end of each through edge triggered file events that send queued bytes and record what
// arrives.
class SplicePumpTest : public testing::Test {
public:
  SplicePumpTest() : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")) {}

  void TearDown() override {
    client_.event.reset();
    backend_.event.reset();
    pump_.reset();
    for (const int fd : {client_.fd, backend_.fd, down_fd_, up_fd_}) {
      if (fd >= 0) {
        ::close(fd);
      }
    }
  }

protected:
  // One test end of a socket pair.
  struct Peer {
    int fd{-1};
    std::string to_send;
    size_t sent{0};
    bool shutdown_after_send{false};
    std::string received;
    bool eof{false};
    Event::FileEventPtr event;
  };

  void startPump(bool up_is_ktls) {
    connectPair(down_fd_, client_.fd);
    connectPair(up_fd_, backend_.fd);
    if (up_is_ktls) {
      ASSERT_TRUE(installKernelTls(up_fd_));
      ASSERT_TRUE(installKernelTls(backend_.fd));
    }
    armPeer(client_);
    armPeer(backend_);
    pump_ = std::make_unique<SplicePump>(
        down_fd_, up_fd_, *dispatcher_,
        [this](SplicePump::Outcome outcome) {
          completed_ = true;
          outcome_ = outcome;
          checkDone();
        },
        max_bytes_per_wakeup_);
    // A kTLS upstream stands for a secure one, so the pump must find kTLS on it.
    ASSERT_TRUE(pump_->prepare(/*upstream_needs_ktls=*/up_is_ktls));
    pump_->arm();
  }

  // Services a peer whenever its socket is readable or writable.
  void armPeer(Peer& peer) {
    peer.event = dispatcher_->createFileEvent(
        peer.fd,
        [this, &peer](uint32_t) {
          service(peer);
          return absl::OkStatus();
        },
        Event::FileTriggerType::Edge,
        Event::FileReadyType::Read | Event::FileReadyType::Write | Event::FileReadyType::Closed);
  }

  // Resets the connection of a peer, so the pump sees ECONNRESET on its end.
  void resetPeer(Peer& peer) {
    const linger hard_close = {1, 0};
    ASSERT_EQ(0, ::setsockopt(peer.fd, SOL_SOCKET, SO_LINGER, &hard_close, sizeof(hard_close)));
    peer.event.reset();
    ::close(peer.fd);
    peer.fd = -1;
  }

  // Caps every socket buffer and stops reading at peer, so the path toward it fills fast.
  void stallPeer(Peer& peer) {
    for (const int fd : {client_.fd, backend_.fd, down_fd_, up_fd_}) {
      shrinkBuffers(fd);
    }
    peer.event.reset();
  }

  // Runs the loop briefly so the sockets and pipes fill while the stalled peer does not read.
  void runWhilePathFills() {
    for (int i = 0; i < 100; ++i) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  // Queues bytes for a peer to send, optionally half-closing once they are all sent.
  void send(Peer& peer, const std::string& payload, bool shutdown_after = false) {
    peer.to_send += payload;
    peer.shutdown_after_send = shutdown_after;
    service(peer);
  }

  // Runs the loop until done holds. The test fails on a timeout instead of hanging.
  void runUntil(std::function<bool()> done) {
    done_ = std::move(done);
    if (!done_()) {
      auto timeout = dispatcher_->createTimer([this]() {
        timed_out_ = true;
        dispatcher_->exit();
      });
      timeout->enableTimer(std::chrono::seconds(15));
      dispatcher_->run(Event::Dispatcher::RunType::Block);
    }
    done_ = nullptr;
    ASSERT_FALSE(timed_out_);
  }

  void checkDone() {
    if (done_ && done_()) {
      dispatcher_->exit();
    }
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  SplicePumpPtr pump_;
  int down_fd_{-1};
  int up_fd_{-1};
  uint64_t max_bytes_per_wakeup_{SplicePump::DefaultMaxBytesPerWakeup};
  Peer client_;
  Peer backend_;
  bool completed_{false};
  SplicePump::Outcome outcome_{SplicePump::Outcome::Done};
  bool timed_out_{false};

private:
  // Sends what the socket accepts, half-closes once everything is sent if asked, and records
  // everything that arrived.
  void service(Peer& peer) {
    while (peer.sent < peer.to_send.size()) {
      const ssize_t n = ::send(peer.fd, peer.to_send.data() + peer.sent,
                               peer.to_send.size() - peer.sent, MSG_NOSIGNAL);
      if (n <= 0) {
        break;
      }
      peer.sent += static_cast<size_t>(n);
    }
    if (peer.shutdown_after_send && peer.sent == peer.to_send.size()) {
      ::shutdown(peer.fd, SHUT_WR);
      peer.shutdown_after_send = false;
    }
    char buf[65536];
    ssize_t n;
    while ((n = ::recv(peer.fd, buf, sizeof(buf), 0)) > 0) {
      peer.received.append(buf, static_cast<size_t>(n));
    }
    if (n == 0) {
      peer.eof = true;
    }
    checkDone();
  }

  std::function<bool()> done_;
};

TEST_F(SplicePumpTest, RelaysUpload) {
  startPump(false);
  const std::string upload = pattern(4 << 20);
  send(client_, upload);
  runUntil([&]() { return backend_.received.size() >= upload.size(); });
  EXPECT_TRUE(samePayload(upload, backend_.received));
}

TEST_F(SplicePumpTest, RelaysDownload) {
  startPump(false);
  const std::string download = pattern(4 << 20);
  send(backend_, download);
  runUntil([&]() { return client_.received.size() >= download.size(); });
  EXPECT_TRUE(samePayload(download, client_.received));
}

// A downstream half-close reaches the upstream, and the upstream can still answer afterwards.
TEST_F(SplicePumpTest, DownstreamHalfCloseKeepsDownloadFlowing) {
  startPump(false);
  const std::string upload = pattern(256 << 10, 1);
  send(client_, upload, /*shutdown_after=*/true);
  runUntil([&]() { return backend_.eof; });
  EXPECT_TRUE(samePayload(upload, backend_.received));
  EXPECT_FALSE(completed_);

  const std::string download = pattern(256 << 10, 2);
  send(backend_, download, /*shutdown_after=*/true);
  runUntil([&]() { return client_.eof && completed_; });
  EXPECT_TRUE(samePayload(download, client_.received));
  EXPECT_EQ(SplicePump::Outcome::Done, outcome_);
}

// An upstream half-close reaches the downstream, and the downstream can still upload afterwards.
TEST_F(SplicePumpTest, UpstreamHalfCloseKeepsUploadFlowing) {
  startPump(false);
  const std::string download = pattern(256 << 10, 3);
  send(backend_, download, /*shutdown_after=*/true);
  runUntil([&]() { return client_.eof; });
  EXPECT_TRUE(samePayload(download, client_.received));
  EXPECT_FALSE(completed_);

  const std::string upload = pattern(256 << 10, 4);
  send(client_, upload, /*shutdown_after=*/true);
  runUntil([&]() { return backend_.eof && completed_; });
  EXPECT_TRUE(samePayload(upload, backend_.received));
  EXPECT_EQ(SplicePump::Outcome::Done, outcome_);
}

// A client that stops reading closes its window, so the downstream socket fills and the pump
// stalls. It resumes on the next write edge and delivers every byte once the client reads again.
TEST_F(SplicePumpTest, ResumesAfterStalledReader) {
  startPump(false);
  stallPeer(client_);
  const std::string download = pattern(8 << 20, 5);
  send(backend_, download);
  runWhilePathFills();
  // The backend could not send everything, so the stalled client held back the whole path.
  ASSERT_LT(backend_.sent, download.size());
  armPeer(client_);
  runUntil([&]() { return client_.received.size() >= download.size(); });
  EXPECT_TRUE(samePayload(download, client_.received));
}

// A backend that stops reading holds back the upload the same way, until the next write edge.
TEST_F(SplicePumpTest, ResumesAfterStalledBackend) {
  startPump(false);
  stallPeer(backend_);
  const std::string upload = pattern(8 << 20, 17);
  send(client_, upload);
  runWhilePathFills();
  ASSERT_LT(client_.sent, upload.size());
  armPeer(backend_);
  runUntil([&]() { return backend_.received.size() >= upload.size(); });
  EXPECT_TRUE(samePayload(upload, backend_.received));
}

// A download queued toward a client that then resets fails the downstream write.
TEST_F(SplicePumpTest, DownstreamWriteErrorIsDownstreamError) {
  startPump(false);
  stallPeer(client_);
  send(backend_, pattern(8 << 20, 11));
  runWhilePathFills();
  resetPeer(client_);
  runUntil([&]() { return completed_; });
  EXPECT_EQ(SplicePump::Outcome::DownstreamError, outcome_);
}

// A reset on a socket ends the session with the error of that side.
TEST_F(SplicePumpTest, UpstreamResetIsUpstreamError) {
  startPump(false);
  send(backend_, pattern(64 << 10, 6));
  runUntil([&]() { return client_.received.size() >= (64u << 10); });
  resetPeer(backend_);
  runUntil([&]() { return completed_; });
  EXPECT_EQ(SplicePump::Outcome::UpstreamError, outcome_);
}

TEST_F(SplicePumpTest, DownstreamResetIsDownstreamError) {
  startPump(false);
  send(client_, pattern(64 << 10, 7));
  runUntil([&]() { return backend_.received.size() >= (64u << 10); });
  resetPeer(client_);
  runUntil([&]() { return completed_; });
  EXPECT_EQ(SplicePump::Outcome::DownstreamError, outcome_);
}

// An upload toward an upstream that can no longer be written fails the upstream write.
TEST_F(SplicePumpTest, UpstreamWriteErrorIsUpstreamError) {
  startPump(false);
  ASSERT_EQ(0, ::shutdown(up_fd_, SHUT_WR));
  send(client_, pattern(64 << 10, 19));
  runUntil([&]() { return completed_; });
  EXPECT_EQ(SplicePump::Outcome::UpstreamError, outcome_);
}

// A pass that spends its byte budget yields and resumes on the next loop iteration, which no
// socket event would trigger, so every byte still arrives.
TEST_F(SplicePumpTest, YieldsAtByteBudget) {
  max_bytes_per_wakeup_ = 16 << 10;
  startPump(false);
  const std::string download = pattern(4 << 20, 20);
  send(backend_, download);
  runUntil([&]() { return client_.received.size() >= download.size(); });
  EXPECT_TRUE(samePayload(download, client_.received));
}

// A blocking socket, as io_uring creates for upstream connections, would stall the worker inside
// splice.
TEST_F(SplicePumpTest, PrepareRejectsBlockingSocket) {
  connectPair(down_fd_, client_.fd);
  connectPair(up_fd_, backend_.fd);
  ASSERT_EQ(0, ::fcntl(up_fd_, F_SETFL, 0));
  SplicePump pump(down_fd_, up_fd_, *dispatcher_, [](SplicePump::Outcome) {});
  EXPECT_FALSE(pump.prepare(/*upstream_needs_ktls=*/false));
}

// A secure upstream without kTLS encrypts in the module, which the splice would bypass.
TEST_F(SplicePumpTest, PrepareRejectsSecureUpstreamWithoutKtls) {
  connectPair(down_fd_, client_.fd);
  connectPair(up_fd_, backend_.fd);
  SplicePump pump(down_fd_, up_fd_, *dispatcher_, [](SplicePump::Outcome) {});
  EXPECT_FALSE(pump.prepare(/*upstream_needs_ktls=*/true));
}

// A pipe that cannot be created leaves the caller on the buffered path.
TEST_F(SplicePumpTest, PrepareFailsWhenPipeCannotBeCreated) {
  connectPair(down_fd_, client_.fd);
  connectPair(up_fd_, backend_.fd);
  SplicePump pump(down_fd_, up_fd_, *dispatcher_, [](SplicePump::Outcome) {});
  // Lower the open file limit to the next free descriptor so the pipe creation hits `EMFILE`,
  // then restore it so the fixture and later tests keep working.
  const int next_fd = ::open("/dev/null", O_RDONLY);
  ASSERT_GE(next_fd, 0);
  rlimit original{};
  ASSERT_EQ(0, ::getrlimit(RLIMIT_NOFILE, &original));
  rlimit capped = original;
  capped.rlim_cur = static_cast<rlim_t>(next_fd);
  ASSERT_EQ(0, ::setrlimit(RLIMIT_NOFILE, &capped));
  ::close(next_fd);
  const bool prepared = pump.prepare(/*upstream_needs_ktls=*/false);
  ASSERT_EQ(0, ::setrlimit(RLIMIT_NOFILE, &original));
  EXPECT_FALSE(prepared);
}

// Runs the pump over kernel TLS. Hosts without the tls module skip these tests.
class KtlsSplicePumpTest : public SplicePumpTest {
protected:
  void SetUp() override {
    int a = -1;
    int b = -1;
    connectPair(a, b);
    // The pump probes kTLS with getsockopt, which older kernels do not support for receiving.
    struct tls_crypto_info info;
    socklen_t len = sizeof(info);
    const bool available =
        installKernelTls(a) && ::getsockopt(a, SOL_TLS, TLS_RX, &info, &len) == 0;
    ::close(a);
    ::close(b);
    if (!available) {
      GTEST_SKIP() << "kernel TLS is unavailable on this host";
    }
  }

  // Reads the backend one record at a time, so the test sees the type of each record that arrives.
  void armRecordReader() {
    backend_.event = dispatcher_->createFileEvent(
        backend_.fd,
        [this](uint32_t) {
          char buf[16384];
          alignas(cmsghdr) char control[CMSG_SPACE(sizeof(uint8_t))];
          iovec iov = {buf, sizeof(buf)};
          msghdr msg = {};
          msg.msg_iov = &iov;
          msg.msg_iovlen = 1;
          msg.msg_control = control;
          msg.msg_controllen = sizeof(control);
          ssize_t n;
          while ((n = ::recvmsg(backend_.fd, &msg, 0)) > 0) {
            const cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
            last_record_type_ = cmsg != nullptr ? *CMSG_DATA(cmsg) : 0;
            last_record_.assign(buf, static_cast<size_t>(n));
            if (last_record_type_ == ApplicationData) {
              backend_.received += last_record_;
            }
            msg.msg_controllen = sizeof(control);
          }
          if (n == 0) {
            backend_.eof = true;
          }
          checkDone();
          return absl::OkStatus();
        },
        Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Closed);
  }

  uint8_t last_record_type_{0};
  std::string last_record_;
};

// Splice from a kTLS RX socket can truncate, so a kTLS downstream stays on the buffered path.
TEST_F(KtlsSplicePumpTest, PrepareRejectsKtlsDownstream) {
  connectPair(down_fd_, client_.fd);
  connectPair(up_fd_, backend_.fd);
  ASSERT_TRUE(installKernelTls(down_fd_));
  SplicePump pump(down_fd_, up_fd_, *dispatcher_, [](SplicePump::Outcome) {});
  EXPECT_FALSE(pump.prepare(/*upstream_needs_ktls=*/false));
}

// A kTLS upstream that only sends could neither be read with recvmsg nor spliced.
TEST_F(KtlsSplicePumpTest, PrepareRejectsOneWayKtlsUpstream) {
  connectPair(down_fd_, client_.fd);
  connectPair(up_fd_, backend_.fd);
  ASSERT_TRUE(installKernelTls(up_fd_, /*receive=*/false));
  SplicePump pump(down_fd_, up_fd_, *dispatcher_, [](SplicePump::Outcome) {});
  EXPECT_FALSE(pump.prepare(/*upstream_needs_ktls=*/false));
}

TEST_F(KtlsSplicePumpTest, DownloadReadsEveryRecord) {
  startPump(true);
  const std::string download = pattern(4 << 20);
  send(backend_, download);
  runUntil([&]() { return client_.received.size() >= download.size(); });
  EXPECT_TRUE(samePayload(download, client_.received));
}

// A small byte budget makes the kTLS download yield many times on its way, and every byte still
// arrives.
TEST_F(KtlsSplicePumpTest, YieldsAtByteBudget) {
  max_bytes_per_wakeup_ = 16 << 10;
  startPump(true);
  const std::string download = pattern(4 << 20, 22);
  send(backend_, download);
  runUntil([&]() { return client_.received.size() >= download.size(); });
  EXPECT_TRUE(samePayload(download, client_.received));
}

TEST_F(KtlsSplicePumpTest, UploadIsEncryptedInKernel) {
  startPump(true);
  const std::string upload = pattern(4 << 20);
  send(client_, upload);
  runUntil([&]() { return backend_.received.size() >= upload.size(); });
  EXPECT_TRUE(samePayload(upload, backend_.received));
}

// A NewSessionTicket between two bursts is consumed and never reaches the client.
TEST_F(KtlsSplicePumpTest, SkipsNewSessionTicket) {
  startPump(true);
  const std::string first = pattern(32 << 10, 8);
  const std::string second = pattern(32 << 10, 9);
  send(backend_, first);
  runUntil([&]() { return client_.received.size() >= first.size(); });
  sendTlsRecord(backend_.fd, Handshake, handshakeRecord({{NewSessionTicket, 16}}));
  send(backend_, second);
  runUntil([&]() { return client_.received.size() >= first.size() + second.size(); });
  EXPECT_TRUE(samePayload(first + second, client_.received));
}

// A close_notify is the upstream end of stream. The client sees a clean half-close, and the session
// ends gracefully once the client closes too.
TEST_F(KtlsSplicePumpTest, CloseNotifyHalfClosesDownstream) {
  startPump(true);
  const std::string download = pattern(64 << 10, 10);
  send(backend_, download);
  sendTlsRecord(backend_.fd, Alert, std::string{1, static_cast<char>(CloseNotify)});
  runUntil([&]() { return client_.eof; });
  EXPECT_TRUE(samePayload(download, client_.received));
  EXPECT_FALSE(completed_);

  send(client_, "", /*shutdown_after=*/true);
  runUntil([&]() { return completed_; });
  EXPECT_EQ(SplicePump::Outcome::Done, outcome_);
}

// A fatal alert from the upstream aborts the session.
TEST_F(KtlsSplicePumpTest, FatalAlertAborts) {
  startPump(true);
  sendTlsRecord(backend_.fd, Alert, std::string{2, 40});
  runUntil([&]() { return completed_; });
  EXPECT_EQ(SplicePump::Outcome::UpstreamError, outcome_);
}

// A client half-close sends a close_notify ahead of the FIN on the kernel TLS socket.
TEST_F(KtlsSplicePumpTest, DownstreamHalfCloseSendsCloseNotify) {
  startPump(true);
  armRecordReader();
  send(client_, "", /*shutdown_after=*/true);
  runUntil([&]() { return backend_.eof; });
  EXPECT_EQ(Alert, last_record_type_);
  EXPECT_EQ((std::string{1, static_cast<char>(CloseNotify)}), last_record_);
}

// A close_notify that finds the kernel TLS send buffer full waits for room, so the backend still
// gets it ahead of the FIN.
TEST_F(KtlsSplicePumpTest, CloseNotifyWaitsForSendBuffer) {
  startPump(true);
  shrinkBuffers(up_fd_);
  shrinkBuffers(backend_.fd);
  backend_.event.reset();
  // Fill the send buffer of the upstream end of the pump while the backend does not read.
  const std::string chunk = pattern(16 << 10, 16);
  std::string upload;
  ssize_t n;
  while ((n = ::send(up_fd_, chunk.data(), chunk.size(), MSG_NOSIGNAL)) > 0) {
    upload.append(chunk, 0, static_cast<size_t>(n));
  }
  send(client_, "", /*shutdown_after=*/true);
  runWhilePathFills();
  armRecordReader();
  runUntil([&]() { return backend_.eof; });
  EXPECT_TRUE(samePayload(upload, backend_.received));
  EXPECT_EQ(Alert, last_record_type_);
  EXPECT_EQ((std::string{1, static_cast<char>(CloseNotify)}), last_record_);
}

// A backend that resets while the close_notify waits ends the session with an upstream error, even
// though its download already ended and no read is left to see the reset.
TEST_F(KtlsSplicePumpTest, CloseNotifyFailureIsUpstreamError) {
  startPump(true);
  shrinkBuffers(up_fd_);
  shrinkBuffers(backend_.fd);
  send(backend_, "", /*shutdown_after=*/true);
  runUntil([&]() { return client_.eof; });
  backend_.event.reset();
  // Fill the send buffer so the alert has to wait.
  const std::string chunk = pattern(16 << 10, 18);
  while (::send(up_fd_, chunk.data(), chunk.size(), MSG_NOSIGNAL) > 0) {
  }
  send(client_, "", /*shutdown_after=*/true);
  runWhilePathFills();
  ASSERT_FALSE(completed_);
  resetPeer(backend_);
  runUntil([&]() { return completed_; });
  EXPECT_EQ(SplicePump::Outcome::UpstreamError, outcome_);
}

// A record that the stalled downstream socket cannot take stays buffered in the pump and is
// delivered once the client reads again.
TEST_F(KtlsSplicePumpTest, ResumesAfterStalledReaderMidRecord) {
  startPump(true);
  stallPeer(client_);
  const std::string download = pattern((8 << 20) + 1000, 13);
  send(backend_, download);
  runWhilePathFills();
  ASSERT_LT(backend_.sent, download.size());
  armPeer(client_);
  runUntil([&]() { return client_.received.size() >= download.size(); });
  EXPECT_TRUE(samePayload(download, client_.received));
}

// A kTLS download held for a client that then resets fails the write to the downstream socket.
TEST_F(KtlsSplicePumpTest, DownstreamWriteErrorIsDownstreamError) {
  startPump(true);
  stallPeer(client_);
  send(backend_, pattern(8 << 20, 21));
  runWhilePathFills();
  resetPeer(client_);
  runUntil([&]() { return completed_; });
  EXPECT_EQ(SplicePump::Outcome::DownstreamError, outcome_);
}

TEST_F(KtlsSplicePumpTest, ResetIsUpstreamError) {
  startPump(true);
  send(backend_, pattern(64 << 10, 15));
  runUntil([&]() { return client_.received.size() >= (64u << 10); });
  resetPeer(backend_);
  runUntil([&]() { return completed_; });
  EXPECT_EQ(SplicePump::Outcome::UpstreamError, outcome_);
}

// An upstream that floods control records with no data is closed rather than drained without end.
TEST_F(KtlsSplicePumpTest, TooManyControlRecordsAborts) {
  startPump(true);
  for (int i = 0; i < 1100; ++i) {
    sendTlsRecord(backend_.fd, Handshake, handshakeRecord({{NewSessionTicket, 4}}));
  }
  runUntil([&]() { return completed_; });
  EXPECT_EQ(SplicePump::Outcome::UpstreamError, outcome_);
}

#else // !defined(__linux__)

TEST(SplicePumpTest, PrepareFailsOffLinux) {
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher = api->allocateDispatcher("test");
  SplicePump pump(-1, -1, *dispatcher, [](SplicePump::Outcome) {});
  EXPECT_FALSE(pump.prepare(/*upstream_needs_ktls=*/false));
}

#endif // defined(__linux__)

} // namespace
} // namespace DynamicModules
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
