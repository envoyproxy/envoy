package org.chromium.net.impl;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.chromium.net.testing.ConditionVariable;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.nio.ByteBuffer;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;

import io.envoyproxy.envoymobile.engine.EnvoyHTTPStream;

@RunWith(AndroidJUnit4.class)
public class CancelProofEnvoyStreamTest {

  private static final ByteBuffer BYTE_BUFFER = ByteBuffer.allocateDirect(1);
  private static final HashMap<String, List<String>> HEADERS = new HashMap<>();

  // This mocked version of the EnvoyHTTPStream allows to block while executing of some of its
  // methods. This is used to easily simulate concurrent invocations of those methods.
  private final MockedStream mMockedStream = new MockedStream();

  // When "closed", these the condition variables are blocking the execution of the corresponding
  // method being overridden in MockedStream. A "multi-thread" test can then open the ones it wants.
  private final ConditionVariable mSendHeadersBlock = new ConditionVariable();
  private final ConditionVariable mSendDataBlock = new ConditionVariable();
  private final ConditionVariable mReadDataBlock = new ConditionVariable();

  private final AtomicInteger mSendHeadersInvocationCount = new AtomicInteger();
  private final AtomicInteger mSendDataInvocationCount = new AtomicInteger();
  private final AtomicInteger mReadDataInvocationCount = new AtomicInteger();
  private final AtomicInteger mCancelInvocationCount = new AtomicInteger();

  // This is used to guarantee that many Threads have reach a blocking point. Example, if 10
  // Threads are to be blocked while executing some of the MockedStream methods, then this latch
  // will unblock only when those 10 Threads have reached that blocking point. So to avoid race
  // conditions, a "multi-thread" test must wait for this CountDownLatch to unlock.
  private CountDownLatch mStartLatch = new CountDownLatch(0); // By default no latch.

  private final CancelProofEnvoyStream cancelProofEnvoyStream = new CancelProofEnvoyStream();

  @Before
  public void setUp() {
    // By default MockStream's methods are not blocking.
    mSendHeadersBlock.open();
    mSendDataBlock.open();
    mReadDataBlock.open();
  }

  @After
  public void tearDown() {
    // For the cases where a Thread is being blocked.
    mSendHeadersBlock.open();
    mSendDataBlock.open();
    mReadDataBlock.open();
  }

  @Test
  public void setStream() {
    cancelProofEnvoyStream.setStream(mMockedStream);

    assertThat(mCancelInvocationCount.get()).isZero();
  }

  @Test
  public void setStream_afterCancel() {
    cancelProofEnvoyStream.cancel();

    cancelProofEnvoyStream.setStream(mMockedStream);

    assertThat(mCancelInvocationCount.get()).isOne();
  }

  @Test
  public void setStream_twice() {
    cancelProofEnvoyStream.setStream(mMockedStream);

    assertThatThrownBy(() -> cancelProofEnvoyStream.setStream(mMockedStream))
        .isInstanceOf(AssertionError.class);
  }

  @Test
  public void sendHeaders() {
    cancelProofEnvoyStream.setStream(mMockedStream);

    cancelProofEnvoyStream.sendHeaders(HEADERS, false);

    assertThat(mSendHeadersInvocationCount.get()).isOne();
    assertThat(mCancelInvocationCount.get()).isZero();
  }

  @Test
  public void sendHeaders_withoutStreamSet() {
    assertThatThrownBy(() -> cancelProofEnvoyStream.sendHeaders(HEADERS, false))
        .isInstanceOf(NullPointerException.class);
  }

  @Test
  public void sendHeaders_afterCancel() {
    cancelProofEnvoyStream.setStream(mMockedStream);
    cancelProofEnvoyStream.cancel();

    cancelProofEnvoyStream.sendHeaders(HEADERS, false);

    assertThat(mSendHeadersInvocationCount.get()).isZero();
    assertThat(mCancelInvocationCount.get()).isOne();
  }

  @Test
  public void sendHeaders_postponedCancelGetsExecutedToo() throws Exception {
    cancelProofEnvoyStream.setStream(mMockedStream);
    mSendHeadersBlock.close(); // Following Thread will block on executing sendHeaders
    Thread thread = new Thread(() -> cancelProofEnvoyStream.sendHeaders(HEADERS, false));
    thread.start();
    mStartLatch = new CountDownLatch(1); // Only one method to wait for.
    mStartLatch.await(); // Wait for the above Thread to enter the sendHeaders method.
    cancelProofEnvoyStream.cancel();
    mSendHeadersBlock.open();
    thread.join(); // Wait for the Thread to die.

    assertThat(mSendHeadersInvocationCount.get()).isOne();
    assertThat(mCancelInvocationCount.get()).isOne();
  }

