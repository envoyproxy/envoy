package org.chromium.net.urlconnection;

import androidx.annotation.VisibleForTesting;
import java.io.IOException;
import java.net.HttpRetryException;
import java.net.ProtocolException;
import java.nio.ByteBuffer;
import org.chromium.net.UploadDataProvider;
import org.chromium.net.UploadDataSink;

/**
 * An implementation of {@link java.io.OutputStream} to send data to a server,
 * when {@link CronetHttpURLConnection#setFixedLengthStreamingMode} is used.
 * This implementation does not buffer the entire request body in memory.
 * It does not support rewind. Note that {@link #write} should only be called
 * from the thread on which the {@link #mConnection} is created.
 */
final class CronetFixedModeOutputStream extends CronetOutputStream {
  // CronetFixedModeOutputStream buffers up to this value and wait for UploadDataStream
  // to consume the data. This field is non-final, so it can be changed for tests.
  // Using 16384 bytes is because the internal read buffer is 14520 for QUIC,
  // 16384 for SPDY, and 16384 for normal HTTP/1.1 stream.
  @VisibleForTesting private static int sDefaultBufferLength = 16384;
  private final CronetHttpURLConnection mConnection;
  private final MessageLoop mMessageLoop;
  private final long mContentLength;
  // Internal buffer for holding bytes from the client until the bytes are
  // copied to the UploadDataSink in UploadDataProvider.read().
  // CronetFixedModeOutputStream allows client to provide up to
  // sDefaultBufferLength bytes, and wait for UploadDataProvider.read() to be
  // called after which point mBuffer is cleared so client can fill in again.
  // While the client is filling the buffer (via {@code write()}), the buffer's
  // position points to the next byte to be provided by the client, and limit
  // points to the end of the buffer. The buffer is flipped before it is
  // passed to the UploadDataProvider for consuming. Once it is flipped,
  // buffer position points to the next byte to be copied to the
  // UploadDataSink, and limit points to the end of data available to be
  // copied to UploadDataSink. When the UploadDataProvider has provided all
  // remaining bytes from the buffer to UploadDataSink, it clears the buffer
  // so client can fill it again.
  private final ByteBuffer mBuffer;
  private final UploadDataProvider mUploadDataProvider = new UploadDataProviderImpl();
  private long mBytesWritten;

  /**
   * Package protected constructor.
   * @param connection The CronetHttpURLConnection object.
   * @param contentLength The content length of the request body. Non-zero for
   *            non-chunked upload.
   */
  CronetFixedModeOutputStream(CronetHttpURLConnection connection, long contentLength,
                              MessageLoop messageLoop) {
    if (connection == null) {
      throw new NullPointerException();
    }
    if (contentLength < 0) {
      throw new IllegalArgumentException(
          "Content length must be larger than 0 for non-chunked upload.");
    }
    mContentLength = contentLength;
    int bufferSize = (int)Math.min(mContentLength, sDefaultBufferLength);
    mBuffer = ByteBuffer.allocate(bufferSize);
    mConnection = connection;
    mMessageLoop = messageLoop;
    mBytesWritten = 0;
  }

  @Override
  public void write(int oneByte) throws IOException {
    checkNotClosed();
    checkNotExceedContentLength(1);
    ensureBufferHasRemaining();
    mBuffer.put((byte)oneByte);
    mBytesWritten++;
    uploadIfComplete();
  }

  @Override
  public void write(byte[] buffer, int offset, int count) throws IOException {
    checkNotClosed();
    if (buffer.length - offset < count || offset < 0 || count < 0) {
      throw new IndexOutOfBoundsException();
    }
    checkNotExceedContentLength(count);
    int toSend = count;
    while (toSend > 0) {
      ensureBufferHasRemaining();
      int sent = Math.min(toSend, mBuffer.remaining());
      mBuffer.put(buffer, offset + count - toSend, sent);
      toSend -= sent;
    }
    mBytesWritten += count;
    uploadIfComplete();
  }

  /**
   * If {@code mBuffer} is full, wait until it is consumed and there is
   * space to write more data to it.
   */
  private void ensureBufferHasRemaining() throws IOException {
    if (!mBuffer.hasRemaining()) {
      uploadBufferInternal();
    }
  }

  /**
   * Waits for the native stack to upload {@code mBuffer}'s contents because
   * the client has provided all bytes to be uploaded and there is no need to
   * wait for or expect the client to provide more bytes.
   */
  private void uploadIfComplete() throws IOException {
    if (mBytesWritten == mContentLength) {
      // Entire post data has been received. Now wait for network stack to
      // read it.
      uploadBufferInternal();
    }
  }

  /**
   * Helper function to upload {@code mBuffer} to the native stack. This
   * function blocks until {@code mBuffer} is consumed and there is space to
   * write more data.
   */
  private void uploadBufferInternal() throws IOException {
    checkNotClosed();
    mBuffer.flip();
    mMessageLoop.loop();
    checkNoException();
  }

  /**
   * Throws {@link ProtocolException} if adding {@code numBytes} will
   * exceed content length.
   */
  private void checkNotExceedContentLength(int numBytes) throws ProtocolException {
    if (mBytesWritten + numBytes > mContentLength) {
      throw new ProtocolException("expected " + (mContentLength - mBytesWritten) +
                                  " bytes but received " + numBytes);
    }
  }

  // Below are CronetOutputStream implementations:

  @Override
  void setConnected() throws IOException {
    // Do nothing.
  }

  @Override
  void checkReceivedEnoughContent() throws IOException {
    if (mBytesWritten < mContentLength) {
      throw new ProtocolException("Content received is less than Content-Length.");
    }
  }

  @Override
  UploadDataProvider getUploadDataProvider() {
    return mUploadDataProvider;
  }

  private class UploadDataProviderImpl extends UploadDataProvider {
    @Override
    public long getLength() {
      return mContentLength;
    }

    @Override
    public void read(final UploadDataSink uploadDataSink, final ByteBuffer byteBuffer) {
      if (byteBuffer.remaining() >= mBuffer.remaining()) {
        byteBuffer.put(mBuffer);
        // Reuse this buffer.
        mBuffer.clear();
        uploadDataSink.onReadSucceeded(false);
        // Quit message loop so embedder can write more data.
        mMessageLoop.quit();
      } else {
        int oldLimit = mBuffer.limit();
        mBuffer.limit(mBuffer.position() + byteBuffer.remaining());
        byteBuffer.put(mBuffer);
        mBuffer.limit(oldLimit);
        uploadDataSink.onReadSucceeded(false);
      }
    }

    @Override
    public void rewind(UploadDataSink uploadDataSink) {
      uploadDataSink.onRewindError(new HttpRetryException("Cannot retry streamed Http body", -1));
    }
  }

  /**
   * Sets the default buffer length for use in tests.
   */
  @VisibleForTesting
  static void setDefaultBufferLengthForTesting(int length) {
    sDefaultBufferLength = length;
  }
}
