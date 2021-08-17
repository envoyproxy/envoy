package io.envoyproxy.envoymobile.engine;

import java.nio.ByteBuffer;
import java.util.List;
import java.util.Map;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilter;

/**
 * Wrapper class for EnvoyHTTPFilter for receiving JNI calls.
 */
class JvmFilterContext {
  private final JvmBridgeUtility headerUtility;
  private final JvmBridgeUtility trailerUtility;
  private final EnvoyHTTPFilter filter;

  public JvmFilterContext(EnvoyHTTPFilter filter) {
    headerUtility = new JvmBridgeUtility();
    trailerUtility = new JvmBridgeUtility();
    this.filter = filter;
  }

  /**
   * Delegates header retrieval to the bridge utility.
   *
   * @param key,        the name of the HTTP header.
   * @param value,      the value of the HTTP header.
   * @param start,      indicates this is the first header pair of the block.
   */
  public void passHeader(byte[] key, byte[] value, boolean start) {
    headerUtility.passHeader(key, value, start);
  }

  /**
   * Delegates trailer retrieval to the secondary bridge utility.
   *
   * @param key,        the name of the HTTP trailer.
   * @param value,      the value of the HTTP trailer.
   * @param start,      indicates this is the first trailer pair of the block.
   */
  public void passTrailer(byte[] key, byte[] value, boolean start) {
    trailerUtility.passHeader(key, value, start);
  }

  /**
   * Invokes onHeaders callback using headers passed via passHeaders.
   *
   * @param headerCount, the total number of headers included in this header block.
   * @param endStream,   whether this header block is the final remote frame.
   * @param streamIntel, internal HTTP stream metrics, context, and other details.
   * @return Object[],   pair of HTTP filter status and optional modified headers.
   */
  public Object onRequestHeaders(long headerCount, boolean endStream, long[] streamIntel) {
    assert headerUtility.validateCount(headerCount);
    final Map headers = headerUtility.retrieveHeaders();
    return toJniFilterHeadersStatus(
        filter.onRequestHeaders(headers, endStream, new EnvoyStreamIntelImpl(streamIntel)));
  }

  /**
   * Dispatches data received from the JNI layer up to the platform.
   *
   * @param data,        chunk of body data from the HTTP request.
   * @param endStream,   indicates this is the last remote frame of the stream.
   * @param streamIntel, internal HTTP stream metrics, context, and other details.
   * @return Object[],   pair of HTTP filter status and optional modified data.
   */
  public Object onRequestData(byte[] data, boolean endStream, long[] streamIntel) {
    ByteBuffer dataBuffer = ByteBuffer.wrap(data);
    return toJniFilterDataStatus(
        filter.onRequestData(dataBuffer, endStream, new EnvoyStreamIntelImpl(streamIntel)));
  }

  /**
   * Invokes onTrailers callback using trailers passed via passHeaders.
   *
   * @param trailerCount, the total number of trailers included in this header block.
   * @param streamIntel,  internal HTTP stream metrics, context, and other details.
   * @return Object[],    pair of HTTP filter status and optional modified trailers.
   */
  public Object onRequestTrailers(long trailerCount, long[] streamIntel) {
    assert headerUtility.validateCount(trailerCount);
    final Map trailers = headerUtility.retrieveHeaders();
    return toJniFilterTrailersStatus(
        filter.onRequestTrailers(trailers, new EnvoyStreamIntelImpl(streamIntel)));
  }

  /**
   * Invokes onHeaders callback using headers passed via passHeaders.
   *
   * @param headerCount, the total number of headers included in this header block.
   * @param endStream,   whether this header block is the final remote frame.
   * @param streamIntel, internal HTTP stream metrics, context, and other details.
   * @return Object[],   pair of HTTP filter status and optional modified headers.
   */
  public Object onResponseHeaders(long headerCount, boolean endStream, long[] streamIntel) {
    assert headerUtility.validateCount(headerCount);
    final Map headers = headerUtility.retrieveHeaders();
    return toJniFilterHeadersStatus(
        filter.onResponseHeaders(headers, endStream, new EnvoyStreamIntelImpl(streamIntel)));
  }

  /**
   * Dispatches data received from the JNI layer up to the platform.
   *
   * @param data,        chunk of body data from the HTTP response.
   * @param endStream,   indicates this is the last remote frame of the stream.
   * @param streamIntel, internal HTTP stream metrics, context, and other details.
   * @return Object[],   pair of HTTP filter status and optional modified data.
   */
  public Object onResponseData(byte[] data, boolean endStream, long[] streamIntel) {
    ByteBuffer dataBuffer = ByteBuffer.wrap(data);
    return toJniFilterDataStatus(
        filter.onResponseData(dataBuffer, endStream, new EnvoyStreamIntelImpl(streamIntel)));
  }

  /**
   * Invokes onTrailers callback using trailers passed via passHeaders.
   *
   * @param trailerCount, the total number of trailers included in this header block.
   * @param streamIntel,  internal HTTP stream metrics, context, and other details.
   * @return Object[],    pair of HTTP filter status and optional modified trailers.
   */
  public Object onResponseTrailers(long trailerCount, long[] streamIntel) {
    assert headerUtility.validateCount(trailerCount);
    final Map trailers = headerUtility.retrieveHeaders();
    return toJniFilterTrailersStatus(
        filter.onResponseTrailers(trailers, new EnvoyStreamIntelImpl(streamIntel)));
  }

