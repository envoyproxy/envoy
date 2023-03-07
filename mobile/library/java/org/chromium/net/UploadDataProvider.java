package org.chromium.net;

import java.io.Closeable;
import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * Abstract class allowing the embedder to provide an upload body to {@link UrlRequest}. It supports
 * both non-chunked (size known in advanced) and chunked (size not known in advance) uploads. Be
 * aware that not all servers support chunked uploads.
 *
 * <p>An upload is either always chunked, across multiple uploads if the data ends up being sent
 * more than once, or never chunked.
 */
public abstract class UploadDataProvider implements Closeable {
  /**
   * If this is a non-chunked upload, returns the length of the upload. Must always return -1 if
   * this is a chunked upload.
   *
   * @return the length of the upload for non-chunked uploads, -1 otherwise.
   * @throws IOException if any IOException occurred during the process.
   */
  public abstract long getLength() throws IOException;

  /**
   * Reads upload data into {@code byteBuffer}. Upon completion, the buffer's position is updated to
   * the end of the bytes that were read. The buffer's limit is not changed. Each call of this
   * method must be followed be a single call, either synchronous or asynchronous, to {@code
   * uploadDataSink}: {@link UploadDataSink#onReadSucceeded} on success or {@link
   * UploadDataSink#onReadError} on failure. Neither read nor rewind will be called until one of
   * those methods or the other is called. Even if the associated {@link UrlRequest} is canceled,
   * one or the other must still be called before resources can be safely freed. Throwing an
   * exception will also result in resources being freed and the request being errored out.
   *
   * @param uploadDataSink The object to notify when the read has completed, successfully or
   *     otherwise.
   * @param byteBuffer The buffer to copy the read bytes into. Do not change byteBuffer's limit.
   * @throws IOException if any IOException occurred during the process. {@link
   *     UrlRequest.Callback#onFailed} will be called with the thrown exception set as the cause of
   *     the {@link CallbackException}.
   */
  public abstract void read(UploadDataSink uploadDataSink, ByteBuffer byteBuffer)
      throws IOException;

  /**
   * Rewinds upload data. Each call must be followed be a single call, either synchronous or
   * asynchronous, to {@code uploadDataSink}: {@link UploadDataSink#onRewindSucceeded} on success or
   * {@link UploadDataSink#onRewindError} on failure. Neither read nor rewind will be called until
   * one of those methods or the other is called. Even if the associated {@link UrlRequest} is
   * canceled, one or the other must still be called before resources can be safely freed. Throwing
   * an exception will also result in resources being freed and the request being errored out.
   *
   * <p>If rewinding is not supported, this should call {@link UploadDataSink#onRewindError}. Note
   * that rewinding is required to follow redirects that preserve the upload body, and for retrying
   * when the server times out stale sockets.
   *
   * @param uploadDataSink The object to notify when the rewind operation has completed,
   *     successfully or otherwise.
   * @throws IOException if any IOException occurred during the process. {@link
   *     UrlRequest.Callback#onFailed} will be called with the thrown exception set as the cause of
   *     the {@link CallbackException}.
   */
  public abstract void rewind(UploadDataSink uploadDataSink) throws IOException;

  /**
   * Called when this UploadDataProvider is no longer needed by a request, so that resources (like a
   * file) can be explicitly released.
   *
   * @throws IOException if any IOException occurred during the process. This will cause the request
   *     to fail if it is not yet complete; otherwise it will be logged.
   */
  @Override
  public void close() throws IOException {}
}
