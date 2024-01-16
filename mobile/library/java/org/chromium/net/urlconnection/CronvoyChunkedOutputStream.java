package org.chromium.net.urlconnection;

import java.io.IOException;
import java.net.HttpRetryException;
import java.nio.ByteBuffer;
import org.chromium.net.UploadDataProvider;
import org.chromium.net.UploadDataSink;

/**
 * An implementation of {@link java.io.OutputStream} to send data to a server,
 * when {@link CronvoyHttpURLConnection#setChunkedStreamingMode} is used.
 * This implementation does not buffer the entire request body in memory.
 * It does not support rewind. Note that {@link #write} should only be called
 * from the thread on which the {@link #mConnection} is created.
 */
final class CronvoyChunkedOutputStream extends CronvoyOutputStream {
  private final CronvoyHttpURLConnection mConnection;
  private final CronvoyMessageLoop mMessageLoop;
  private final ByteBuffer mBuffer;
  private final UploadDataProvider mUploadDataProvider = new UploadDataProviderImpl();
  private boolean mLastChunk;

  /**
   * Package protected constructor.
   * @param connection The CronvoyHttpURLConnection object.
   * @param chunkLength The chunk length of the request body in bytes. It must
   *            be a positive number.
   */
  CronvoyChunkedOutputStream(CronvoyHttpURLConnection connection, int chunkLength,
                             CronvoyMessageLoop messageLoop) {
    if (connection == null) {
      throw new NullPointerException();
    }
    if (chunkLength <= 0) {
      throw new IllegalArgumentException("chunkLength should be greater than 0");
    }
    mBuffer = ByteBuffer.allocate(chunkLength);
    mConnection = connection;
    mMessageLoop = messageLoop;
  }

  @Override
  public void write(int oneByte) throws IOException {
    ensureBufferHasRemaining();
    mBuffer.put((byte)oneByte);
  }

  @Override
  public void write(byte[] buffer, int offset, int count) throws IOException {
    checkNotClosed();
    if (buffer.length - offset < count || offset < 0 || count < 0) {
      throw new IndexOutOfBoundsException();
    }
    int toSend = count;
    while (toSend > 0) {
      int sent = Math.min(toSend, mBuffer.remaining());
      mBuffer.put(buffer, offset + count - toSend, sent);
      toSend -= sent;
      // Upload mBuffer now if an entire chunk is written.
      ensureBufferHasRemaining();
    }
  }

  @Override
  public void close() throws IOException {
    super.close();
    if (!mLastChunk) {
      // Consumer can only call close() when message loop is not running.
      // Set mLastChunk to be true and flip mBuffer to upload its contents.
      mLastChunk = true;
      mBuffer.flip();
    }
  }

  // Below are CronvoyOutputStream implementations:

  @Override
  void setConnected() throws IOException {
    // Do nothing.
  }

  @Override
  void checkReceivedEnoughContent() throws IOException {
    // Do nothing.
  }

  @Override
  UploadDataProvider getUploadDataProvider() {
    return mUploadDataProvider;
  }

  private class UploadDataProviderImpl extends UploadDataProvider {
    @Override
    public long getLength() {
      return -1;
    }

    @Override
    public void read(final UploadDataSink uploadDataSink, final ByteBuffer byteBuffer) {
      if (byteBuffer.remaining() >= mBuffer.remaining()) {
        byteBuffer.put(mBuffer);
        mBuffer.clear();
        uploadDataSink.onReadSucceeded(mLastChunk);
        if (!mLastChunk) {
          // Quit message loop so embedder can write more data.
          mMessageLoop.quit();
        }
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
   * If {@code mBuffer} is full, wait until it is consumed and there is
   * space to write more data to it.
   */
  private void ensureBufferHasRemaining() throws IOException {
    if (!mBuffer.hasRemaining()) {
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
}
