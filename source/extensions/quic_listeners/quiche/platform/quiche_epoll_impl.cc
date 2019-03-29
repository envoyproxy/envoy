// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quiche_epoll_impl.h"

#include <errno.h>  // for errno and strerror_r
#include <stdlib.h> // for abort
#include <unistd.h> // For read, pipe, close and write.

#include <algorithm>
#include <utility>

#define CHECK_NE(x, y) CHECK(x != y)
#define CHECK_EQ(x, y) CHECK(x == y)
#define CHECK_GE(x, y) CHECK(x >= y)
#define CHECK_GT(x, y) CHECK(x > y)
#define CHECK_LE(x, y) CHECK(x <= y)
#define CHECK_LT(x, y) CHECK(x < y)

#ifndef NDEBUG
#define DCHECK_NE(x, y) CHECK(x != y)
#define DCHECK_EQ(x, y) CHECK(x == y)
#define DCHECK_GE(x, y) CHECK(x >= y)
#define DCHECK_GT(x, y) CHECK(x > y)
#define DCHECK_LE(x, y) CHECK(x <= y)
#define DCHECK_LT(x, y) CHECK(x < y)
#else
#define DCHECK_NE(x, y) DCHECK(x != y)
#define DCHECK_EQ(x, y) DCHECK(x == y)
#define DCHECK_GE(x, y) DCHECK(x >= y)
#define DCHECK_GT(x, y) DCHECK(x > y)
#define DCHECK_LE(x, y) DCHECK(x <= y)
#define DCHECK_LT(x, y) DCHECK(x < y)
#endif

// Design notes: An efficient implementation of ready list has the following
// desirable properties:
//
// A. O(1) insertion into/removal from the list in any location.
// B. Once the callback is found by hash lookup using the fd, the lookup of
//    corresponding entry in the list is O(1).
// C. Safe insertion into/removal from the list during list iteration. (The
//    ready list's purpose is to enable completely event driven I/O model.
//    Thus, all the interesting bits happen in the callback. It is critical
//    to not place any restriction on the API during list iteration.
//
// The current implementation achieves these goals with the following design:
//
// - The ready list is constructed as a doubly linked list to enable O(1)
//   insertion/removal (see man 3 queue).
// - The forward and backward links are directly embedded inside the
//   CBAndEventMask struct. This enables O(1) lookup in the list for a given
//   callback. (Techincally, we could've used std::list of hash_set::iterator,
//   and keep a list::iterator in CBAndEventMask to achieve the same effect.
//   However, iterators have two problems: no way to portably invalidate them,
//   and no way to tell whether an iterator is singular or not. The only way to
//   overcome these issues is to keep bools in both places, but that throws off
//   memory alignment (up to 7 wasted bytes for each bool). The extra level of
//   indirection will also likely be less cache friendly. Direct manipulation
//   of link pointers makes it easier to retrieve the CBAndEventMask from the
//   list, easier to check whether an CBAndEventMask is in the list, uses less
//   memory (save 32 bytes/fd), and does not affect cache usage (we need to
//   read in the struct to use the callback anyway).)
// - Embed the fd directly into CBAndEventMask and switch to using hash_set.
//   This removes the need to store hash_map::iterator in the list just so that
//   we can get both the fd and the callback.
// - The ready list is "one shot": each entry is removed before OnEvent is
//   called. This removes the mutation-while-iterating problem.
// - Use two lists to keep track of callbacks. The ready_list_ is the one used
//   for registration. Before iteration, the ready_list_ is swapped into the
//   tmp_list_. Once iteration is done, tmp_list_ will be empty, and
//   ready_list_ will have all the new ready fds.

// The size we use for buffers passed to strerror_r
static const int kErrorBufferSize = 256;

namespace quiche {

class ScopedReset {
public:
  ScopedReset(bool* scoped_variable, bool new_value)
      : scoped_variable_(scoped_variable), original_value_(std::move(*scoped_variable)) {
    *scoped_variable_ = std::move(new_value);
  }

  ScopedReset(const ScopedReset&) = delete;
  ScopedReset& operator=(const ScopedReset&) = delete;

