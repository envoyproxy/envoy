package io.envoyproxy.envoymobile.engine;

import static com.google.common.truth.Truth.assertThat;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

import java.nio.ByteBuffer;

@RunWith(RobolectricTestRunner.class)
public class ByteBuffersTest {
  @Test
  public void testCopy() {
    ByteBuffer source = ByteBuffer.allocateDirect(3);
    source.put((byte)1);
    source.put((byte)2);
    source.put((byte)3);
    source.flip();

    ByteBuffer dest = ByteBuffers.copy(source);
    source.flip();
    assertThat(dest).isEqualTo(source);
  }
}
