package org.cronvoy;

import java.util.concurrent.Executor;
import org.chromium.net.InlineExecutionProhibitedException;

/** Utilities related to {@link Executor}s. */
final class Executors {

  /**
   * Interface used to run commands that could throw an exception. Specifically useful for calling
   * {@link org.chromium.net.UrlRequest.Callback}s on a user-supplied executor.
   */
  interface CheckedRunnable {
    void run() throws Exception;
  }

  /** Executor that detects and throws if its delegate runs a submitted runnable inline. */
  static final class DirectPreventingExecutor implements Executor {
    private final Executor delegate;

    /**
     * Constructs an {@link DirectPreventingExecutor} that executes {@link Runnable}s on the
     * provided {@link Executor}.
     *
     * @param delegate the {@link Executor} used to run {@link Runnable}s
     */
    DirectPreventingExecutor(Executor delegate) { this.delegate = delegate; }

    /**
     * Executes a {@link Runnable} on this {@link Executor} and throws an exception if it is being
     * run on the same thread as the calling thread.
     *
     * @param command the {@link Runnable} to attempt to run
     */
    @Override
    public void execute(Runnable command) {
      Thread currentThread = Thread.currentThread();
      InlineCheckingRunnable runnable = new InlineCheckingRunnable(command, currentThread);
      delegate.execute(runnable);
      // This next read doesn't require synchronization; only the current thread could have
      // written to runnable.mExecutedInline.
      if (runnable.executedInline != null) {
        throw runnable.executedInline;
      } else {
        // It's possible that this method is being called on an executor, and the runnable
        // that was just queued will run on this thread after the current runnable returns.
        // By nulling out the mCallingThread field, the InlineCheckingRunnable's current
        // thread comparison will not fire.
        //
        // Java reference assignment is always atomic (no tearing, even on 64-bit VMs, see
        // JLS 17.7), but other threads aren't guaranteed to ever see updates without
        // something like locking, volatile, or AtomicReferences. We're ok in
        // this instance, since this write only needs to be seen in the case that
        // InlineCheckingRunnable.run() runs on the same thread as this execute() method.
        runnable.callingThread = null;
      }
    }

    private static class InlineCheckingRunnable implements Runnable {
      private final Runnable command;
      private Thread callingThread;
      private InlineExecutionProhibitedException executedInline;

      private InlineCheckingRunnable(Runnable command, Thread callingThread) {
        this.command = command;
        this.callingThread = callingThread;
      }

      @Override
      public void run() {
        if (Thread.currentThread() == callingThread) {
          // Can't throw directly from here, since the delegate executor could catch this
          // exception.
          executedInline = new InlineExecutionProhibitedException();
          return;
        }
        command.run();
      }
    }
  }

  private Executors() {}
}
