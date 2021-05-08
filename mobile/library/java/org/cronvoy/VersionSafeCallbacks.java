package org.cronvoy;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.concurrent.Executor;
import org.chromium.net.BidirectionalStream;
import org.chromium.net.CronetEngine;
import org.chromium.net.CronetException;
import org.chromium.net.NetworkQualityRttListener;
import org.chromium.net.NetworkQualityThroughputListener;
import org.chromium.net.RequestFinishedInfo;
import org.chromium.net.UploadDataProvider;
import org.chromium.net.UploadDataSink;
import org.chromium.net.UrlRequest;
import org.chromium.net.UrlResponseInfo;

/**
 * This class contains wrapper classes for all Cronet API callback/listener classes. These classes
 * only permit callbacks that the version of the client API is known to support. For example, if
 * version 2 of the API adds a callback onFoo() but the client API this class is implementing is
 * version 1, these wrapper classes should not call {@code wrappedCallback.onFoo()} and should
 * instead silently drop the callback.
 *
 * <p>When adding any callback wrapping here, be sure you add the proper version check. Only
 * callbacks supported in all versions of the API should forgo a version check.
 */
final class VersionSafeCallbacks {

  /** Wrap a {@link UrlRequest.Callback} in a version safe manner. */
  static final class UrlRequestCallback extends UrlRequest.Callback {
    private final UrlRequest.Callback wrappedCallback;

    UrlRequestCallback(UrlRequest.Callback callback) { wrappedCallback = callback; }

    @Override
    public void onRedirectReceived(UrlRequest request, UrlResponseInfo info, String newLocationUrl)
        throws Exception {
      wrappedCallback.onRedirectReceived(request, info, newLocationUrl);
    }

    @Override
    public void onResponseStarted(UrlRequest request, UrlResponseInfo info) throws Exception {
      wrappedCallback.onResponseStarted(request, info);
    }

    @Override
    public void onReadCompleted(UrlRequest request, UrlResponseInfo info, ByteBuffer byteBuffer)
        throws Exception {
      wrappedCallback.onReadCompleted(request, info, byteBuffer);
    }

    @Override
    public void onSucceeded(UrlRequest request, UrlResponseInfo info) {
      wrappedCallback.onSucceeded(request, info);
    }

    @Override
    public void onFailed(UrlRequest request, UrlResponseInfo info, CronetException error) {
      wrappedCallback.onFailed(request, info, error);
    }

    @Override
    public void onCanceled(UrlRequest request, UrlResponseInfo info) {
      wrappedCallback.onCanceled(request, info);
    }
  }

  /** Wrap a {@link UrlRequest.StatusListener} in a version safe manner. */
  static final class UrlRequestStatusListener extends UrlRequest.StatusListener {
    private final UrlRequest.StatusListener wrappedListener;

    UrlRequestStatusListener(UrlRequest.StatusListener listener) { wrappedListener = listener; }

    @Override
    public void onStatus(int status) {
      wrappedListener.onStatus(status);
    }
  }

  /** Wrap a {@link BidirectionalStream.Callback} in a version safe manner. */
  static final class BidirectionalStreamCallback extends BidirectionalStream.Callback {
    private final BidirectionalStream.Callback wrappedCallback;

    BidirectionalStreamCallback(BidirectionalStream.Callback callback) {
      wrappedCallback = callback;
    }

    @Override
    public void onStreamReady(BidirectionalStream stream) {
      wrappedCallback.onStreamReady(stream);
    }

    @Override
    public void onResponseHeadersReceived(BidirectionalStream stream, UrlResponseInfo info) {
      wrappedCallback.onResponseHeadersReceived(stream, info);
    }

    @Override
    public void onReadCompleted(BidirectionalStream stream, UrlResponseInfo info, ByteBuffer buffer,
                                boolean endOfStream) {
      wrappedCallback.onReadCompleted(stream, info, buffer, endOfStream);
    }

    @Override
    public void onWriteCompleted(BidirectionalStream stream, UrlResponseInfo info,
                                 ByteBuffer buffer, boolean endOfStream) {
      wrappedCallback.onWriteCompleted(stream, info, buffer, endOfStream);
    }

    @Override
    public void onResponseTrailersReceived(BidirectionalStream stream, UrlResponseInfo info,
                                           UrlResponseInfo.HeaderBlock trailers) {
      wrappedCallback.onResponseTrailersReceived(stream, info, trailers);
    }

    @Override
    public void onSucceeded(BidirectionalStream stream, UrlResponseInfo info) {
      wrappedCallback.onSucceeded(stream, info);
    }

