package org.chromium.net.impl;

import java.nio.ByteBuffer;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;
import io.envoyproxy.envoymobile.engine.EnvoyHTTPStream;

/**
 * CancelProofEnvoyStream is a consistency layer above the {@link EnvoyHTTPStream} preventing
 * unwarranted Stream operations after a "cancel" operation. There are no "synchronized" - this is
 * Compare And Swap based logic. This class is Thread Safe.
 *
 * <p>This contraption ensures that once a "cancel" operation is invoked, there will be no further
 * operations allowed with the EnvoyHTTPStream - subsequent operations will be ignored silently.
 * However, in the event that that one or more EnvoyHTTPStream operations are currently being
 * executed, the "cancel" operation gets postponed: the last concurrent operation will invoke
 * "cancel" at the end.
 *
 * <p>Instances of this class start with a state of "Busy Starting". This ensure that if a cancel
 * is invoked while the stream is being created, that cancel will be executed only once the stream
 * is completely initialized. Doing otherwise leads to unpredictable outcomes.
 */
final class CancelProofEnvoyStream {

  private static final int CANCEL_BIT = 0x8000;
  /**
   * Mainly maintains a counter of how many Stream operations are currently in-flight. However when
   * bit 15 (0x8000) is set, it indicates that the cancel operation has been requested. If the
   * counter is greater than 0, then that "cancel" operation is postponed until the last in-flight
   * operation finishes, i.e. then the counter is back to 0. Then that last operation also invokes
   * "cancel". On the other hand, if the counter is already 0 when invoking "cancel", then it means
   * that there are no in-flight operations: the "cancel" operation is immediately executed. Once
   * the state is "canceled", any new stream operation is silently ignored.
   */
  private final AtomicInteger mConcurrentInvocationCount = new AtomicInteger();
  private volatile EnvoyHTTPStream mStream; // Cancel can come from any Thread.

  /**
   * The "mConcurrentInvocationCount" does not start with "zero" - this is on purpose. At this
   * stage, the Stream is considered to be in its initialization/starting phase. That phase ends
   * when mStream is set: {@link #setStream}. This way, if "cancel" gets called before the
   * {@link #setStream} method, then the intent is recorded, and the effect will be delivered
   * when {@link #setStream} will be invoked.
   */
  CancelProofEnvoyStream() { mConcurrentInvocationCount.set(1); }

  /** Sets the stream. Can only be invoked once. */
  void setStream(EnvoyHTTPStream stream) {
    // "if (returnTrueIfCanceledOrIncreaseConcurrentlyRunningStreamOperations()) { ..."
    // is not called here - see the Constructor's comment.
    assert mStream == null;
    mStream = stream;
    if (decreaseConcurrentlyRunningStreamOperationsAndReturnTrueIfAwaitingCancel()) {
      mStream.cancel(); // Cancel was called meanwhile, so now this is honored.
    }
  }

  /** Initiates the sending of the request headers if the state permits. */
  void sendHeaders(Map<String, List<String>> envoyRequestHeaders, boolean endStream) {
    if (returnTrueIfCanceledOrIncreaseConcurrentlyRunningStreamOperations()) {
      return; // Already Cancelled - to late to send something.
    }
    mStream.sendHeaders(envoyRequestHeaders, endStream, /* idempotent= */ false);
    if (decreaseConcurrentlyRunningStreamOperationsAndReturnTrueIfAwaitingCancel()) {
      mStream.cancel(); // Cancel was called previously, so now this is honored.
    }
  }

  /** Initiates the sending of one chunk of the request body if the state permits. */
  void sendData(ByteBuffer buffer, boolean finalChunk) {
    if (returnTrueIfCanceledOrIncreaseConcurrentlyRunningStreamOperations()) {
      return; // Already Cancelled - to late to send something.
    }
    // The Envoy Mobile library only cares about the capacity - must use the correct ByteBuffer
    if (buffer.position() == 0) {
      mStream.sendData(buffer, buffer.remaining(), finalChunk);
    } else {
      // TODO(https://github.com/envoyproxy/envoy-mobile/issues/2247): avoid ByteBuffer copies
      ByteBuffer resizedBuffer = ByteBuffer.allocateDirect(buffer.remaining());
      buffer.mark();
      resizedBuffer.put(buffer);
      buffer.reset();
      mStream.sendData(resizedBuffer, finalChunk);
    }
    if (decreaseConcurrentlyRunningStreamOperationsAndReturnTrueIfAwaitingCancel()) {
      mStream.cancel(); // Cancel was called previously, so now this is honored.
    }
  }

  /** Initiates the reading of one chunk of the the request body if the state permits. */
  void readData(int size) {
    if (returnTrueIfCanceledOrIncreaseConcurrentlyRunningStreamOperations()) {
      return; // Already Cancelled - to late to read something.
    }
    mStream.readData(size);
    if (decreaseConcurrentlyRunningStreamOperationsAndReturnTrueIfAwaitingCancel()) {
      mStream.cancel(); // Cancel was called previously, so now this is honored.
    }
  }

  /**
   * Cancels the Stream if the state permits. Will be delayed when an operation is concurrently
   * running. Idempotent and Thread Safe.
   */
  void cancel() {
    // With "Compare And Swap", the contract is the mutation succeeds only if the original value
    // matches the expected one - this is atomic at the assembly language level: most CPUs have
    // dedicated mnemonics for this operation - extremely efficient. And this might look like an
    // infinite loop. There is always one Thread that will succeed - the others may/will loop, and
    // so forth. "Compare And Swap" maybe bad under heavy contention - in that case it is probably
    // better to go with "synchronized" blocks. In our case, there is none or very little
    // contention. What matters is correctness and efficiency.
    while (true) {
      int count = mConcurrentInvocationCount.get();
      if ((count & CANCEL_BIT) != 0) {
        return; // Cancel already invoked.
      }
      if (mConcurrentInvocationCount.compareAndSet(count, count | CANCEL_BIT)) {
        if (count == 0) {
          mStream.cancel(); // Was not busy with other EM operations - cancel right now.
        }
        return;
      }
    }
  }

  private boolean returnTrueIfCanceledOrIncreaseConcurrentlyRunningStreamOperations() {
    while (true) {
      int count = mConcurrentInvocationCount.get();
      if ((count & CANCEL_BIT) != 0) {
        return true; // Already canceled
      }
      if (mConcurrentInvocationCount.compareAndSet(count, count + 1)) {
        return false;
      }
    }
  }

  private boolean decreaseConcurrentlyRunningStreamOperationsAndReturnTrueIfAwaitingCancel() {
    // Only true if the count is back to zero and the cancel bit is set.
    return mConcurrentInvocationCount.decrementAndGet() == CANCEL_BIT;
  }
}
