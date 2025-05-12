#include "source/exe/admin_response.h"

#include "envoy/server/admin.h"

#include "source/server/admin/admin_filter.h"
#include "source/server/admin/utils.h"

namespace Envoy {

AdminResponse::AdminResponse(Server::Instance& server, absl::string_view path,
                             absl::string_view method, SharedPtrSet response_set)
    : server_(server), opt_admin_(server.admin()), shared_response_set_(response_set) {
  request_headers_->setMethod(method);
  request_headers_->setPath(path);
}

AdminResponse::~AdminResponse() {
  cancel();
  shared_response_set_->detachResponse(this);
}

void AdminResponse::getHeaders(HeadersFn fn) {
  auto request_headers = [response = shared_from_this()]() { response->requestHeaders(); };

  // First check for cancelling or termination.
  {
    absl::MutexLock lock(&mutex_);
    ASSERT(headers_fn_ == nullptr);
    if (cancelled_) {
      return;
    }
    headers_fn_ = fn;
    if (terminated_ || !opt_admin_) {
      sendErrorLockHeld();
      return;
    }
  }
  server_.dispatcher().post(request_headers);
}

void AdminResponse::nextChunk(BodyFn fn) {
  auto request_next_chunk = [response = shared_from_this()]() { response->requestNextChunk(); };

  // Note the caller may race a call to nextChunk with the server being
  // terminated.
  {
    absl::MutexLock lock(&mutex_);
    ASSERT(body_fn_ == nullptr);
    if (cancelled_) {
      return;
    }
    body_fn_ = fn;
    if (terminated_ || !opt_admin_) {
      sendAbortChunkLockHeld();
      return;
    }
  }

  // Note that nextChunk may be called from any thread -- it's the callers choice,
  // including the Envoy main thread, which would occur if the caller initiates
  // the request of a chunk upon receipt of the previous chunk.
  //
  // In that case it may race against the AdminResponse object being deleted,
  // in which case the callbacks, held in a shared_ptr, will be cancelled
  // from the destructor. If that happens *before* we post to the main thread,
  // we will just skip and never call fn.
  server_.dispatcher().post(request_next_chunk);
}

// Called by the user if it is not longer interested in the result of the
// admin request. After calling cancel() the caller must not call nextChunk or
// getHeaders.
void AdminResponse::cancel() {
  absl::MutexLock lock(&mutex_);
  cancelled_ = true;
  headers_fn_ = nullptr;
  body_fn_ = nullptr;
}

bool AdminResponse::cancelled() const {
  absl::MutexLock lock(&mutex_);
  return cancelled_;
}

// Called from terminateAdminRequests when the Envoy server
// terminates. After this is called, the caller may need to complete the
// admin response, and so calls to getHeader and nextChunk remain valid,
// resulting in 503 and an empty body.
void AdminResponse::terminate() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  absl::MutexLock lock(&mutex_);
  if (!terminated_) {
    terminated_ = true;
    sendErrorLockHeld();
    sendAbortChunkLockHeld();
  }
}

void AdminResponse::requestHeaders() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  {
    absl::MutexLock lock(&mutex_);
    if (cancelled_ || terminated_) {
      return;
    }
  }
  Server::AdminFilter filter(*opt_admin_);
  filter.decodeHeaders(*request_headers_, false);
  request_ = opt_admin_->makeRequest(filter);
  code_ = request_->start(*response_headers_);
  {
    absl::MutexLock lock(&mutex_);
    if (headers_fn_ == nullptr || cancelled_) {
      return;
    }
    Server::Utility::populateFallbackResponseHeaders(code_, *response_headers_);
    headers_fn_(code_, *response_headers_);
    headers_fn_ = nullptr;
  }
}

void AdminResponse::requestNextChunk() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  {
    absl::MutexLock lock(&mutex_);
    if (cancelled_ || terminated_ || !more_data_) {
      return;
    }
  }
  ASSERT(response_.length() == 0);
  more_data_ = request_->nextChunk(response_);
  {
    absl::MutexLock lock(&mutex_);
    if (sent_end_stream_ || cancelled_) {
      return;
    }
    sent_end_stream_ = !more_data_;
    body_fn_(response_, more_data_);
    ASSERT(response_.length() == 0);
    body_fn_ = nullptr;
  }
}

void AdminResponse::sendAbortChunkLockHeld() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
  if (!sent_end_stream_ && body_fn_ != nullptr) {
    response_.drain(response_.length());
    body_fn_(response_, false);
    sent_end_stream_ = true;
  }
  body_fn_ = nullptr;
}

void AdminResponse::sendErrorLockHeld() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
  if (headers_fn_ != nullptr) {
    code_ = Http::Code::InternalServerError;
    Server::Utility::populateFallbackResponseHeaders(code_, *response_headers_);
    headers_fn_(code_, *response_headers_);
    headers_fn_ = nullptr;
  }
}

void AdminResponse::PtrSet::terminateAdminRequests() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  absl::MutexLock lock(&mutex_);
  accepting_admin_requests_ = false;
  for (AdminResponse* response : response_set_) {
    // Consider the possibility of response being deleted due to its creator
    // dropping its last reference right here. From its destructor it will call
    // detachResponse(), which is mutex-ed against this loop, so before the
    // memory becomes invalid, the call to terminate will complete.
    response->terminate();
  }
  response_set_.clear();
}

void AdminResponse::PtrSet::attachResponse(AdminResponse* response) {
  absl::MutexLock lock(&mutex_);
  if (accepting_admin_requests_) {
    response_set_.insert(response);
  } else {
    response->terminate();
  }
}

void AdminResponse::PtrSet::detachResponse(AdminResponse* response) {
  absl::MutexLock lock(&mutex_);
  response_set_.erase(response);
}

} // namespace Envoy
