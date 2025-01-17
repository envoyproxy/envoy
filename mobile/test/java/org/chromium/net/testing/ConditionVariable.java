package org.chromium.net.testing;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Class that implements the condition variable locking paradigm.
 *
 * <p>
 * This differs from the built-in java.lang.Object wait() and notify()
 * in that this class contains the condition to wait on itself. That means
 * open(), close() and block() are sticky. If open() is called before block(),
 * block() will not block, and instead return immediately.
 *
 * <p>
 * This class uses itself as the object to wait on, so if you wait()
 * or notify() on a ConditionVariable, the results are undefined.
 *
 * <p>
 * This offers a similar API to android.os.ConditionVariable, but more suitable for test purposes.
 */
public final class ConditionVariable {

  private final AtomicReference<CountDownLatch> mCountDownLatch = new AtomicReference<>();

  public ConditionVariable() { close(); }

  public void open() {
    CountDownLatch latch = mCountDownLatch.getAndSet(null);
    if (latch != null) {
      latch.countDown();
    }
  }

  public void close() { mCountDownLatch.compareAndSet(null, new CountDownLatch(1)); }

  public void block() {
    CountDownLatch latch = mCountDownLatch.get();
    if (latch == null) {
      return;
    }
    while (true) {
      try {
        latch.await();
        return;
      } catch (InterruptedException e) {
        // Ignore
      }
    }
  }

  public boolean isBlocked() { return mCountDownLatch.get() != null; }
}