  /**
   * Invokes onResumeRequest callback with pending HTTP entities.
   *
   * @param headerCount,  total pending headers included in the header block.
   * @param data,         buffered body data.
   * @param trailerCount, total pending trailers included in the trailer block.
   * @param endStream,    whether the stream is closed at this point.
   * @param streamIntel,  internal HTTP stream metrics, context, and other details.
   * @return Object[],    tuple of status with updated entities to be forwarded.
   */
  public Object onResumeRequest(long headerCount, byte[] data, long trailerCount, boolean endStream,
                                long[] streamIntel) {
    // Headers are optional in this call, and a negative length indicates omission.
    Map<String, List<String>> headers = null;
    if (headerCount >= 0) {
      assert headerUtility.validateCount(headerCount);
      headers = headerUtility.retrieveHeaders();
    }
    ByteBuffer dataBuffer = data == null ? null : ByteBuffer.wrap(data);
    // Trailers are optional in this call, and a negative length indicates omission.
    Map<String, List<String>> trailers = null;
    if (trailerCount >= 0) {
      assert trailerUtility.validateCount(trailerCount);
      trailers = trailerUtility.retrieveHeaders();
    }
    return toJniFilterResumeStatus(filter.onResumeRequest(headers, dataBuffer, trailers, endStream,
                                                          new EnvoyStreamIntelImpl(streamIntel)));
  }

  /**
   * Invokes onResumeResponse callback with pending HTTP entities.
   *
   * @param headerCount,  total pending headers included in the header block.
   * @param data,         buffered body data.
   * @param trailerCount, total pending trailers included in the trailer block.
   * @param endStream,    whether the stream is closed at this point.
   * @param streamIntel,  internal HTTP stream metrics, context, and other details.
   * @return Object[],    tuple of status with updated entities to be forwarded.
   */
  public Object onResumeResponse(long headerCount, byte[] data, long trailerCount,
                                 boolean endStream, long[] streamIntel) {
    // Headers are optional in this call, and a negative length indicates omission.
    Map<String, List<String>> headers = null;
    if (headerCount >= 0) {
      assert headerUtility.validateCount(headerCount);
      headers = headerUtility.retrieveHeaders();
    }
    ByteBuffer dataBuffer = data == null ? null : ByteBuffer.wrap(data);
    // Trailers are optional in this call, and a negative length indicates omission.
    Map<String, List<String>> trailers = null;
    if (trailerCount >= 0) {
      assert trailerUtility.validateCount(trailerCount);
      trailers = trailerUtility.retrieveHeaders();
    }
    return toJniFilterResumeStatus(filter.onResumeResponse(headers, dataBuffer, trailers, endStream,
                                                           new EnvoyStreamIntelImpl(streamIntel)));
  }

  /**
   * Sets request filter callbacks with memory-managed wrapper around native implementation.
   *
   * @param callbackHandle, native identifier for resource management.
   */
  public void setRequestFilterCallbacks(long callbackHandle) {
    filter.setRequestFilterCallbacks(EnvoyHTTPFilterCallbacksImpl.create(callbackHandle));
  }

  /**
   * Sets response filter callbacks with memory-managed wrapper around native implementation.
   *
   * @param callbackHandle, native identifier for resource management.
   */
  public void setResponseFilterCallbacks(long callbackHandle) {
    filter.setResponseFilterCallbacks(EnvoyHTTPFilterCallbacksImpl.create(callbackHandle));
  }

  /**
   * Dispatches error received from the JNI layer up to the platform.
   *
   * @param errorCode,    the error code.
   * @param message,      the error message.
   * @param attemptCount, the number of times an operation was attempted before firing this error.
   * @param streamIntel,  internal HTTP stream metrics, context, and other details.
   * @return Object,      not used in HTTP filters.
   */
  public Object onError(int errorCode, byte[] message, int attemptCount, long[] streamIntel) {
    String errorMessage = new String(message);
    filter.onError(errorCode, errorMessage, attemptCount, new EnvoyStreamIntelImpl(streamIntel));
    return null;
  }

  /**
   * Dispatches cancellation notice up to the platform.
   *
   * @param streamIntel, internal HTTP stream metrics, context, and other details.
   * @return Object,     not used in HTTP filters.
   */
  public Object onCancel(long[] streamIntel) {
    filter.onCancel(new EnvoyStreamIntelImpl(streamIntel));
    return null;
  }

  private static byte[][] toJniHeaders(Object headers) {
    return JniBridgeUtility.toJniHeaders((Map<String, List<String>>)headers);
  }

  private static Object[] toJniFilterHeadersStatus(Object[] result) {
    assert result.length == 2;
    result[1] = toJniHeaders(result[1]);
    return result;
  }

  private static Object[] toJniFilterDataStatus(Object[] result) {
    if (result.length == 3) {
      // Convert optionally-included headers on ResumeIteration.
      result[2] = toJniHeaders(result[2]);
      return result;
    }
    assert result.length == 2;
    return result;
  }

  private static Object[] toJniFilterTrailersStatus(Object[] result) {
    result[1] = toJniHeaders(result[1]);
    if (result.length == 4) {
      // Convert optionally-included headers on ResumeIteration.
      result[2] = toJniHeaders(result[2]);
      return result;
    }
    assert result.length == 2;
    return result;
  }

  private static Object[] toJniFilterResumeStatus(Object[] result) {
    assert result.length == 4;
    result[1] = toJniHeaders(result[1]);
    result[3] = toJniHeaders(result[3]);
    return result;
  }
}
