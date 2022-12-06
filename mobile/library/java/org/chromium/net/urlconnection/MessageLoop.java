package org.chromium.net.urlconnection;

import java.io.IOException;
import java.io.InterruptedIOException;
import java.net.SocketTimeoutException;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.Executor;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;

/**
 * A MessageLoop class for use in {@link CronetHttpURLConnection}.
 */
final class MessageLoop implements Executor {
  private final BlockingQueue<Runnable> mQueue;

  // Indicates whether this message loop is currently running.
  private boolean mLoopRunning;

  // Indicates whether an InterruptedException or a RuntimeException has
  // occurred in loop(). If true, the loop cannot be safely started because
  // this might cause the loop to terminate immediately if there is a quit
  // task enqueued.
  private boolean mLoopFailed;
  // The exception that caused mLoopFailed to be set to true. Will be
  // rethrown if loop() is called again. If mLoopFailed is set then
  // exactly one of mPriorInterruptedIOException and mPriorRuntimeException
  // will be set.
  private InterruptedIOException mPriorInterruptedIOException;
  private RuntimeException mPriorRuntimeException;

  // Used when assertions are enabled to enforce single-threaded use.
  private static final long INVALID_THREAD_ID = -1;
  private long mThreadId = INVALID_THREAD_ID;

  MessageLoop() { mQueue = new LinkedBlockingQueue<Runnable>(); }

  private boolean calledOnValidThread() {
    if (mThreadId == INVALID_THREAD_ID) {
      mThreadId = Thread.currentThread().getId();
      return true;
    }
    return mThreadId == Thread.currentThread().getId();
  }

  /**
   * Retrieves a task from the queue with the given timeout.
   *
   * @param useTimeout whether to use a timeout.
   * @param timeoutNano Time to wait, in nanoseconds.
   * @return A non-{@code null} Runnable from the queue.
   * @throws InterruptedIOException
   */
  private Runnable take(boolean useTimeout, long timeoutNano) throws InterruptedIOException {
    Runnable task = null;
    try {
      if (!useTimeout) {
        task = mQueue.take(); // Blocks if the queue is empty.
      } else {
        // poll returns null upon timeout.
        task = mQueue.poll(timeoutNano, TimeUnit.NANOSECONDS);
      }
    } catch (InterruptedException e) {
      InterruptedIOException exception = new InterruptedIOException();
      exception.initCause(e);
      throw exception;
    }
    if (task == null) {
      // This will terminate the loop.
      throw new SocketTimeoutException();
    }
    return task;
  }

  /**
   * Runs the message loop. Be sure to call {@link MessageLoop#quit()}
   * to end the loop. If an interruptedException occurs, the loop cannot be
   * started again (see {@link #mLoopFailed}).
   * @throws IOException
   */
  public void loop() throws IOException { loop(0); }

  /**
   * Runs the message loop. Be sure to call {@link MessageLoop#quit()}
   * to end the loop. If an interruptedException occurs, the loop cannot be
   * started again (see {@link #mLoopFailed}).
   * @param timeoutMilli Timeout, in milliseconds, or 0 for no timeout.
   * @throws IOException
   */
  public void loop(int timeoutMilli) throws IOException {
    assert calledOnValidThread();
    // Use System.nanoTime() which is monotonically increasing.
    long startNano = System.nanoTime();
    long timeoutNano = TimeUnit.NANOSECONDS.convert(timeoutMilli, TimeUnit.MILLISECONDS);
    if (mLoopFailed) {
      if (mPriorInterruptedIOException != null) {
        throw mPriorInterruptedIOException;
      } else {
        throw mPriorRuntimeException;
      }
    }
    if (mLoopRunning) {
      throw new IllegalStateException("Cannot run loop when it is already running.");
    }
    mLoopRunning = true;
    while (mLoopRunning) {
      try {
        if (timeoutMilli == 0) {
          take(false, 0).run();
        } else {
          take(true, timeoutNano - System.nanoTime() + startNano).run();
        }
      } catch (InterruptedIOException e) {
        mLoopRunning = false;
        mLoopFailed = true;
        mPriorInterruptedIOException = e;
        throw e;
      } catch (RuntimeException e) {
        mLoopRunning = false;
        mLoopFailed = true;
        mPriorRuntimeException = e;
        throw e;
      }
    }
  }

  /**
   * This causes {@link #loop()} to stop executing messages after the current
   * message being executed. Should only be called from the currently
   * executing message.
   */
  public void quit() {
    assert calledOnValidThread();
    mLoopRunning = false;
  }

  /**
   * Posts a task to the message loop.
   */
  @Override
  public void execute(Runnable task) throws RejectedExecutionException {
    if (task == null) {
      throw new IllegalArgumentException();
    }
    try {
      mQueue.put(task);
    } catch (InterruptedException e) {
      // In theory this exception won't happen, since we have an blocking
      // queue with Integer.MAX_Value capacity, put() call will not block.
      throw new RejectedExecutionException(e);
    }
  }

  /**
   * Returns whether the loop is currently running. Used in testing.
   */
  public boolean isRunning() { return mLoopRunning; }

  /**
   * Returns whether an exception occurred in {#loop()}. Used in testing.
   */
  public boolean hasLoopFailed() { return mLoopFailed; }
}