    @Override
    public void onFailed(BidirectionalStream stream, UrlResponseInfo info, CronetException error) {
      wrappedCallback.onFailed(stream, info, error);
    }

    @Override
    public void onCanceled(BidirectionalStream stream, UrlResponseInfo info) {
      wrappedCallback.onCanceled(stream, info);
    }
  }

  /** Wrap a {@link UploadDataProvider} in a version safe manner. */
  static final class UploadDataProviderWrapper extends UploadDataProvider {
    private final UploadDataProvider wrappedProvider;

    UploadDataProviderWrapper(UploadDataProvider provider) { wrappedProvider = provider; }

    @Override
    public long getLength() throws IOException {
      return wrappedProvider.getLength();
    }

    @Override
    public void read(UploadDataSink uploadDataSink, ByteBuffer byteBuffer) throws IOException {
      wrappedProvider.read(uploadDataSink, byteBuffer);
    }

    @Override
    public void rewind(UploadDataSink uploadDataSink) throws IOException {
      wrappedProvider.rewind(uploadDataSink);
    }

    @Override
    public void close() throws IOException {
      wrappedProvider.close();
    }
  }

  /** Wrap a {@link RequestFinishedInfo.Listener} in a version safe manner. */
  static final class RequestFinishedInfoListener extends RequestFinishedInfo.Listener {
    private final RequestFinishedInfo.Listener wrappedListener;

    RequestFinishedInfoListener(RequestFinishedInfo.Listener listener) {
      super(listener.getExecutor());
      wrappedListener = listener;
    }

    @Override
    public void onRequestFinished(RequestFinishedInfo requestInfo) {
      wrappedListener.onRequestFinished(requestInfo);
    }

    @Override
    public Executor getExecutor() {
      return wrappedListener.getExecutor();
    }
  }

  /**
   * Wrap a {@link NetworkQualityRttListener} in a version safe manner. NOTE(pauljensen): Delegates
   * equals() and hashCode() to wrapped listener to facilitate looking up by wrapped listener in an
   * ArrayList.indexOf().
   */
  static final class NetworkQualityRttListenerWrapper extends NetworkQualityRttListener {
    private final NetworkQualityRttListener wrappedListener;

    NetworkQualityRttListenerWrapper(NetworkQualityRttListener listener) {
      super(listener.getExecutor());
      wrappedListener = listener;
    }

    @Override
    public void onRttObservation(int rttMs, long whenMs, int source) {
      wrappedListener.onRttObservation(rttMs, whenMs, source);
    }

    @Override
    public Executor getExecutor() {
      return wrappedListener.getExecutor();
    }

    @Override
    public int hashCode() {
      return wrappedListener.hashCode();
    }

    @Override
    public boolean equals(Object o) {
      if (!(o instanceof NetworkQualityRttListenerWrapper)) {
        return false;
      }
      return wrappedListener.equals(((NetworkQualityRttListenerWrapper)o).wrappedListener);
    }
  }

  /**
   * Wrap a {@link NetworkQualityThroughputListener} in a version safe manner. NOTE(pauljensen):
   * Delegates equals() and hashCode() to wrapped listener to facilitate looking up by wrapped
   * listener in an ArrayList.indexOf().
   */
  static final class NetworkQualityThroughputListenerWrapper
      extends NetworkQualityThroughputListener {
    private final NetworkQualityThroughputListener wrappedListener;

    NetworkQualityThroughputListenerWrapper(NetworkQualityThroughputListener listener) {
      super(listener.getExecutor());
      wrappedListener = listener;
    }

    @Override
    public void onThroughputObservation(int throughputKbps, long whenMs, int source) {
      wrappedListener.onThroughputObservation(throughputKbps, whenMs, source);
    }

    @Override
    public Executor getExecutor() {
      return wrappedListener.getExecutor();
    }

    @Override
    public int hashCode() {
      return wrappedListener.hashCode();
    }

    @Override
    public boolean equals(Object o) {
      if (!(o instanceof NetworkQualityThroughputListenerWrapper)) {
        return false;
      }
      return wrappedListener.equals(((NetworkQualityThroughputListenerWrapper)o).wrappedListener);
    }
  }

  /** Wrap a {@link CronetEngine.Builder.LibraryLoader} in a version safe manner. */
  static final class LibraryLoader extends CronetEngine.Builder.LibraryLoader {
    private final CronetEngine.Builder.LibraryLoader wrappedLoader;

    public LibraryLoader(CronetEngine.Builder.LibraryLoader libraryLoader) {
      wrappedLoader = libraryLoader;
    }

    @Override
    public void loadLibrary(String libName) {
      wrappedLoader.loadLibrary(libName);
    }
  }

  private VersionSafeCallbacks() {}
}
