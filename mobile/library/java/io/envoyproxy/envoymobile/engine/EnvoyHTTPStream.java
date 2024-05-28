package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;

import java.nio.ByteBuffer;
import java.util.List;
import java.util.Map;

public class EnvoyHTTPStream {
  private final long engineHandle;
  private final long streamHandle;
  private final boolean explicitFlowControl;
  private final EnvoyHTTPCallbacks callbacks;

  /**
   * Start the stream via the JNI library.
   */
  void start() {
    JniLibrary.startStream(engineHandle, streamHandle, callbacks, explicitFlowControl);
  }

  /**
   * Initialize a new stream.
   * @param engineHandle Underlying handle of the Envoy engine.
   * @param streamHandle Underlying handle of the HTTP stream owned by an Envoy engine.
   * @param callbacks The callbacks for the stream.
   * @param explicitFlowControl Whether explicit flow control will be enabled for this stream.
   */
  public EnvoyHTTPStream(long engineHandle, long streamHandle, EnvoyHTTPCallbacks callbacks,
                         boolean explicitFlowControl) {
    this.engineHandle = engineHandle;
    this.streamHandle = streamHandle;
    this.explicitFlowControl = explicitFlowControl;
    this.callbacks = callbacks;
  }

  /**
   * Send headers over an open HTTP streamHandle. This method can be invoked once
   * and needs to be called before send_data.
   *
   * @param headers,   the headers to send.
   * @param endStream, supplies whether this is headers only.
   */
  public void sendHeaders(Map<String, List<String>> headers, boolean endStream) {
    JniLibrary.sendHeaders(engineHandle, streamHandle, headers, endStream);
  }

  /**
   * Send data over an open HTTP streamHandle. This method can be invoked multiple
   * times. The data length is the {@link ByteBuffer#capacity}.
   *
   * @param data,      the data to send.
   * @param endStream, supplies whether this is the last data in the streamHandle.
   * @throws UnsupportedOperationException - if the provided buffer is neither a
   *                                       direct ByteBuffer nor backed by an
   *                                       on-heap byte array.
   */
  public void sendData(ByteBuffer data, boolean endStream) {
    sendData(data, data.capacity(), endStream);
  }

  /**
   * Send data over an open HTTP streamHandle. This method can be invoked multiple
   * times.
   *
   * @param data,      the data to send.
   * @param length,    number of bytes to send: 0 <= length <= ByteBuffer.capacity()
   * @param endStream, supplies whether this is the last data in the streamHandle.
   */
  public void sendData(ByteBuffer data, int length, boolean endStream) {
    if (length < 0 || length > data.capacity()) {
      throw new IllegalArgumentException("Length out of bound");
    }
    JniLibrary.sendData(engineHandle, streamHandle, data, length, endStream);
  }

  /**
   * Read data from the response stream. Returns immediately.
   *
   * @param byteCount, Maximum number of bytes that may be be passed by the next data callback.
   * @throws UnsupportedOperationException - if explicit flow control is not enabled.
   */
  public void readData(long byteCount) {
    if (!explicitFlowControl) {
      throw new UnsupportedOperationException("Called readData without explicit flow control.");
    }
    JniLibrary.readData(engineHandle, streamHandle, byteCount);
  }

  /**
   * Send trailers over an open HTTP streamHandle. This method can only be invoked
   * once per streamHandle. Note that this method implicitly ends the
   * streamHandle.
   *
   * @param trailers, the trailers to send.
   */
  public void sendTrailers(Map<String, List<String>> trailers) {
    JniLibrary.sendTrailers(engineHandle, streamHandle, trailers);
  }

  /**
   * Cancel the stream. This functions as an interrupt, and aborts further
   * callbacks and handling of the stream.
   *
   * @return int, success unless the stream has already been canceled.
   */
  public int cancel() { return JniLibrary.resetStream(engineHandle, streamHandle); }
}
