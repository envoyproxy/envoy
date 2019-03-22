// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <hash_map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "source/extensions/quic_listeners/quiche/platform/quiche_epoll_impl.h"

namespace quiche {
namespace {

const int kPageSize = 4096;
const int kMaxBufLen = 10000;

// These are used to record what is happening.
enum { CREATION, REGISTRATION, MODIFICATION, EVENT, UNREGISTRATION, SHUTDOWN, DESTRUCTION };

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

struct RecordEntry {
  RecordEntry() : time(0), instance(nullptr), event_type(0), fd(0), data(0) {}

  RecordEntry(int64 time, void* instance, int event_type, int fd, int data)
      : time(time), instance(instance), event_type(event_type), fd(fd), data(data) {}

  int64 time;
  void* instance;
  int event_type;
  int fd;
  int data;

  void Write(IOBuffer* buf) const {
    switch (event_type) {
    case CREATION:
      buf->WriteString("CREATION\n");
      return;
    case REGISTRATION:
      ::absl::Format(buf, "REGISTRATION fd=%d event_mask=%d (%s)\n", fd, data,
                     EpollServer::EventMaskToString(data));
      return;
    case MODIFICATION:
      ::absl::Format(buf, "MODIFICATION fd=%d event_mask=%d (%s)\n", fd, data,
                     EpollServer::EventMaskToString(data));
      return;
    case EVENT:
      ::absl::Format(buf, "EVENT fd=%d event_mask=%d (%s)\n", fd, data,
                     EpollServer::EventMaskToString(data));
      return;
    case UNREGISTRATION:
      ::absl::Format(buf, "UNREGISTRATION fd=%d replaced=%s\n", fd, data ? "true" : "false");
      return;
    case SHUTDOWN:
      ::absl::Format(buf, "SHUTDOWN fd=%d", fd);
      return;
    case DESTRUCTION:
      buf->WriteString("DESTRUCTION\n");
      return;
    default:
      abort();
    }
  }

  friend std::ostream& operator<<(std::ostream& os, const RecordEntry& re) {
    IOBuffer buf;
    re.Write(&buf);
    string output_string;
    buf.ReadToString(&output_string);
    os << output_string;
    return os;
  }

  bool IsEqual(const RecordEntry* entry) const {
    bool retval = true;

    if (instance != entry->instance) {
      retval = false;
      LOG(INFO) << " instance (" << instance << ") != entry->instance(" << entry->instance << ")";
    }
    if (event_type != entry->event_type) {
      retval = false;
      LOG(INFO) << " event_type (" << event_type << ") != entry->event_type(" << entry->event_type
                << ")";
    }
    if (fd != entry->fd) {
      retval = false;
      LOG(INFO) << " fd (" << fd << ") != entry->fd (" << entry->fd << ")";
    }
    if (data != entry->data) {
      retval = false;
      LOG(INFO) << " data (" << data << ") != entry->data(" << entry->data << ")";
    }
    return retval;
  }
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

class Recorder {
public:
  void Record(void* instance, int event_type, int fd, int data) {
    records_.push_back(RecordEntry(ToUnixMicros(absl::Now()), instance, event_type, fd, data));
  }

  const std::vector<RecordEntry>* records() const { return &records_; }

  void Write(IOBuffer* buf) const {
    for (int i = 0; i < records_.size(); ++i) {
      records_[i].Write(buf);
    }
  }

  friend std::ostream& operator<<(std::ostream& os, const Recorder& recorder) {
    IOBuffer buf;
    recorder.Write(&buf);
    string string_buf;
    buf.ReadToString(&string_buf);
    os << string_buf;
    return os;
  }

  bool IsEqual(const Recorder* recorder) const {
    const std::vector<RecordEntry>* records = recorder->records();

    if (records_.size() != records->size()) {
      LOG(INFO) << "records_.size() (" << records_.size() << ") != records->size() ("
                << records->size() << ")";
      LOG(INFO) << "    *this: \n" << *this;
      LOG(INFO) << "recorder: \n" << *recorder;
      return false;
    }
    for (int i = 0; i < std::min(records_.size(), records->size()); ++i) {
      if (!records_[i].IsEqual(&(*records)[i])) {
        LOG(INFO) << "entry in index: " << i << " with data:\n"
                  << records_[i] << " differs from:\n"
                  << (*records)[i];
        LOG(INFO) << "    *this: \n" << *this;
        LOG(INFO) << "recorder: \n" << *recorder;
        return false;
      }
    }
    return true;
  }

private:
  std::vector<RecordEntry> records_;
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

class RecordingCB : public EpollCallbackInterface {
public:
  RecordingCB() : recorder_(new Recorder()) { recorder_->Record(this, CREATION, 0, 0); }

