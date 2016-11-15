#pragma once

namespace Http {

class StreamCallbackHelper {
public:
  void runResetCallbacks(StreamResetReason reason) {
    if (reset_callbacks_run_) {
      return;
    }

    for (StreamCallbacks* callbacks : callbacks_) {
      if (callbacks) {
        callbacks->onResetStream(reason);
      }
    }

    reset_callbacks_run_ = true;
  }

protected:
  StreamCallbackHelper() {
    // Set space for 8 callbacks (64 bytes).
    callbacks_.reserve(8);
  }

  void addCallbacks_(StreamCallbacks& callbacks) { callbacks_.push_back(&callbacks); }

  void removeCallbacks_(StreamCallbacks& callbacks) {
    // For performance reasons we just clear the callback and do not resize the vector.
    // Reset callbacks scale with the number of filters per request and do not get added and
    // removed multiple times.
    for (size_t i = 0; i < callbacks_.size(); i++) {
      if (callbacks_[i] == &callbacks) {
        callbacks_[i] = nullptr;
        return;
      }
    }
  }

private:
  std::vector<StreamCallbacks*> callbacks_;
  bool reset_callbacks_run_{};
};

} // Http
