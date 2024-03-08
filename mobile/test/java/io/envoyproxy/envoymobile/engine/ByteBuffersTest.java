package io.envoyproxy.envoymobile.engine;

import static com.google.common.truth.Truth.assertThat;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.nio.ByteBuffer;

@RunWith(AndroidJUnit4.class)
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