  ~RecordingCB() override {
    recorder_->Record(this, DESTRUCTION, 0, 0);
    delete recorder_;
  }

  void OnRegistration(EpollServer* eps, int fd, int event_mask) override {
    recorder_->Record(this, REGISTRATION, fd, event_mask);
  }

  void OnModification(int fd, int event_mask) override {
    recorder_->Record(this, MODIFICATION, fd, event_mask);
  }

  void OnEvent(int fd, EpollEvent* event) override {
    recorder_->Record(this, EVENT, fd, event->in_events);
    if (event->in_events & EPOLLIN) {
      const int kLength = 1024;
      char buf[kLength];
      read(fd, &buf, kLength);
    }
  }

  void OnUnregistration(int fd, bool replaced) override {
    recorder_->Record(this, UNREGISTRATION, fd, replaced);
  }

  void OnShutdown(EpollServer* eps, int fd) override {
    if (fd >= 0) {
      eps->UnregisterFD(fd);
    }
    recorder_->Record(this, SHUTDOWN, fd, 0);
  }

  string Name() const override { return "RecordingCB"; }

  const Recorder* recorder() const { return recorder_; }

protected:
  Recorder* recorder_;
};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

// A simple test server that adds some test functions to EpollServer as well as
// allowing access to protected functions.
class EpollTestServer : public EpollServer {
public:
  EpollTestServer() : EpollServer() {}

  ~EpollTestServer() override {}

  void CheckMapping(int fd, CB* cb) {
    CBAndEventMask tmp;
    tmp.fd = fd;
    FDToCBMap::iterator fd_i = cb_map_.find(tmp);
    CHECK(fd_i != cb_map_.end()); // Chokes CHECK_NE.
    CHECK(fd_i->cb == cb);
  }

  void CheckNotMapped(int fd) {
    CBAndEventMask tmp;
    tmp.fd = fd;
    FDToCBMap::iterator fd_i = cb_map_.find(tmp);
    CHECK(fd_i == cb_map_.end()); // Chokes CHECK_EQ.
  }

  void CheckEventMask(int fd, int event_mask) {
    CBAndEventMask tmp;
    tmp.fd = fd;
    FDToCBMap::iterator fd_i = cb_map_.find(tmp);
    CHECK(cb_map_.end() != fd_i); // Chokes CHECK_NE.
    CHECK_EQ(fd_i->event_mask, event_mask);
  }

  void CheckNotRegistered(int fd) {
    struct epoll_event ee;
    memset(&ee, 0, sizeof(ee));
    // If the fd is registered, the epoll_ctl call would succeed (return 0) and
    // the CHECK would fail.
    CHECK(epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &ee));
  }

  void WaitForEventsAndCallHandleEvents(int64 timeout_in_us, struct epoll_event events[],
                                        int events_size) override {
    EpollServer::WaitForEventsAndCallHandleEvents(timeout_in_us, events, events_size);
  }
};

class EpollFunctionTest : public ::testing::Test {
public:
  EpollFunctionTest() : fd_(-1), fd2_(-1), recorder_(nullptr), cb_(nullptr), ep_(nullptr) {}

  ~EpollFunctionTest() override {
    delete ep_;
    delete cb_;
  }

  void SetUp() override {
    ep_ = new EpollTestServer();
    cb_ = new RecordingCB();
    // recorder_ is safe to use directly as we know it has the same scope as
    // cb_
    recorder_ = cb_->recorder();

    int pipe_fds[2];
    if (pipe(pipe_fds) < 0) {
      PLOG(FATAL) << "pipe() failed";
    }
    fd_ = pipe_fds[0];
    fd2_ = pipe_fds[1];
  }

  void TearDown() override {
    close(fd_);
    close(fd2_);
  }

  void DeleteEpollServer() {
    delete ep_;
    ep_ = nullptr;
  }

  int fd() { return fd_; }
  int fd2() { return fd2_; }
  EpollTestServer* ep() { return ep_; }
  EpollCallbackInterface* cb() { return cb_; }
  const Recorder* recorder() { return recorder_; }

private:
  int fd_;
  int fd2_;
  const Recorder* recorder_;
  RecordingCB* cb_;
  EpollTestServer* ep_;
};

} // namespace
} // namespace quiche