  ~ScopedReset() { *scoped_variable_ = std::move(original_value_); }

private:
  bool* scoped_variable_;
  bool original_value_;
};

// Clears the pipe and returns. Used for waking the epoll server up.
class ReadPipeCallback : public EpollCallbackInterface {
public:
  void OnEvent(int fd, EpollEvent* event) override {
    DCHECK(event->in_events == EPOLLIN);
    int data;
    int data_read = 1;
    // Read until the pipe is empty.
    while (data_read > 0) {
      data_read = read(fd, &data, sizeof(data));
    }
  }
  void OnShutdown(EpollServer*, int) override {}
  void OnRegistration(EpollServer*, int, int) override {}
  void OnModification(int, int) override {}    // COV_NF_LINE
  void OnUnregistration(int, bool) override {} // COV_NF_LINE
  std::string Name() const override { return "ReadPipeCallback"; }
};

EpollServer::EpollServer()
    : epoll_fd_(epoll_create(1024)), timeout_in_us_(0), recorded_now_in_us_(0), ready_list_size_(0),
      wake_cb_(new ReadPipeCallback), read_fd_(-1), write_fd_(-1),
      in_wait_for_events_and_execute_callbacks_(false), in_shutdown_(false) {
  // ensure that the epoll_fd_ is valid.
  CHECK_NE(epoll_fd_, -1);
  LIST_INIT(&ready_list_);
  LIST_INIT(&tmp_list_);

  int pipe_fds[2];
  if (pipe(pipe_fds) < 0) {
    // Unfortunately, it is impossible to test any such initialization in
    // a constructor (as virtual methods do not yet work).
    // This -could- be solved by moving initialization to an outside
    // call...
    int saved_errno = errno;
    char buf[kErrorBufferSize];
    QUIC_LOG_IMPL(FATAL) << "Error " << saved_errno
                         << " in pipe(): " << strerror_r(saved_errno, buf, sizeof(buf));
  }
  read_fd_ = pipe_fds[0];
  write_fd_ = pipe_fds[1];
  RegisterFD(read_fd_, wake_cb_.get(), EPOLLIN);
}

void EpollServer::CleanupFDToCBMap() {
  auto cb_iter = cb_map_.begin();
  while (cb_iter != cb_map_.end()) {
    int fd = cb_iter->fd;
    CB* cb = cb_iter->cb;

    cb_iter->in_use = true;
    if (cb) {
      cb->OnShutdown(this, fd);
    }

    cb_map_.erase(cb_iter);
    cb_iter = cb_map_.begin();
  }
}

void EpollServer::CleanupTimeToAlarmCBMap() {
  TimeToAlarmCBMap::iterator erase_it;

  // Call OnShutdown() on alarms. Note that the structure of the loop
  // is similar to the structure of loop in the function HandleAlarms()
  for (auto i = alarm_map_.begin(); i != alarm_map_.end();) {
    // Note that OnShutdown() can call UnregisterAlarm() on
    // other iterators. OnShutdown() should not call UnregisterAlarm()
    // on self because by definition the iterator is not valid any more.
    i->second->OnShutdown(this);
    erase_it = i;
    ++i;
    alarm_map_.erase(erase_it);
  }
}

EpollServer::~EpollServer() {
  DCHECK_EQ(in_shutdown_, false);
  in_shutdown_ = true;
#ifdef EPOLL_SERVER_EVENT_TRACING
  QUIC_LOG_IMPL(INFO) << "\n" << event_recorder_;
#endif
  QUIC_VLOG_IMPL(2) << "Shutting down epoll server ";
  CleanupFDToCBMap();

  LIST_INIT(&ready_list_);
  LIST_INIT(&tmp_list_);

  CleanupTimeToAlarmCBMap();

  close(read_fd_);
  close(write_fd_);
  close(epoll_fd_);
}

// Whether a CBAandEventMask is on the ready list is determined by a non-NULL
// le_prev pointer (le_next being NULL indicates end of list).
inline void EpollServer::AddToReadyList(CBAndEventMask* cb_and_mask) {
  if (cb_and_mask->entry.le_prev == NULL) {
    LIST_INSERT_HEAD(&ready_list_, cb_and_mask, entry);
    ++ready_list_size_;
  }
}

inline void EpollServer::RemoveFromReadyList(const CBAndEventMask& cb_and_mask) {
  if (cb_and_mask.entry.le_prev != NULL) {
    LIST_REMOVE(&cb_and_mask, entry);
    // Clean up all the ready list states. Don't bother with the other fields
    // as they are initialized when the CBAandEventMask is added to the ready
    // list. This saves a few cycles in the inner loop.
    cb_and_mask.entry.le_prev = NULL;
    --ready_list_size_;
    if (ready_list_size_ == 0) {
      DCHECK(ready_list_.lh_first == NULL);
      DCHECK(tmp_list_.lh_first == NULL);
    }
  }
}

void EpollServer::RegisterFD(int fd, CB* cb, int event_mask) {
  CHECK(cb);
  QUIC_VLOG_IMPL(3) << "RegisterFD fd=" << fd << " event_mask=" << event_mask;
  auto fd_i = cb_map_.find(CBAndEventMask(NULL, 0, fd));
  if (cb_map_.end() != fd_i) {
    // do we just abort, or do we just unregister the other callback?
    // for now, lets just unregister the other callback.

    // unregister any callback that may already be registered for this FD.
    CB* other_cb = fd_i->cb;
    if (other_cb) {
      // Must remove from the ready list before erasing.
      RemoveFromReadyList(*fd_i);
      other_cb->OnUnregistration(fd, true);
      ModFD(fd, event_mask);
    } else {
      // already unregistered, so just recycle the node.
      AddFD(fd, event_mask);
    }
    fd_i->cb = cb;
    fd_i->event_mask = event_mask;
    fd_i->events_to_fake = 0;
  } else {
    AddFD(fd, event_mask);
    cb_map_.insert(CBAndEventMask(cb, event_mask, fd));
  }

  // set the FD to be non-blocking.
  SetNonblocking(fd);

  cb->OnRegistration(this, fd, event_mask);
}

void EpollServer::SetNonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    int saved_errno = errno;
    char buf[kErrorBufferSize];
    QUIC_LOG_IMPL(FATAL) << "Error " << saved_errno << " doing fcntl(" << fd
                         << ", F_GETFL, 0): " << strerror_r(saved_errno, buf, sizeof(buf));
  }
  if (!(flags & O_NONBLOCK)) {
    int saved_flags = flags;
    flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (flags == -1) {
      // bad.
      int saved_errno = errno;
      char buf[kErrorBufferSize];
      QUIC_LOG_IMPL(FATAL) << "Error " << saved_errno << " doing fcntl(" << fd << ", F_SETFL, "
                           << saved_flags << "): " << strerror_r(saved_errno, buf, sizeof(buf));
    }
  }
}

