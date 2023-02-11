package io.envoyproxy.envoymobile.engine;

import java.nio.ByteBuffer;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;

class JvmCallbackContext {
  private final JvmBridgeUtility bridgeUtility;
  private final EnvoyHTTPCallbacks callbacks;

  public JvmCallbackContext(EnvoyHTTPCallbacks callbacks) {
    bridgeUtility = new JvmBridgeUtility();
    this.callbacks = callbacks;
  }

  /**
   * Delegates header retrieval to the bridge utility.
   *
   * @param key,        the name of the HTTP header.
   * @param value,      the value of the HTTP header.
   * @param start,      indicates this is the first header pair of the block.
   */
  void passHeader(byte[] key, byte[] value, boolean start) {
    bridgeUtility.passHeader(key, value, start);
  }

  /**
   * Invokes onHeaders callback using headers passed via passHeaders.
   *
   * @param headerCount, the total number of headers included in this header block.
   * @param endStream,   whether this header block is the final remote frame.
   * @param streamIntel, internal HTTP stream metrics, context, and other details.
   * @return Object,     not used for response callbacks.
   */
  public Object onResponseHeaders(long headerCount, boolean endStream, long[] streamIntel) {
    assert bridgeUtility.validateCount(headerCount);
    final Map headers = bridgeUtility.retrieveHeaders();

    callbacks.getExecutor().execute(new Runnable() {
      public void run() {
        callbacks.onHeaders(headers, endStream, new EnvoyStreamIntelImpl(streamIntel));
      }
    });

    return null;
  }

  /**
   * Invokes onTrailers callback using trailers passed via passHeaders.
   *
   * @param trailerCount, the total number of trailers included in this header block.
   * @param streamIntel,  internal HTTP stream metrics, context, and other details.
   * @return Object,      not used for response callbacks.
   */
  public Object onResponseTrailers(long trailerCount, long[] streamIntel) {
    assert bridgeUtility.validateCount(trailerCount);
    final Map trailers = bridgeUtility.retrieveHeaders();

    callbacks.getExecutor().execute(new Runnable() {
      public void run() { callbacks.onTrailers(trailers, new EnvoyStreamIntelImpl(streamIntel)); }
    });

    return null;
  }

  /**
   * Dispatches data received from the JNI layer up to the platform.
   *
   * @param data,        chunk of body data from the HTTP response.
   * @param endStream,   indicates this is the last remote frame of the stream.
   * @param streamIntel, internal HTTP stream metrics, context, and other details.
   * @return Object,     not used for response callbacks.
   */
  public Object onResponseData(byte[] data, boolean endStream, long[] streamIntel) {
    callbacks.getExecutor().execute(new Runnable() {
      public void run() {
        ByteBuffer dataBuffer = ByteBuffer.wrap(data);
        callbacks.onData(dataBuffer, endStream, new EnvoyStreamIntelImpl(streamIntel));
      }
    });

    return null;
  }

  /**
   * Dispatches error received from the JNI layer up to the platform.
   *
   * @param errorCode,         the error code.
   * @param message,           the error message.
   * @param attemptCount,      the number of times an operation was attempted before firing this
   *     error.
   * @param streamIntel,       internal HTTP stream metrics, context, and other details.
   * @param finalStreamIntel,  final internal HTTP stream metrics, context, and other details.
   * @return Object,           not used for response callbacks.
   */
  public Object onError(int errorCode, byte[] message, int attemptCount, long[] streamIntel,
                        long[] finalStreamIntel) {
    callbacks.getExecutor().execute(new Runnable() {
      public void run() {
        String errorMessage = new String(message);
        callbacks.onError(errorCode, errorMessage, attemptCount,
                          new EnvoyStreamIntelImpl(streamIntel),
                          new EnvoyFinalStreamIntelImpl(finalStreamIntel));
      }
    });

    return null;
  }

  /**
   * Dispatches cancellation notice up to the platform
   *
   * @param streamIntel,       internal HTTP stream metrics, context, and other details.
   * @param finalStreamIntel, final internal HTTP stream metrics, context, and other details.
   * @return Object, not used for response callbacks.
   */
  public Object onCancel(long[] streamIntel, long[] finalStreamIntel) {
    callbacks.getExecutor().execute(new Runnable() {
      public void run() {
        // This call is atomically gated at the call-site and will only happen once.
        callbacks.onCancel(new EnvoyStreamIntelImpl(streamIntel),
                           new EnvoyFinalStreamIntelImpl(finalStreamIntel));
      }
    });

    return null;
  }

  /**
   * Dispatches onSendWindowAvailable notice up to the platform
   *
   * @param streamIntel, internal HTTP stream metrics, context, and other details.
   * @return Object, not used for response callbacks.
   */
  public Object onSendWindowAvailable(long[] streamIntel) {
    callbacks.getExecutor().execute(new Runnable() {
      public void run() {
        // This call is atomically gated at the call-site and will only happen once.
        callbacks.onSendWindowAvailable(new EnvoyStreamIntelImpl(streamIntel));
      }
    });

    return null;
  }
  /**
   * Called with all stream metrics after the final headers/data/trailers call.
   *
   * @param streamIntel,       internal HTTP stream metrics, context, and other details.
   * @param finalStreamIntel, final internal HTTP stream metrics for the end of stream.
   * @return Object, not used for response callbacks.
   */
  public Object onComplete(long[] streamIntel, long[] finalStreamIntel) {
    callbacks.getExecutor().execute(new Runnable() {
      public void run() {
        // This call is atomically gated at the call-site and will only happen once.
        callbacks.onComplete(new EnvoyStreamIntelImpl(streamIntel),
                             new EnvoyFinalStreamIntelImpl(finalStreamIntel));
      }
    });

    return null;
  }
}
