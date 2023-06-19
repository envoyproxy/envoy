package org.chromium.net.urlconnection;

import java.io.IOException;
import java.io.OutputStream;
import org.chromium.net.UploadDataProvider;

/**
 * An abstract class of {@link OutputStream} that concrete implementations must
 * extend in order to be used in {@link CronvoyHttpURLConnection}.
 */
abstract class CronvoyOutputStream extends OutputStream {
  private IOException mException;
  private boolean mClosed;
  private boolean mRequestCompleted;

  @Override
  public void close() throws IOException {
    mClosed = true;
  }

  /**
   * Tells the underlying implementation that connection has been established.
   * Used in {@link CronvoyHttpURLConnection}.
   */
  abstract void setConnected() throws IOException;

  /**
   * Checks whether content received is less than Content-Length.
   * Used in {@link CronvoyHttpURLConnection}.
   */
  abstract void checkReceivedEnoughContent() throws IOException;

  /**
   * Returns {@link UploadDataProvider} implementation.
   */
  abstract UploadDataProvider getUploadDataProvider();

  /**
   * Signals that the request is done. If there is no error,
   * {@code exception} is null. Used by {@link CronvoyHttpURLConnection}.
   */
  void setRequestCompleted(IOException exception) {
    mException = exception;
    mRequestCompleted = true;
  }

  /**
   * Throws an IOException if the stream is closed or the request is done.
   */
  protected void checkNotClosed() throws IOException {
    if (mRequestCompleted) {
      checkNoException();
      throw new IOException("Writing after request completed.");
    }
    if (mClosed) {
      throw new IOException("Stream has been closed.");
    }
  }

  /**
   * Throws the same IOException that the request is failed with. If there
   * is no exception reported, this method is no-op.
   */
  protected void checkNoException() throws IOException {
    if (mException != null) {
      throw mException;
    }
  }
}