int EpollServer::epoll_wait_impl(int epfd, struct epoll_event* events, int max_events,
                                 int timeout_in_ms) {
  return epoll_wait(epfd, events, max_events, timeout_in_ms);
}

void EpollServer::RegisterFDForWrite(int fd, CB* cb) { RegisterFD(fd, cb, EPOLLOUT); }

void EpollServer::RegisterFDForReadWrite(int fd, CB* cb) { RegisterFD(fd, cb, EPOLLIN | EPOLLOUT); }

void EpollServer::RegisterFDForRead(int fd, CB* cb) { RegisterFD(fd, cb, EPOLLIN); }

void EpollServer::UnregisterFD(int fd) {
  auto fd_i = cb_map_.find(CBAndEventMask(NULL, 0, fd));
  if (cb_map_.end() == fd_i || fd_i->cb == NULL) {
    // Doesn't exist in server, or has gone through UnregisterFD once and still
    // inside the callchain of OnEvent.
    return;
  }
#ifdef EPOLL_SERVER_EVENT_TRACING
  event_recorder_.RecordUnregistration(fd);
#endif
  CB* cb = fd_i->cb;
  // Since the links are embedded within the struct, we must remove it from the
  // list before erasing it from the hash_set.
  RemoveFromReadyList(*fd_i);
  DelFD(fd);
  cb->OnUnregistration(fd, false);
  // fd_i->cb is NULL if that fd is unregistered inside the callchain of
  // OnEvent. Since the EpollServer needs a valid CBAndEventMask after OnEvent
  // returns in order to add it to the ready list, we cannot have UnregisterFD
  // erase the entry if it is in use. Thus, a NULL fd_i->cb is used as a
  // condition that tells the EpollServer that this entry is unused at a later
  // point.
  if (!fd_i->in_use) {
    cb_map_.erase(fd_i);
  } else {
    // Remove all trace of the registration, and just keep the node alive long
    // enough so the code that calls OnEvent doesn't have to worry about
    // figuring out whether the CBAndEventMask is valid or not.
    fd_i->cb = NULL;
    fd_i->event_mask = 0;
    fd_i->events_to_fake = 0;
  }
}

