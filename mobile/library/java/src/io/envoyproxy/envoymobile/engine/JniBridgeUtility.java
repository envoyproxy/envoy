package io.envoyproxy.envoymobile.engine;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * Class to assist with passing types from the JVM to native code. Currently supports
 * HTTP headers.
 */
final class JniBridgeUtility {

  private JniBridgeUtility() {}

  public static byte[][] toJniHeaders(Map<String, List<String>> headers) {
    // Create array with some room for potential headers that have more than one
    // value.
    final List<byte[]> convertedHeaders = new ArrayList<byte[]>(2 * headers.size());
    for (Map.Entry<String, List<String>> entry : headers.entrySet()) {
      for (String value : entry.getValue()) {
        convertedHeaders.add(entry.getKey().getBytes(StandardCharsets.UTF_8));
        convertedHeaders.add(value.getBytes(StandardCharsets.UTF_8));
      }
    }
    return convertedHeaders.toArray(new byte[0][0]);
  }
}
