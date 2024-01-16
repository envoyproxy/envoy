package org.chromium.net.impl;

import java.nio.ByteBuffer;

/**
 * Utility class to check preconditions.
 */
public final class CronvoyPreconditions {

  public static void checkDirect(ByteBuffer buffer) {
    if (!buffer.isDirect()) {
      throw new IllegalArgumentException("byteBuffer must be a direct ByteBuffer.");
    }
  }

  public static void checkHasRemaining(ByteBuffer buffer) {
    if (!buffer.hasRemaining()) {
      throw new IllegalArgumentException("ByteBuffer is already full.");
    }
  }

  private CronvoyPreconditions() {}
}