void EpollServer::ModifyCallback(int fd, int event_mask) { ModifyFD(fd, ~0, event_mask); }

void EpollServer::StopRead(int fd) { ModifyFD(fd, EPOLLIN, 0); }

void EpollServer::StartRead(int fd) { ModifyFD(fd, 0, EPOLLIN); }

void EpollServer::StopWrite(int fd) { ModifyFD(fd, EPOLLOUT, 0); }

void EpollServer::StartWrite(int fd) { ModifyFD(fd, 0, EPOLLOUT); }

void EpollServer::HandleEvent(int fd, int event_mask) {
#ifdef EPOLL_SERVER_EVENT_TRACING
  event_recorder_.RecordEpollEvent(fd, event_mask);
#endif
  auto fd_i = cb_map_.find(CBAndEventMask(NULL, 0, fd));
  if (fd_i == cb_map_.end() || fd_i->cb == NULL) {
    // Ignore the event.
    // This could occur if epoll() returns a set of events, and
    // while processing event A (earlier) we removed the callback
    // for event B (and are now processing event B).
    return;
  }
  fd_i->events_asserted = event_mask;
  CBAndEventMask* cb_and_mask = const_cast<CBAndEventMask*>(&*fd_i);
  AddToReadyList(cb_and_mask);
}

void EpollServer::WaitForEventsAndExecuteCallbacks() {
  if (in_wait_for_events_and_execute_callbacks_) {
    QUIC_LOG_IMPL(DFATAL) << "Attempting to call WaitForEventsAndExecuteCallbacks"
                             " when an ancestor to the current function is already"
                             " WaitForEventsAndExecuteCallbacks!";
    // The line below is actually tested, but in coverage mode,
    // we never see it.
    return; // COV_NF_LINE
  }
  ScopedReset recursion_guard(&in_wait_for_events_and_execute_callbacks_, true);
  if (alarm_map_.empty()) {
    // no alarms, this is business as usual.
    WaitForEventsAndCallHandleEvents(timeout_in_us_, events_, events_size_);
    recorded_now_in_us_ = 0;
    return;
  }

  // store the 'now'. If we recomputed 'now' every iteration
  // down below, then we might never exit that loop-- any
  // long-running alarms might install other long-running
  // alarms, etc. By storing it here now, we ensure that
  // a more reasonable amount of work is done here.
  int64_t now_in_us = NowInUsec();

  // Get the first timeout from the alarm_map where it is
  // stored in absolute time.
  int64_t next_alarm_time_in_us = alarm_map_.begin()->first;
  QUIC_VLOG_IMPL(4) << "next_alarm_time = " << next_alarm_time_in_us
                    << " now             = " << now_in_us << " timeout_in_us = " << timeout_in_us_;

  int64_t wait_time_in_us;
  int64_t alarm_timeout_in_us = next_alarm_time_in_us - now_in_us;

  // If the next alarm is sooner than the default timeout, or if there is no
  // timeout (timeout_in_us_ == -1), wake up when the alarm should fire.
  // Otherwise use the default timeout.
  if (alarm_timeout_in_us < timeout_in_us_ || timeout_in_us_ < 0) {
    wait_time_in_us = std::max(alarm_timeout_in_us, static_cast<int64_t>(0));
  } else {
    wait_time_in_us = timeout_in_us_;
  }

  QUIC_VLOG_IMPL(4) << "wait_time_in_us = " << wait_time_in_us;

  // wait for events.

  WaitForEventsAndCallHandleEvents(wait_time_in_us, events_, events_size_);
  CallAndReregisterAlarmEvents();
  recorded_now_in_us_ = 0;
}