  @Test
  public void sendData() {
    cancelProofEnvoyStream.setStream(mMockedStream);

    cancelProofEnvoyStream.sendData(BYTE_BUFFER, false);

    assertThat(mSendDataInvocationCount.get()).isOne();
    assertThat(mCancelInvocationCount.get()).isZero();
  }

  @Test
  public void sendData_withoutStreamSet() {
    assertThatThrownBy(() -> cancelProofEnvoyStream.sendData(BYTE_BUFFER, false))
        .isInstanceOf(NullPointerException.class);
  }

  @Test
  public void sendData_afterCancel() {
    cancelProofEnvoyStream.setStream(mMockedStream);
    cancelProofEnvoyStream.cancel();

    cancelProofEnvoyStream.sendData(BYTE_BUFFER, false);

    assertThat(mSendDataInvocationCount.get()).isZero();
    assertThat(mCancelInvocationCount.get()).isOne();
  }

  @Test
  public void sendData_postponedCancelGetsExecutedToo() throws Exception {
    cancelProofEnvoyStream.setStream(mMockedStream);
    mSendDataBlock.close(); // Following Thread will block on executing sendData
    Thread thread = new Thread(() -> cancelProofEnvoyStream.sendData(BYTE_BUFFER, false));
    thread.start();
    mStartLatch = new CountDownLatch(1); // Only one method to wait for.
    mStartLatch.await();                 // Wait for the above Thread to enter the sendData method.
    cancelProofEnvoyStream.cancel();
    mSendDataBlock.open();
    thread.join(); // Wait for the Thread to die.

    assertThat(mSendDataInvocationCount.get()).isOne();
    assertThat(mCancelInvocationCount.get()).isOne();
  }

  @Test
  public void readData() {
    cancelProofEnvoyStream.setStream(mMockedStream);

    cancelProofEnvoyStream.readData(1);

    assertThat(mReadDataInvocationCount.get()).isOne();
    assertThat(mCancelInvocationCount.get()).isZero();
  }

  @Test
  public void readData_withoutStreamSet() {
    assertThatThrownBy(() -> cancelProofEnvoyStream.readData(1))
        .isInstanceOf(NullPointerException.class);
  }

  @Test
  public void readData_afterCancel() {
    cancelProofEnvoyStream.setStream(mMockedStream);
    cancelProofEnvoyStream.cancel();

    cancelProofEnvoyStream.readData(1);

    assertThat(mReadDataInvocationCount.get()).isZero();
    assertThat(mCancelInvocationCount.get()).isOne();
  }

  @Test
  public void readData_postponedCancelGetsExecutedToo() throws Exception {
    cancelProofEnvoyStream.setStream(mMockedStream);
    mReadDataBlock.close(); // Following Thread will block on executing readData
    Thread thread = new Thread(() -> cancelProofEnvoyStream.readData(1));
    thread.start();
    mStartLatch = new CountDownLatch(1); // Only one method to wait for.
    mStartLatch.await();                 // Wait for the above Thread to enter the readData method.
    cancelProofEnvoyStream.cancel();
    mReadDataBlock.open();
    thread.join(); // Wait for the Thread to die.

    assertThat(mReadDataInvocationCount.get()).isOne();
    assertThat(mCancelInvocationCount.get()).isOne();
  }

  @Test
  public void cancel() {
    cancelProofEnvoyStream.setStream(mMockedStream);

    cancelProofEnvoyStream.cancel();

    assertThat(mCancelInvocationCount.get()).isOne();
  }

  @Test
  public void cancel_twice_executedOnlyOnce() {
    cancelProofEnvoyStream.setStream(mMockedStream);

    cancelProofEnvoyStream.cancel();
    cancelProofEnvoyStream.cancel();

    assertThat(mCancelInvocationCount.get()).isOne();
  }

  @Test
  public void cancel_manyConcurrentThreads_executedOnlyOnce() throws Exception {
    cancelProofEnvoyStream.setStream(mMockedStream);
    Thread[] threads = new Thread[100];
    for (int i = 0; i < threads.length; i++) {
      threads[i] = new Thread(cancelProofEnvoyStream::cancel);
    }
    for (Thread thread : threads) {
      thread.start();
    }
    // Wait for all the Threads to die.
    for (Thread thread : threads) {
      thread.join();
    }

    assertThat(mCancelInvocationCount.get()).isOne();
  }

  @Test
  public void cancel_withoutStreamSet() {
    // Does nothing because the "Concurrently Running Stream Operations Count" starts with "1".
    // This is the desired outcome - the cancel is postponed. Once the stream is set, the next
    // Stream Operation will invoke "cancel".
    cancelProofEnvoyStream.cancel();
    // Note: asserting that mCancelInvocationCount is zero is pointless here. It is the MockedStream
    // that can increment the cancel invocation count, and this test does not invoke setStream.
  }

