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
   * @return Object[],   pair of HTTP filter status and optional modified headers.
   */
  public Object onRequestHeaders(long headerCount, boolean endStream) {
    assert headerUtility.validateCount(headerCount);
    final Map headers = headerUtility.retrieveHeaders();
    return toJniFilterHeadersStatus(filter.onRequestHeaders(headers, endStream));
  }

  /**
   * Dispatches data received from the JNI layer up to the platform.
   *
   * @param data,      chunk of body data from the HTTP request.
   * @param endStream, indicates this is the last remote frame of the stream.
   * @return Object[], pair of HTTP filter status and optional modified data.
   */
  public Object onRequestData(byte[] data, boolean endStream) {
    ByteBuffer dataBuffer = ByteBuffer.wrap(data);
    return filter.onRequestData(dataBuffer, endStream);
  }

  /**
   * Invokes onTrailers callback using trailers passed via passHeaders.
   *
   * @param trailerCount, the total number of trailers included in this header block.
   * @return Object[],    pair of HTTP filter status and optional modified trailers.
   */
  public Object onRequestTrailers(long trailerCount) {
    assert headerUtility.validateCount(trailerCount);
    final Map trailers = headerUtility.retrieveHeaders();
    return toJniFilterHeadersStatus(filter.onRequestTrailers(trailers));
  }

  /**
   * Invokes onHeaders callback using headers passed via passHeaders.
   *
   * @param headerCount, the total number of headers included in this header block.
   * @param endStream,   whether this header block is the final remote frame.
   * @return Object[],   pair of HTTP filter status and optional modified headers.
   */
  public Object onResponseHeaders(long headerCount, boolean endStream) {
    assert headerUtility.validateCount(headerCount);
    final Map headers = headerUtility.retrieveHeaders();
    return toJniFilterHeadersStatus(filter.onResponseHeaders(headers, endStream));
  }

  /**
   * Dispatches data received from the JNI layer up to the platform.
   *
   * @param data,      chunk of body data from the HTTP response.
   * @param endStream, indicates this is the last remote frame of the stream.
   * @return Object[], pair of HTTP filter status and optional modified data.
   */
  public Object onResponseData(byte[] data, boolean endStream) {
    ByteBuffer dataBuffer = ByteBuffer.wrap(data);
    return filter.onResponseData(dataBuffer, endStream);
  }

  /**
   * Invokes onTrailers callback using trailers passed via passHeaders.
   *
   * @param trailerCount, the total number of trailers included in this header block.
   * @return Object[],    pair of HTTP filter status and optional modified trailers.
   */
  public Object onResponseTrailers(long trailerCount) {
    assert headerUtility.validateCount(trailerCount);
    final Map trailers = headerUtility.retrieveHeaders();
    return toJniFilterHeadersStatus(filter.onResponseTrailers(trailers));
  }

  /**
   *
   */
  public Object onResumeRequest(long headerCount, byte[] data, long trailerCount,
                                boolean endStream) {
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
    return filter.onResumeRequest(headers, dataBuffer, trailers, endStream);
  }

  /**
   *
   */
  public Object onResumeResponse(long headerCount, byte[] data, long trailerCount,
                                 boolean endStream) {
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
    return filter.onResumeResponse(headers, dataBuffer, trailers, endStream);
  }

  /**
   *
   */
  public void setRequestFilterCallbacks(long callbackHandle) {
    filter.setRequestFilterCallbacks(EnvoyHTTPFilterCallbacksImpl.create(callbackHandle));
  }

  /**
   *
   */
  public void setResponseFilterCallbacks(long callbackHandle) {
    filter.setResponseFilterCallbacks(EnvoyHTTPFilterCallbacksImpl.create(callbackHandle));
  }

  private static byte[][] toJniHeaders(Object headers) {
    return JniBridgeUtility.toJniHeaders((Map<String, List<String>>)headers);
  }

  private static Object[] toJniFilterHeadersStatus(Object[] result) {
    assert result.length == 2;
    result[1] = toJniHeaders(result[1]);
    return result;
  }
}