void EpollServer::SetFDReady(int fd, int events_to_fake) {
  auto fd_i = cb_map_.find(CBAndEventMask(NULL, 0, fd));
  if (cb_map_.end() != fd_i && fd_i->cb != NULL) {
    // This const_cast is necessary for LIST_HEAD_INSERT to work. Declaring
    // entry mutable is insufficient because LIST_HEAD_INSERT assigns the
    // forward pointer of the list head to the current cb_and_mask, and the
    // compiler complains that it can't assign a const T* to a T*.
    CBAndEventMask* cb_and_mask = const_cast<CBAndEventMask*>(&*fd_i);
    // Note that there is no clearly correct behavior here when
    // cb_and_mask->events_to_fake != 0 and this function is called.
    // Of the two operations:
    //      cb_and_mask->events_to_fake = events_to_fake
    //      cb_and_mask->events_to_fake |= events_to_fake
    // the first was picked because it discourages users from calling
    // SetFDReady repeatedly to build up the correct event set as it is more
    // efficient to call SetFDReady once with the correct, final mask.
    cb_and_mask->events_to_fake = events_to_fake;
    AddToReadyList(cb_and_mask);
  }
}

void EpollServer::SetFDNotReady(int fd) {
  auto fd_i = cb_map_.find(CBAndEventMask(NULL, 0, fd));
  if (cb_map_.end() != fd_i) {
    RemoveFromReadyList(*fd_i);
  }
}

bool EpollServer::IsFDReady(int fd) const {
  auto fd_i = cb_map_.find(CBAndEventMask(NULL, 0, fd));
  return (cb_map_.end() != fd_i && fd_i->cb != NULL && fd_i->entry.le_prev != NULL);
}

void EpollServer::VerifyReadyList() const {
  int count = 0;
  CBAndEventMask* cur = ready_list_.lh_first;
  for (; cur; cur = cur->entry.le_next) {
    ++count;
  }
  for (cur = tmp_list_.lh_first; cur; cur = cur->entry.le_next) {
    ++count;
  }
  CHECK_EQ(ready_list_size_, count) << "Ready list size does not match count";
}

void EpollServer::RegisterAlarm(int64_t timeout_time_in_us, AlarmCB* ac) {
  CHECK(ac);
  if (all_alarms_.find(ac) != all_alarms_.end()) {
    QUIC_LOG_IMPL(FATAL) << "Alarm already exists " << ac;
  }
  QUIC_VLOG_IMPL(4) << "RegisteringAlarm at : " << timeout_time_in_us;

  auto alarm_iter = alarm_map_.insert(std::make_pair(timeout_time_in_us, ac));

  all_alarms_.insert(ac);
  // Pass the iterator to the EpollAlarmCallbackInterface.
  ac->OnRegistration(alarm_iter, this);
}

// Unregister a specific alarm callback: iterator_token must be a
//  valid iterator. The caller must ensure the validity of the iterator.
void EpollServer::UnregisterAlarm(const AlarmRegToken& iterator_token) {
  AlarmCB* cb = iterator_token->second;
  alarm_map_.erase(iterator_token);
  all_alarms_.erase(cb);
  cb->OnUnregistration();
}

EpollServer::AlarmRegToken EpollServer::ReregisterAlarm(EpollServer::AlarmRegToken iterator_token,
                                                        int64_t timeout_time_in_us) {
  AlarmCB* cb = iterator_token->second;
  alarm_map_.erase(iterator_token);
  return alarm_map_.emplace(timeout_time_in_us, cb);
}

int EpollServer::NumFDsRegistered() const {
  DCHECK_GE(cb_map_.size(), 1u);
  // Omit the internal FD (read_fd_)
  return cb_map_.size() - 1;
}

void EpollServer::Wake() {
  char data = 'd'; // 'd' is for data. It's good enough for me.
  int rv = write(write_fd_, &data, 1);
  DCHECK_EQ(rv, 1);
}

int64_t EpollServer::NowInUsec() const { return absl::GetCurrentTimeNanos() / 1000; }

int64_t EpollServer::ApproximateNowInUsec() const {
  if (recorded_now_in_us_ != 0) {
    return recorded_now_in_us_;
  }
  return this->NowInUsec();
}

