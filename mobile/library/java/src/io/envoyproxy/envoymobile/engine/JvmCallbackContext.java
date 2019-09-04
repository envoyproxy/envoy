package io.envoyproxy.envoymobile.engine;

import java.nio.ByteBuffer;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;

class JvmCallbackContext {
  private enum FrameType {
    NONE,
    HEADERS,
    METADATA,
    TRAILERS,
  }

  private final AtomicBoolean canceled = new AtomicBoolean(false);
  private final EnvoyHTTPCallbacks callbacks;

  // State-tracking for header accumulation
  private Map<String, List<String>> headerAccumulator = null;
  private FrameType pendingFrameType = FrameType.NONE;
  private boolean pendingEndStream = false;
  private long expectedHeaderLength = 0;
  private long accumulatedHeaderLength = 0;

  public JvmCallbackContext(EnvoyHTTPCallbacks callbacks) { this.callbacks = callbacks; }

  /**
   * Initializes state for accumulating header pairs via passHeaders, ultimately to be dispatched
   * via the callback.
   *
   * @param length,    the total number of headers included in this header block.
   * @param endStream, whether this header block is the final remote frame.
   */
  public void onHeaders(long length, boolean endStream) {
    startAccumulation(FrameType.HEADERS, length, endStream);
  }

  /**
   * Allows pairs of strings to be passed across the JVM, reducing overall calls (at the expense of
   * some complexity).
   *
   * @param key,        the name of the HTTP header.
   * @param value,      the value of the HTTP header.
   * @param endHeaders, indicates this is the last header pair for this header block.
   */
  public void passHeader(byte[] key, byte[] value, boolean endHeaders) {
    String headerKey;
    String headerValue;

    try {
      headerKey = new String(key, "UTF-8");
      headerValue = new String(value, "UTF-8");
    } catch (java.io.UnsupportedEncodingException e) {
      throw new RuntimeException(e);
    }

    List<String> values = headerAccumulator.get(headerKey);
    if (values == null) {
      values = new ArrayList(1);
      headerAccumulator.put(headerKey, values);
    }
    values.add(headerValue);
    accumulatedHeaderLength++;

    if (!endHeaders) {
      return;
    }

    // Received last header, so proceed with dispatch and cleanup
    assert accumulatedHeaderLength == expectedHeaderLength;

    final Map<String, List<String>> headers = headerAccumulator;
    final FrameType frameType = pendingFrameType;
    final boolean endStream = pendingEndStream;

    Runnable runnable = new Runnable() {
      public void run() {
        if (canceled.get()) {
          return;
        }

        switch (frameType) {
        case HEADERS:
          callbacks.onHeaders(headers, endStream);
          break;
        case METADATA:
          callbacks.onMetadata(headers);
          break;
        case TRAILERS:
          callbacks.onTrailers(headers);
          break;
        case NONE:
        default:
          assert false : "missing header frame type";
        }
      }
    };

    callbacks.getExecutor().execute(runnable);

    resetHeaderAccumulation();
  }

  /**
   * Dispatches data received from the JNI layer up to the platform.
   *
   * @param data,      chunk of body data from the HTTP response.
   * @param endStream, indicates this is the last remote frame of the stream.
   */
  public void onData(byte[] data, boolean endStream) {
    callbacks.getExecutor().execute(new Runnable() {
      public void run() {
        if (canceled.get()) {
          return;
        }
        ByteBuffer dataBuffer = ByteBuffer.wrap(data);
        callbacks.onData(dataBuffer, endStream);
      }
    });
  }

  private void startAccumulation(FrameType type, long length, boolean endStream) {
    assert headerAccumulator == null;
    assert pendingFrameType == FrameType.NONE;
    assert pendingEndStream == false;
    assert expectedHeaderLength == 0;
    assert accumulatedHeaderLength == 0;

    headerAccumulator = new HashMap((int)length);
    pendingFrameType = type;
    expectedHeaderLength = length;
    pendingEndStream = endStream;
  }

  private void resetHeaderAccumulation() {
    headerAccumulator = null;
    pendingFrameType = FrameType.NONE;
    pendingEndStream = false;
    expectedHeaderLength = 0;
    accumulatedHeaderLength = 0;
  }
}
