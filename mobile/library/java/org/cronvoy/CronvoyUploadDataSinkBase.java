package org.cronvoy;

import androidx.annotation.IntDef;
import java.io.IOException;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.nio.ByteBuffer;
import java.util.Locale;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.AtomicInteger;
import org.chromium.net.UploadDataProvider;
import org.chromium.net.UploadDataSink;

/**
 * Base class for Java UrlRequest implementations of UploadDataSink. Handles asynchronicity and
 * manages the executors for this upload.
 */
abstract class CronvoyUploadDataSinkBase extends UploadDataSink {

  @IntDef({SinkState.AWAITING_READ_RESULT, SinkState.AWAITING_REWIND_RESULT, SinkState.UPLOADING,
           SinkState.NOT_STARTED})
  @Retention(RetentionPolicy.SOURCE)
  @interface SinkState {
    int AWAITING_READ_RESULT = 0;
    int AWAITING_REWIND_RESULT = 1;
    int UPLOADING = 2;
    int NOT_STARTED = 3;
  }

  static final int DEFAULT_UPLOAD_BUFFER_SIZE = 8192;

  private final AtomicInteger /*SinkState*/ sinkState = new AtomicInteger(SinkState.NOT_STARTED);
  private final Executor userUploadExecutor;
  private final Executor executor;
  private final UploadDataProvider uploadProvider;
  private ByteBuffer buffer;
  /** This holds the total bytes to send (the content-length). -1 if unknown. */
  private long totalBytes;
  /** This holds the bytes written so far */
  private long writtenBytes;

  CronvoyUploadDataSinkBase(final Executor userExecutor, Executor executor,
                            UploadDataProvider provider) {
    userUploadExecutor = runnable -> {
      try {
        userExecutor.execute(runnable);
      } catch (RejectedExecutionException e) {
        processUploadError(e);
      }
    };
    this.executor = executor;
    uploadProvider = provider;
  }

  @Override
  public void onReadSucceeded(final boolean finalChunk) {
    if (!sinkState.compareAndSet(SinkState.AWAITING_READ_RESULT, SinkState.UPLOADING)) {
      throw new IllegalStateException(
          "onReadSucceeded() called when not awaiting a read result; in state: " + sinkState.get());
    }
    executor.execute(getErrorSettingRunnable(() -> {
      buffer.flip();
      if (totalBytes != -1 && totalBytes - writtenBytes < buffer.remaining()) {
        processUploadError(new IllegalArgumentException(String.format(
            Locale.getDefault(), "Read upload data length %d exceeds expected length %d",
            writtenBytes + buffer.remaining(), totalBytes)));
        return;
      }

      writtenBytes += processSuccessfulRead(
          buffer, writtenBytes + buffer.remaining() == totalBytes || finalChunk);

      if (writtenBytes < totalBytes || (totalBytes == -1 && !finalChunk)) {
        buffer.clear();
        sinkState.set(SinkState.AWAITING_READ_RESULT);
        executeOnUploadExecutor(() -> uploadProvider.read(CronvoyUploadDataSinkBase.this, buffer));
      } else if (totalBytes == -1) {
        finish();
      } else if (totalBytes == writtenBytes) {
        finish();
      } else {
        processUploadError(new IllegalArgumentException(String.format(
            Locale.getDefault(), "Read upload data length %d exceeds expected length %d",
            writtenBytes, totalBytes)));
      }
    }));
  }

  @Override
  public void onRewindSucceeded() {
    if (!sinkState.compareAndSet(SinkState.AWAITING_REWIND_RESULT, SinkState.UPLOADING)) {
      throw new IllegalStateException(
          "onRewindSucceeded() called when not awaiting a rewind; in state: " + sinkState.get());
    }
    startRead();
  }

  @Override
  public void onReadError(Exception exception) {
    processUploadError(exception);
  }

  @Override
  public void onRewindError(Exception exception) {
    processUploadError(exception);
  }

  private void startRead() {
    executor.execute(getErrorSettingRunnable(() -> {
      sinkState.set(SinkState.AWAITING_READ_RESULT);
      executeOnUploadExecutor(() -> uploadProvider.read(CronvoyUploadDataSinkBase.this, buffer));
    }));
  }

  /**
   * Helper method to execute a checked runnable on the upload executor and process any errors that
   * occur as upload errors.
   *
   * @param runnable the runnable to attempt to run and check for errors
   */
  private void executeOnUploadExecutor(Executors.CheckedRunnable runnable) {
    try {
      userUploadExecutor.execute(getUploadErrorSettingRunnable(runnable));
    } catch (RejectedExecutionException e) {
      processUploadError(e);
    }
  }

  /**
   * Starts the upload. This method can be called multiple times. If it is not the first time it is
   * called the {@link UploadDataProvider} must rewind.
   *
   * @param firstTime true if this is the first time this {@link UploadDataSink} has started an
   *     upload
   */
  void start(final boolean firstTime) {
    executeOnUploadExecutor(() -> {
      totalBytes = uploadProvider.getLength();
      if (totalBytes == 0) {
        finish();
      } else {
        // If we know how much data we have to upload, and it's small, we can save
        // memory by allocating a reasonably sized buffer to read into.
        if (totalBytes > 0 && totalBytes < DEFAULT_UPLOAD_BUFFER_SIZE) {
          // Allocate one byte more than necessary, to detect callers uploading
          // more bytes than they specified in length.
          buffer = ByteBuffer.allocateDirect((int)totalBytes + 1);
        } else {
          buffer = ByteBuffer.allocateDirect(DEFAULT_UPLOAD_BUFFER_SIZE);
        }

        if (firstTime) {
          startRead();
        } else {
          sinkState.set(SinkState.AWAITING_REWIND_RESULT);
          uploadProvider.rewind(CronvoyUploadDataSinkBase.this);
        }
      }
    });
  }

  /**
   * Gets a runnable that checks for errors and processes them by setting an error state when
   * executing a {@link Executors.CheckedRunnable}.
   *
   * @param runnable The runnable to run.
   * @return a runnable that checks for errors
   */
  abstract Runnable getErrorSettingRunnable(Executors.CheckedRunnable runnable);

  /**
   * Gets a runnable that checks for errors and processes them by setting an upload error state when
   * executing a {@link Executors.CheckedRunnable}.
   *
   * @param runnable The runnable to run.
   * @return a runnable that checks for errors
   */
  abstract Runnable getUploadErrorSettingRunnable(Executors.CheckedRunnable runnable);

  /**
   * Processes an error encountered while uploading data.
   *
   * @param error the {@link Throwable} to process
   */
  abstract void processUploadError(final Throwable error);

  /**
   * Called when a successful read has occurred and there is new data in the {@code buffer} to
   * process.
   *
   * @return the number of bytes processed in this read
   */
  abstract int processSuccessfulRead(ByteBuffer buffer, boolean finalChunk);

  /**
   * Finishes this upload. Called when the upload is complete.
   */
  abstract void finish() throws IOException;
}