std::string EpollServer::EventMaskToString(int event_mask) {
  std::string s;
  if (event_mask & EPOLLIN)
    s += "EPOLLIN ";
  if (event_mask & EPOLLPRI)
    s += "EPOLLPRI ";
  if (event_mask & EPOLLOUT)
    s += "EPOLLOUT ";
  if (event_mask & EPOLLRDNORM)
    s += "EPOLLRDNORM ";
  if (event_mask & EPOLLRDBAND)
    s += "EPOLLRDBAND ";
  if (event_mask & EPOLLWRNORM)
    s += "EPOLLWRNORM ";
  if (event_mask & EPOLLWRBAND)
    s += "EPOLLWRBAND ";
  if (event_mask & EPOLLMSG)
    s += "EPOLLMSG ";
  if (event_mask & EPOLLERR)
    s += "EPOLLERR ";
  if (event_mask & EPOLLHUP)
    s += "EPOLLHUP ";
  if (event_mask & EPOLLONESHOT)
    s += "EPOLLONESHOT ";
  if (event_mask & EPOLLET)
    s += "EPOLLET ";
  return s;
}

void EpollServer::LogStateOnCrash() {
  QUIC_LOG_IMPL(ERROR) << "----------------------Epoll Server---------------------------";
  QUIC_LOG_IMPL(ERROR) << "Epoll server " << this << " polling on fd " << epoll_fd_;
  QUIC_LOG_IMPL(ERROR) << "timeout_in_us_: " << timeout_in_us_;

  // Log sessions with alarms.
  QUIC_LOG_IMPL(ERROR) << alarm_map_.size() << " alarms registered.";
  for (auto it = alarm_map_.begin(); it != alarm_map_.end(); ++it) {
    const bool skipped = alarms_reregistered_and_should_be_skipped_.find(it->second) !=
                         alarms_reregistered_and_should_be_skipped_.end();
    QUIC_LOG_IMPL(ERROR) << "Alarm " << it->second << " registered at time " << it->first
                         << " and should be skipped = " << skipped;
  }

  QUIC_LOG_IMPL(ERROR) << cb_map_.size() << " fd callbacks registered.";
  for (auto it = cb_map_.begin(); it != cb_map_.end(); ++it) {
    QUIC_LOG_IMPL(ERROR) << "fd: " << it->fd << " with mask " << it->event_mask
                         << " registered with cb: " << it->cb;
  }
  QUIC_LOG_IMPL(ERROR) << "----------------------/Epoll Server--------------------------";
}

void EpollServer::DelFD(int fd) const {
  struct epoll_event ee;
  memset(&ee, 0, sizeof(ee));
#ifdef EPOLL_SERVER_EVENT_TRACING
  event_recorder_.RecordFDMaskEvent(fd, 0, "DelFD");
#endif
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, &ee)) {
    int saved_errno = errno;
    char buf[kErrorBufferSize];
    QUIC_LOG_IMPL(FATAL) << "Epoll set removal error for fd " << fd << ": "
                         << strerror_r(saved_errno, buf, sizeof(buf));
  }
}

void EpollServer::AddFD(int fd, int event_mask) const {
  struct epoll_event ee;
  memset(&ee, 0, sizeof(ee));
  ee.events = event_mask | EPOLLERR | EPOLLHUP;
  ee.data.fd = fd;
#ifdef EPOLL_SERVER_EVENT_TRACING
  event_recorder_.RecordFDMaskEvent(fd, ee.events, "AddFD");
#endif
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ee)) {
    int saved_errno = errno;
    char buf[kErrorBufferSize];
    QUIC_LOG_IMPL(FATAL) << "Epoll set insertion error for fd " << fd << ": "
                         << strerror_r(saved_errno, buf, sizeof(buf));
  }
}

void EpollServer::ModFD(int fd, int event_mask) const {
  struct epoll_event ee;
  memset(&ee, 0, sizeof(ee));
  ee.events = event_mask | EPOLLERR | EPOLLHUP;
  ee.data.fd = fd;
#ifdef EPOLL_SERVER_EVENT_TRACING
  event_recorder_.RecordFDMaskEvent(fd, ee.events, "ModFD");
#endif
  QUIC_VLOG_IMPL(3) << "modifying fd= " << fd << " " << EventMaskToString(ee.events);
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ee)) {
    int saved_errno = errno;
    char buf[kErrorBufferSize];
    QUIC_LOG_IMPL(FATAL) << "Epoll set modification error for fd " << fd << ": "
                         << strerror_r(saved_errno, buf, sizeof(buf));
  }
}

