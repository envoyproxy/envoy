package org.chromium.net.impl;

import java.nio.ByteBuffer;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;
import org.chromium.net.CronetException;
import org.chromium.net.UrlRequest;
import org.chromium.net.UrlResponseInfo;

/**
 * Tester for running the full Cronet flow. The base of the logic is similar to a
 * com.google.common.util.concurrent.SettableFuture. The {@link #waitForResponse} method blocks
 * until the flow completes. It throws back any exception caught in a Callback invocation.
 *
 * @param <R> Response type.
 */
final class UrlRequestCallbackTester<R> {

  private final AtomicReference<Throwable> throwable = new AtomicReference<>();
  private final AtomicReference<R> response = new AtomicReference<>();
  private final CountDownLatch countDownLatch = new CountDownLatch(1);

  /**
   * Wraps the provided Callback in such a way that all its methods are run inside a try/catch.
   */
  UrlRequest.Callback getWrappedUrlRequestCallback(UrlRequest.Callback wrappedCallback) {
    return new TestUrlRequestCallback(wrappedCallback);
  }

  /**
   * Ends the flow. Called within the execution of a UrlRequest.Callback method.
   */
  void setResponse(R response) {
    this.response.compareAndSet(null, response);
    countDownLatch.countDown();
  }

  /**
   * Waits for the flow to end, may throw Exception caught while executing a UrlRequest.Callback
   * method.
   */
  R waitForResponse(UrlRequest urlRequest) {
    urlRequest.start();
    try {
      countDownLatch.await();
    } catch (InterruptedException e) {
      throw new RuntimeException(e);
    }
    if (throwable.get() != null) {
      if (throwable.get() instanceof Error) {
        throw(Error) throwable.get();
      }
      if (throwable.get() instanceof RuntimeException) {
        throw(RuntimeException) throwable.get();
      }
      throw new RuntimeException(throwable.get());
    }
    return response.get();
  }

  private void setThrowable(Throwable throwable) {
    this.throwable.compareAndSet(null, throwable);
    countDownLatch.countDown();
  }

  private class TestUrlRequestCallback extends UrlRequest.Callback {

    private final UrlRequest.Callback wrappedCallback;

    private TestUrlRequestCallback(UrlRequest.Callback wrappedCallback) {
      this.wrappedCallback = wrappedCallback;
    }

    @Override
    public void onRedirectReceived(UrlRequest request, UrlResponseInfo info,
                                   String newLocationUrl) {
      try {
        wrappedCallback.onRedirectReceived(request, info, newLocationUrl);
      } catch (Throwable t) {
        setThrowable(t);
      }
    }

    @Override
    public void onResponseStarted(UrlRequest request, UrlResponseInfo info) {
      try {
        wrappedCallback.onResponseStarted(request, info);
      } catch (Throwable t) {
        setThrowable(t);
      }
    }

    @Override
    public void onReadCompleted(UrlRequest request, UrlResponseInfo info, ByteBuffer byteBuffer) {
      try {
        wrappedCallback.onReadCompleted(request, info, byteBuffer);
      } catch (Throwable t) {
        setThrowable(t);
      }
    }

    @Override
    public void onSucceeded(UrlRequest request, UrlResponseInfo info) {
      try {
        wrappedCallback.onSucceeded(request, info);
      } catch (Throwable t) {
        setThrowable(t);
      }
    }

    @Override
    public void onFailed(UrlRequest request, UrlResponseInfo info, CronetException error) {
      try {
        wrappedCallback.onFailed(request, info, error);
      } catch (Throwable t) {
        setThrowable(t);
      }
    }

    @Override
    public void onCanceled(UrlRequest request, UrlResponseInfo info) {
      try {
        wrappedCallback.onCanceled(request, info);
      } catch (Throwable t) {
        setThrowable(t);
      }
    }
  }
}