  @Test
  public void cancel_whileSendHeadersIsExecuting_cancelGetsPostponed() throws Exception {
    cancelProofEnvoyStream.setStream(mMockedStream);
    mSendHeadersBlock.close(); // Following Thread will block on executing sendHeaders
    new Thread(() -> cancelProofEnvoyStream.sendHeaders(HEADERS, false)).start();
    mStartLatch = new CountDownLatch(1); // Only one method to wait for.
    mStartLatch.await(); // Wait for the above Thread to enter the sendHeaders method.

    cancelProofEnvoyStream.cancel();

    assertThat(mSendHeadersInvocationCount.get()).isOne();
    assertThat(mCancelInvocationCount.get()).isZero();
  }

  @Test
  public void cancel_whileSendDataIsExecuting_cancelGetsPostponed() throws Exception {
    cancelProofEnvoyStream.setStream(mMockedStream);
    mSendDataBlock.close(); // Following Thread will block on executing sendData
    new Thread(() -> cancelProofEnvoyStream.sendData(BYTE_BUFFER, false)).start();
    mStartLatch = new CountDownLatch(1); // Only one method to wait for.
    mStartLatch.await();                 // Wait for the above Thread to enter the sendData method.

    cancelProofEnvoyStream.cancel();

    assertThat(mSendDataInvocationCount.get()).isOne();
    assertThat(mCancelInvocationCount.get()).isZero();
  }

  @Test
  public void cancel_whileReadDataIsExecuting_cancelGetsPostponed() throws Exception {
    cancelProofEnvoyStream.setStream(mMockedStream);
    mReadDataBlock.close(); // Following Thread will block on executing readData
    new Thread(() -> cancelProofEnvoyStream.readData(1)).start();
    mStartLatch = new CountDownLatch(1); // Only one method to wait for.
    mStartLatch.await();                 // Wait for the above Thread to enter the readData method.

    cancelProofEnvoyStream.cancel();

    assertThat(mReadDataInvocationCount.get()).isOne();
    assertThat(mCancelInvocationCount.get()).isZero();
  }

  @Test
  public void cancel_manyConcurrentStreamOperationsInFlight() throws Exception {
    cancelProofEnvoyStream.setStream(mMockedStream);
    Thread[] threads = new Thread[300];
    AtomicInteger count = new AtomicInteger();
    for (int i = 0; i < threads.length; i++) {
      threads[i] = new Thread(() -> {
        switch (count.getAndIncrement() % 3) {
        case 0:
          cancelProofEnvoyStream.sendHeaders(HEADERS, false);
          break;
        case 1:
          cancelProofEnvoyStream.sendData(BYTE_BUFFER, false);
          break;
        case 2:
          cancelProofEnvoyStream.readData(1);
          break;
        }
      });
    }
    // Every Thread will be blocked while executing one of the cancelProofEnvoyStream's method.
    mSendHeadersBlock.close();
    mSendDataBlock.close();
    mReadDataBlock.close();
    mStartLatch = new CountDownLatch(threads.length);
    for (Thread thread : threads) {
      thread.start();
    }
    mStartLatch.await(); // Wait for all Thread to reach the blocking point.
    // Sanity check
    assertThat(mSendHeadersInvocationCount.get()).isEqualTo(threads.length / 3);
    assertThat(mSendDataInvocationCount.get()).isEqualTo(threads.length / 3);
    assertThat(mReadDataInvocationCount.get()).isEqualTo(threads.length / 3);

    cancelProofEnvoyStream.cancel();
    assertThat(mCancelInvocationCount.get()).isZero();

    // Let all the Threads to finish executing the cancelProofEnvoyStream's methods.
    mSendHeadersBlock.open();
    mSendDataBlock.open();
    mReadDataBlock.open();
    // Wait for all the Threads to die.
    for (Thread thread : threads) {
      thread.join();
    }
    assertThat(mCancelInvocationCount.get()).isOne();
  }

  private class MockedStream extends EnvoyHTTPStream {

    private MockedStream() { super(0, 0, null, false); }

    @Override
    public void sendHeaders(Map<String, List<String>> headers, boolean endStream) {
      mSendHeadersInvocationCount.incrementAndGet();
      mStartLatch.countDown();
      mSendHeadersBlock.block();
    }

    @Override
    public void sendData(ByteBuffer data, int length, boolean endStream) {
      mSendDataInvocationCount.incrementAndGet();
      mStartLatch.countDown();
      mSendDataBlock.block();
    }

    @Override
    public void readData(long byteCount) {
      mReadDataInvocationCount.incrementAndGet();
      mStartLatch.countDown();
      mReadDataBlock.block();
    }

    @Override
    public int cancel() {
      mCancelInvocationCount.incrementAndGet();
      return 0;
    }
  }
}