void EpollServer::ModifyFD(int fd, int remove_event, int add_event) {
  auto fd_i = cb_map_.find(CBAndEventMask(NULL, 0, fd));
  if (cb_map_.end() == fd_i) {
    QUIC_VLOG_IMPL(2) << "Didn't find the fd " << fd << "in internal structures";
    return;
  }

  if (fd_i->cb != NULL) {
    int& event_mask = fd_i->event_mask;
    QUIC_VLOG_IMPL(3) << "fd= " << fd << " event_mask before: " << EventMaskToString(event_mask);
    event_mask &= ~remove_event;
    event_mask |= add_event;

    QUIC_VLOG_IMPL(3) << " event_mask after: " << EventMaskToString(event_mask);

    ModFD(fd, event_mask);

    fd_i->cb->OnModification(fd, event_mask);
  }
}

void EpollServer::WaitForEventsAndCallHandleEvents(int64_t timeout_in_us,
                                                   struct epoll_event events[], int events_size) {
  if (timeout_in_us == 0 || ready_list_.lh_first != NULL) {
    // If ready list is not empty, then don't sleep at all.
    timeout_in_us = 0;
  } else if (timeout_in_us < 0) {
    QUIC_LOG_IMPL(INFO) << "Negative epoll timeout: " << timeout_in_us
                        << "us; epoll will wait forever for events.";
    // If timeout_in_us is < 0 we are supposed to Wait forever. This means we
    // should set timeout_in_us to -1000 so we will
    // Wait(-1000/1000) == Wait(-1) == Wait forever.
    timeout_in_us = -1000;
  } else {
    // If timeout is specified, and the ready list is empty.
    if (timeout_in_us < 1000) {
      timeout_in_us = 1000;
    }
  }
  const int timeout_in_ms = timeout_in_us / 1000;
  int nfds = epoll_wait_impl(epoll_fd_, events, events_size, timeout_in_ms);
  QUIC_VLOG_IMPL(3) << "nfds=" << nfds;

#ifdef EPOLL_SERVER_EVENT_TRACING
  event_recorder_.RecordEpollWaitEvent(timeout_in_ms, nfds);
#endif

  // If you're wondering why the NowInUsec() is recorded here, the answer is
  // simple: If we did it before the epoll_wait_impl, then the max error for
  // the ApproximateNowInUs() call would be as large as the maximum length of
  // epoll_wait, which can be arbitrarily long. Since this would make
  // ApproximateNowInUs() worthless, we instead record the time -after- we've
  // done epoll_wait, which guarantees that the maximum error is the amount of
  // time it takes to process all the events generated by epoll_wait.
  recorded_now_in_us_ = NowInUsec();
  if (nfds > 0) {
    for (int i = 0; i < nfds; ++i) {
      int event_mask = events[i].events;
      int fd = events[i].data.fd;
      HandleEvent(fd, event_mask);
    }
  } else if (nfds < 0) {
    // Catch interrupted syscall and just ignore it and move on.
    if (errno != EINTR && errno != 0) {
      int saved_errno = errno;
      char buf[kErrorBufferSize];
      QUIC_LOG_IMPL(FATAL) << "Error " << saved_errno
                           << " in epoll_wait: " << strerror_r(saved_errno, buf, sizeof(buf));
    }
  }

  // Now run through the ready list.
  if (ready_list_.lh_first) {
    CallReadyListCallbacks();
  }
}

