package io.envoyproxy.envoymobile.engine;

import java.nio.ByteBuffer;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;

/**
 * Class to assist with passing types from native code over the JNI. Currently supports
 * HTTP headers.
 */
class JvmBridgeUtility {
  // State-tracking for header accumulation
  private Map<String, List<String>> headerAccumulator = null;
  private long headerCount = 0;

  JvmBridgeUtility() {}

  /**
   * Receives pairs of strings passed via the JNI.
   *
   * @param key,        the name of the HTTP header.
   * @param value,      the value of the HTTP header.
   * @param start,      indicates this is the first header pair of the block.
   */
  void passHeader(byte[] key, byte[] value, boolean start) {
    if (start) {
      assert headerAccumulator == null;
      assert headerCount == 0;
      headerAccumulator = new HashMap<>();
    }

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
    headerCount++;
  }

  /**
   * Retrieves accumulated headers and resets state.
   *
   * @return Map, a map of header names to one or more values.
   */
  Map<String, List<String>> retrieveHeaders() {
    final Map<String, List<String>> headers = headerAccumulator;
    headerAccumulator = null;
    headerCount = 0;
    return headers;
  }

  /**
   * May be called *prior* to retrieveHeaders to validate the quantity received.
   *
   * @param headerCount, the expected number of headers.
   * @return boolean, true if the expected number matches the accumulated count.
   */
  boolean validateCount(long headerCount) { return this.headerCount == headerCount; }
}
