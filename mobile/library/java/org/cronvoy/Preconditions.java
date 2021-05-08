package org.cronvoy;

import java.nio.ByteBuffer;

/** Utility class to check preconditions. */
final class Preconditions {

  static void checkDirect(ByteBuffer buffer) {
    if (!buffer.isDirect()) {
      throw new IllegalArgumentException("byteBuffer must be a direct ByteBuffer.");
    }
  }

  static void checkHasRemaining(ByteBuffer buffer) {
    if (!buffer.hasRemaining()) {
      throw new IllegalArgumentException("ByteBuffer is already full.");
    }
  }

  private Preconditions() {}
}