void EpollServer::CallReadyListCallbacks() {
  // Check pre-conditions.
  DCHECK(tmp_list_.lh_first == NULL);
  // Swap out the ready_list_ into the tmp_list_ before traversing the list to
  // enable SetFDReady() to just push new items into the ready_list_.
  std::swap(ready_list_.lh_first, tmp_list_.lh_first);
  if (tmp_list_.lh_first) {
    tmp_list_.lh_first->entry.le_prev = &tmp_list_.lh_first;
    EpollEvent event(0);
    while (tmp_list_.lh_first != NULL) {
      DCHECK_GT(ready_list_size_, 0);
      CBAndEventMask* cb_and_mask = tmp_list_.lh_first;
      RemoveFromReadyList(*cb_and_mask);

      event.out_ready_mask = 0;
      event.in_events = cb_and_mask->events_asserted | cb_and_mask->events_to_fake;
      // TODO(fenix): get rid of the two separate fields in cb_and_mask.
      cb_and_mask->events_asserted = 0;
      cb_and_mask->events_to_fake = 0;
      {
        // OnEvent() may call UnRegister, so we set in_use, here. Any
        // UnRegister call will now simply set the cb to NULL instead of
        // invalidating the cb_and_mask object (by deleting the object in the
        // map to which cb_and_mask refers)
        ScopedReset in_use_guard(&(cb_and_mask->in_use), true);
        cb_and_mask->cb->OnEvent(cb_and_mask->fd, &event);
      }

      // Since OnEvent may have called UnregisterFD, we must check here that
      // the callback is still valid. If it isn't, then UnregisterFD *was*
      // called, and we should now get rid of the object.
      if (cb_and_mask->cb == NULL) {
        cb_map_.erase(*cb_and_mask);
      } else if (event.out_ready_mask != 0) {
        cb_and_mask->events_to_fake = event.out_ready_mask;
        AddToReadyList(cb_and_mask);
      }
    }
  }
  DCHECK(tmp_list_.lh_first == NULL);
}

void EpollServer::CallAndReregisterAlarmEvents() {
  int64_t now_in_us = recorded_now_in_us_;
  DCHECK_NE(0, recorded_now_in_us_);

  TimeToAlarmCBMap::iterator erase_it;

  // execute alarms.
  for (auto i = alarm_map_.begin(); i != alarm_map_.end();) {
    if (i->first > now_in_us) {
      break;
    }
    AlarmCB* cb = i->second;
    // Execute the OnAlarm() only if we did not register
    // it in this loop itself.
    const bool added_in_this_round = alarms_reregistered_and_should_be_skipped_.find(cb) !=
                                     alarms_reregistered_and_should_be_skipped_.end();
    if (added_in_this_round) {
      ++i;
      continue;
    }
    all_alarms_.erase(cb);
    const int64_t new_timeout_time_in_us = cb->OnAlarm();

    erase_it = i;
    ++i;
    alarm_map_.erase(erase_it);

    if (new_timeout_time_in_us > 0) {
      // We add to hash_set only if the new timeout is <= now_in_us.
      // if timeout is > now_in_us then we have no fear that this alarm
      // can be reexecuted in this loop, and hence we do not need to
      // worry about a recursive loop.
      QUIC_DVLOG_IMPL(3) << "Reregistering alarm "
                         << " " << cb << " " << new_timeout_time_in_us << " " << now_in_us;
      if (new_timeout_time_in_us <= now_in_us) {
        alarms_reregistered_and_should_be_skipped_.insert(cb);
      }
      RegisterAlarm(new_timeout_time_in_us, cb);
    }
  }
  alarms_reregistered_and_should_be_skipped_.clear();
}

EpollAlarm::EpollAlarm() : eps_(NULL), registered_(false) {}

EpollAlarm::~EpollAlarm() { UnregisterIfRegistered(); }

int64_t EpollAlarm::OnAlarm() {
  registered_ = false;
  return 0;
}

void EpollAlarm::OnRegistration(const EpollServer::AlarmRegToken& token, EpollServer* eps) {
  DCHECK_EQ(false, registered_);

  token_ = token;
  eps_ = eps;
  registered_ = true;
}

void EpollAlarm::OnUnregistration() { registered_ = false; }

void EpollAlarm::OnShutdown(EpollServer*) {
  registered_ = false;
  eps_ = NULL;
}

// If the alarm was registered, unregister it.
void EpollAlarm::UnregisterIfRegistered() {
  if (!registered_) {
    return;
  }
  eps_->UnregisterAlarm(token_);
}

void EpollAlarm::ReregisterAlarm(int64_t timeout_time_in_us) {
  DCHECK(registered_);
  token_ = eps_->ReregisterAlarm(token_, timeout_time_in_us);
}

} // namespace quiche
