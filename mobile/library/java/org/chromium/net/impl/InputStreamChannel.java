package org.chromium.net.impl;

import androidx.annotation.NonNull;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.channels.ReadableByteChannel;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Adapts an {@link InputStream} into a {@link ReadableByteChannel}, exactly like
 * {@link java.nio.channels.Channels#newChannel(InputStream)} does, but more efficiently, since it
 * does not allocate a temporary buffer if it doesn't have to, and it freely takes advantage of
 * {@link FileInputStream}'s trivial conversion to {@link java.nio.channels.FileChannel}.
 */
final class InputStreamChannel implements ReadableByteChannel {
  private static final int MAX_TMP_BUFFER_SIZE = 16384;
  private static final int MIN_TMP_BUFFER_SIZE = 4096;
  private final InputStream mInputStream;
  private final AtomicBoolean mIsOpen = new AtomicBoolean(true);

  private InputStreamChannel(@NonNull InputStream inputStream) { mInputStream = inputStream; }

  static ReadableByteChannel wrap(@NonNull InputStream inputStream) {
    if (inputStream instanceof FileInputStream) {
      return ((FileInputStream)inputStream).getChannel();
    }
    return new InputStreamChannel(inputStream);
  }

  @Override
  public int read(ByteBuffer dst) throws IOException {
    final int read;
    if (dst.hasArray()) {
      read = mInputStream.read(dst.array(), dst.arrayOffset() + dst.position(), dst.remaining());
      if (read > 0) {
        dst.position(dst.position() + read);
      }
    } else {
      // Since we're allocating a buffer for every read, we want to choose a good size - on
      // Android, the only case where a ByteBuffer won't have a backing byte[] is if it was
      // created wrapping a void * in native code, or if it represents a memory-mapped file.
      // Especially in the latter case, we want to avoid allocating a buffer that could be
      // very large.
      final int possibleToRead =
          Math.min(Math.max(mInputStream.available(), MIN_TMP_BUFFER_SIZE), dst.remaining());
      final int reasonableToRead = Math.min(MAX_TMP_BUFFER_SIZE, possibleToRead);
      byte[] tmpBuf = new byte[reasonableToRead];
      read = mInputStream.read(tmpBuf);
      if (read > 0) {
        dst.put(tmpBuf, 0, read);
      }
    }
    return read;
  }

  @Override
  public boolean isOpen() {
    return mIsOpen.get();
  }

  @Override
  public void close() throws IOException {
    if (mIsOpen.compareAndSet(true, false)) {
      mInputStream.close();
    }
  }
}
