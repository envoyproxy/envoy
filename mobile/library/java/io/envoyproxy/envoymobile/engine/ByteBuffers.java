package io.envoyproxy.envoymobile.engine;

import java.nio.ByteBuffer;

public class ByteBuffers {
  /**
   * Copies the specified `ByteBuffer` into a new `ByteBuffer`. The `ByteBuffer` created will
   * be backed by `byte[]`.
   */
  public static ByteBuffer copy(ByteBuffer byteBuffer) {
    byte[] bytes = new byte[byteBuffer.capacity()];
    byteBuffer.get(bytes);
    return ByteBuffer.wrap(bytes);
  }

  private ByteBuffers() {}
}
