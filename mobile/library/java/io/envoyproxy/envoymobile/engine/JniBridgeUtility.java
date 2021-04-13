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
    // Perform no conversion on null headers.
    if (headers == null) {
      return null;
    }

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

  public static byte[][] toJniTags(Map<String, String> tags) {
    if (tags == null) {
      return null;
    }
    final List<byte[]> convertedTags = new ArrayList<byte[]>(2 * tags.size());
    for (Map.Entry<String, String> tag : tags.entrySet()) {
      convertedTags.add(tag.getKey().getBytes(StandardCharsets.UTF_8));
      convertedTags.add(tag.getValue().getBytes(StandardCharsets.UTF_8));
    }
    return convertedTags.toArray(new byte[0][0]);
  }
}
