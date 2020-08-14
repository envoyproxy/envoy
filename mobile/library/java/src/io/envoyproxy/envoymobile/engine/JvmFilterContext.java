package io.envoyproxy.envoymobile.engine;

import java.nio.ByteBuffer;
import java.util.Map;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilter;

/**
 * Wrapper class for EnvoyHTTPFilter for receiving JNI calls.
 */
class JvmFilterContext {
  private final JvmBridgeUtility bridgeUtility;
  private final EnvoyHTTPFilter filter;

  public JvmFilterContext(EnvoyHTTPFilter filter) {
    bridgeUtility = new JvmBridgeUtility();
    this.filter = filter;
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
   * @return Object,     not used for request filter.
   */
  public Object onRequestHeaders(long headerCount, boolean endStream) {
    assert bridgeUtility.validateCount(headerCount);
    final Map headers = bridgeUtility.retrieveHeaders();
    return filter.onRequestHeaders(headers, endStream);
  }

  /**
   * Dispatches data received from the JNI layer up to the platform.
   *
   * @param data,      chunk of body data from the HTTP request.
   * @param endStream, indicates this is the last remote frame of the stream.
   * @return Object,   not used for request filter.
   */
  public Object onRequestData(byte[] data, boolean endStream) {
    ByteBuffer dataBuffer = ByteBuffer.wrap(data);
    return filter.onRequestData(dataBuffer, endStream);
  }

  /**
   * Invokes onTrailers callback using trailers passed via passHeaders.
   *
   * @param trailerCount, the total number of trailers included in this header block.
   * @return Object,      not used for request filter.
   */
  public Object onRequestTrailers(long trailerCount) {
    assert bridgeUtility.validateCount(trailerCount);
    final Map trailers = bridgeUtility.retrieveHeaders();
    return filter.onRequestTrailers(trailers);
  }

  /**
   * Invokes onHeaders callback using headers passed via passHeaders.
   *
   * @param headerCount, the total number of headers included in this header block.
   * @param endStream,   whether this header block is the final remote frame.
   * @return Object,     not used for response filter.
   */
  public Object onResponseHeaders(long headerCount, boolean endStream) {
    assert bridgeUtility.validateCount(headerCount);
    final Map headers = bridgeUtility.retrieveHeaders();
    return filter.onResponseHeaders(headers, endStream);
  }

  /**
   * Dispatches data received from the JNI layer up to the platform.
   *
   * @param data,      chunk of body data from the HTTP response.
   * @param endStream, indicates this is the last remote frame of the stream.
   * @return Object,   not used for response filter.
   */
  public Object onResponseData(byte[] data, boolean endStream) {
    ByteBuffer dataBuffer = ByteBuffer.wrap(data);
    return filter.onResponseData(dataBuffer, endStream);
  }

  /**
   * Invokes onTrailers callback using trailers passed via passHeaders.
   *
   * @param trailerCount, the total number of trailers included in this header block.
   * @return Object,      not used for response filter.
   */
  public Object onResponseTrailers(long trailerCount) {
    assert bridgeUtility.validateCount(trailerCount);
    final Map trailers = bridgeUtility.retrieveHeaders();
    return filter.onResponseTrailers(trailers);
  }
}
