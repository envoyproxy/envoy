package org.chromium.net;

import android.os.ParcelFileDescriptor;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;

/** Provides implementations of {@link UploadDataProvider} for common use cases. */
public final class UploadDataProviders {
  /**
   * Uploads an entire file.
   *
   * @param file The file to upload
   * @return A new UploadDataProvider for the given file
   */
  public static UploadDataProvider create(final File file) {
    return new FileUploadProvider(new FileChannelProvider() {
      @Override
      public FileChannel getChannel() throws IOException {
        return new FileInputStream(file).getChannel();
      }
    });
  }

  /**
   * Uploads an entire file, closing the descriptor when it is no longer needed.
   *
   * @param fd The file descriptor to upload
   * @throws IllegalArgumentException if {@code fd} is not a file.
   * @return A new UploadDataProvider for the given file descriptor
   */
  public static UploadDataProvider create(final ParcelFileDescriptor fd) {
    return new FileUploadProvider(new FileChannelProvider() {
      @Override
      public FileChannel getChannel() throws IOException {
        if (fd.getStatSize() != -1) {
          return new ParcelFileDescriptor.AutoCloseInputStream(fd).getChannel();
        } else {
          fd.close();
          throw new IllegalArgumentException("Not a file: " + fd);
        }
      }
    });
  }

  /**
   * Uploads a ByteBuffer, from the current {@code buffer.position()} to {@code buffer.limit()}
   *
   * @param buffer The data to upload
   * @return A new UploadDataProvider for the given buffer
   */
  public static UploadDataProvider create(ByteBuffer buffer) {
    return new ByteBufferUploadProvider(buffer.slice());
  }

  /**
   * Uploads {@code length} bytes from {@code data}, starting from {@code offset}
   *
   * @param data Array containing data to upload
   * @param offset Offset within data to start with
   * @param length Number of bytes to upload
   * @return A new UploadDataProvider for the given data
   */
  public static UploadDataProvider create(byte[] data, int offset, int length) {
    return new ByteBufferUploadProvider(ByteBuffer.wrap(data, offset, length).slice());
  }

  /**
   * Uploads the contents of {@code data}
   *
   * @param data Array containing data to upload
   * @return A new UploadDataProvider for the given data
   */
  public static UploadDataProvider create(byte[] data) { return create(data, 0, data.length); }

  private interface FileChannelProvider {
    FileChannel getChannel() throws IOException;
  }

  private static final class FileUploadProvider extends UploadDataProvider {
    private volatile FileChannel mChannel;
    private final FileChannelProvider mProvider;
    /** Guards initialization of {@code mChannel} */
    private final Object mLock = new Object();

    private FileUploadProvider(FileChannelProvider provider) { this.mProvider = provider; }

    @Override
    public long getLength() throws IOException {
      return getChannel().size();
    }

    @Override
    public void read(UploadDataSink uploadDataSink, ByteBuffer byteBuffer) throws IOException {
      if (!byteBuffer.hasRemaining()) {
        throw new IllegalStateException("Cronet passed a buffer with no bytes remaining");
      }
      FileChannel channel = getChannel();
      int bytesRead = 0;
      while (bytesRead == 0) {
        int read = channel.read(byteBuffer);
        if (read == -1) {
          break;
        } else {
          bytesRead += read;
        }
      }
      uploadDataSink.onReadSucceeded(false);
    }

    @Override
    public void rewind(UploadDataSink uploadDataSink) throws IOException {
      getChannel().position(0);
      uploadDataSink.onRewindSucceeded();
    }

    /**
     * Lazily initializes the channel so that a blocking operation isn't performed on a non-executor
     * thread.
     */
    private FileChannel getChannel() throws IOException {
      if (mChannel == null) {
        synchronized (mLock) {
          if (mChannel == null) {
            mChannel = mProvider.getChannel();
          }
        }
      }
      return mChannel;
    }

    @Override
    public void close() throws IOException {
      FileChannel channel = mChannel;
      if (channel != null) {
        channel.close();
      }
    }
  }

  private static final class ByteBufferUploadProvider extends UploadDataProvider {
    private final ByteBuffer mUploadBuffer;

    private ByteBufferUploadProvider(ByteBuffer uploadBuffer) { this.mUploadBuffer = uploadBuffer; }

    @Override
    public long getLength() {
      return mUploadBuffer.limit();
    }

    @Override
    public void read(UploadDataSink uploadDataSink, ByteBuffer byteBuffer) {
      if (!byteBuffer.hasRemaining()) {
        throw new IllegalStateException("Cronet passed a buffer with no bytes remaining");
      }
      if (byteBuffer.remaining() >= mUploadBuffer.remaining()) {
        byteBuffer.put(mUploadBuffer);
      } else {
        int oldLimit = mUploadBuffer.limit();
        mUploadBuffer.limit(mUploadBuffer.position() + byteBuffer.remaining());
        byteBuffer.put(mUploadBuffer);
        mUploadBuffer.limit(oldLimit);
      }
      uploadDataSink.onReadSucceeded(false);
    }

    @Override
    public void rewind(UploadDataSink uploadDataSink) {
      mUploadBuffer.position(0);
      uploadDataSink.onRewindSucceeded();
    }
  }

  // Prevent instantiation
  private UploadDataProviders() {}
}
