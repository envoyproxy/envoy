package org.chromium.net.urlconnection;

import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;

/**
 * An InputStream that is used by {@link CronetHttpURLConnection} to request
 * data from the network stack as needed.
 */
final class CronetInputStream extends InputStream {
  private final CronetHttpURLConnection mHttpURLConnection;
  // Indicates whether listener's onSucceeded or onFailed callback is invoked.
  private boolean mResponseDataCompleted;
  private ByteBuffer mBuffer;
  private IOException mException;

  private static final int READ_BUFFER_SIZE = 32 * 1024;

  /**
   * Constructs a CronetInputStream.
   * @param httpURLConnection the CronetHttpURLConnection that is associated
   *            with this InputStream.
   */
  public CronetInputStream(CronetHttpURLConnection httpURLConnection) {
    mHttpURLConnection = httpURLConnection;
  }

  @Override
  public int read() throws IOException {
    getMoreDataIfNeeded();
    if (hasUnreadData()) {
      return mBuffer.get() & 0xFF;
    }
    return -1;
  }

  @Override
  public int read(byte[] buffer, int byteOffset, int byteCount) throws IOException {
    if (byteOffset < 0 || byteCount < 0 || byteOffset + byteCount > buffer.length) {
      throw new IndexOutOfBoundsException();
    }
    if (byteCount == 0) {
      return 0;
    }
    getMoreDataIfNeeded();
    if (hasUnreadData()) {
      int bytesRead = Math.min(mBuffer.limit() - mBuffer.position(), byteCount);
      mBuffer.get(buffer, byteOffset, bytesRead);
      return bytesRead;
    }
    return -1;
  }

  @Override
  public int available() throws IOException {
    if (mResponseDataCompleted) {
      if (mException != null) {
        throw mException;
      }
      return 0;
    }
    if (hasUnreadData()) {
      return mBuffer.remaining();
    } else {
      return 0;
    }
  }

  /**
   * Called by {@link CronetHttpURLConnection} to notify that the entire
   * response body has been read.
   * @param exception if not {@code null}, it is the exception to throw when caller
   *            tries to read more data.
   */
  void setResponseDataCompleted(IOException exception) {
    mException = exception;
    mResponseDataCompleted = true;
    // Nothing else to read, so can free the buffer.
    mBuffer = null;
  }

  private void getMoreDataIfNeeded() throws IOException {
    if (mResponseDataCompleted) {
      if (mException != null) {
        throw mException;
      }
      return;
    }
    if (!hasUnreadData()) {
      // Allocate read buffer if needed.
      if (mBuffer == null) {
        mBuffer = ByteBuffer.allocateDirect(READ_BUFFER_SIZE);
      }
      mBuffer.clear();

      // Requests more data from CronetHttpURLConnection.
      mHttpURLConnection.getMoreData(mBuffer);
      if (mException != null) {
        throw mException;
      }
      if (mBuffer != null) {
        mBuffer.flip();
      }
    }
  }

  /**
   * Returns whether {@link #mBuffer} has unread data.
   */
  private boolean hasUnreadData() { return mBuffer != null && mBuffer.hasRemaining(); }
}
