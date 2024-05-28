package io.envoyproxy.envoymobile.jni;

import static com.google.common.truth.Truth.assertThat;

import com.google.protobuf.Struct;
import com.google.protobuf.Value;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import io.envoyproxy.envoymobile.engine.types.EnvoyFinalStreamIntel;
import io.envoyproxy.envoymobile.engine.types.EnvoyStreamIntel;

@RunWith(RobolectricTestRunner.class)
public class JniUtilityTest {
  public JniUtilityTest() { System.loadLibrary("envoy_jni_utility_test"); }

  //================================================================================
  // Native methods for testing.
  //================================================================================
  public static native byte[] protoJavaByteArrayConversion(byte[] source);
  public static native String javaCppStringConversion(String string);
  public static native Map<String, String> javaCppMapConversion(Map<String, String> map);
  public static native Map<String, List<String>>
  javaCppHeadersConversion(Map<String, List<String>> headers);
  public static native boolean isJavaDirectByteBuffer(ByteBuffer byteBuffer);
  public static native ByteBuffer javaCppDirectByteBufferConversion(ByteBuffer byteBuffer,
                                                                    long length);
  public static native ByteBuffer javaCppNonDirectByteBufferConversion(ByteBuffer byteBuffer,
                                                                       long length);
  public static native String getJavaExceptionMessage(Throwable throwable);
  public static native EnvoyStreamIntel javaCppStreamIntelConversion(EnvoyStreamIntel streamIntel);
  public static native EnvoyFinalStreamIntel
  javaCppFinalStreamIntelConversion(EnvoyFinalStreamIntel finalStreamIntel);

  @Test
  public void testProtoJavaByteArrayConversion() throws Exception {
    Struct source =
        Struct.newBuilder()
            .putFields("string_key", Value.newBuilder().setStringValue("string_value").build())
            .putFields("num_key", Value.newBuilder().setNumberValue(123).build())
            .putFields("bool_key", Value.newBuilder().setBoolValue(true).build())
            .build();
    Struct dest = Struct.parseFrom(protoJavaByteArrayConversion(source.toByteArray()));
    assertThat(source).isEqualTo(dest);
  }

  @Test
  public void testJavaCppStringConversion() {
    String input = "Hello World";
    assertThat(javaCppStringConversion(input)).isEqualTo(input);
  }

  @Test
  public void testJavaCppMapConversion() {
    Map<String, String> map = new HashMap<>();
    map.put("key1", "value1");
    map.put("key2", "value2");
    map.put("key3", "value3");
    assertThat(javaCppMapConversion(map)).isEqualTo(map);
  }

  @Test
  public void testJavaCppHeadersConversion() {
    Map<String, List<String>> headers = new HashMap<>();
    headers.put("lowercase_key_1", Arrays.asList("value1", "value2"));
    headers.put("UPPERCASE_KEY_2", Arrays.asList("value1"));
    headers.put("Mixed_Case_Key_3", Arrays.asList("value1"));
    assertThat(javaCppHeadersConversion(headers)).isEqualTo(headers);
  }

  @Test
  public void testIsJavaDirectByteBuffer() {
    assertThat(isJavaDirectByteBuffer(ByteBuffer.allocate(3))).isFalse();
    assertThat(isJavaDirectByteBuffer(ByteBuffer.allocateDirect(3))).isTrue();
  }

  @Test
  public void testJavaCppDirectByteBufferFullLengthConversion() {
    ByteBuffer inByteBuffer = ByteBuffer.allocateDirect(5);
    inByteBuffer.put((byte)'h');
    inByteBuffer.put((byte)'e');
    inByteBuffer.put((byte)'l');
    inByteBuffer.put((byte)'l');
    inByteBuffer.put((byte)'o');
    inByteBuffer.flip();

    ByteBuffer outByteBuffer =
        javaCppDirectByteBufferConversion(inByteBuffer, inByteBuffer.capacity());
    assertThat(outByteBuffer.isDirect()).isTrue();
    assertThat(outByteBuffer).isEqualTo(inByteBuffer);
    assertThat(outByteBuffer.capacity()).isEqualTo(5);
    byte[] outBytes = new byte[5];
    outByteBuffer.get(outBytes);
    assertThat(outBytes).isEqualTo(new byte[] {'h', 'e', 'l', 'l', 'o'});
  }

  @Test
  public void testJavaCppDirectByteBufferNotFullLengthConversion() {
    ByteBuffer inByteBuffer = ByteBuffer.allocateDirect(5);
    inByteBuffer.put((byte)'h');
    inByteBuffer.put((byte)'e');
    inByteBuffer.put((byte)'l');
    inByteBuffer.put((byte)'l');
    inByteBuffer.put((byte)'o');
    inByteBuffer.flip();

    ByteBuffer outByteBuffer = javaCppDirectByteBufferConversion(inByteBuffer, 3);
    assertThat(outByteBuffer.isDirect()).isTrue();
    assertThat(outByteBuffer.capacity()).isEqualTo(3);
    byte[] outBytes = new byte[3];
    outByteBuffer.get(outBytes);
    assertThat(outBytes).isEqualTo(new byte[] {'h', 'e', 'l'});
  }

  @Test
  public void testJavaCppNonDirectByteBufferFullLengthConversion() {
    ByteBuffer inByteBuffer = ByteBuffer.allocate(5);
    inByteBuffer.put((byte)'h');
    inByteBuffer.put((byte)'e');
    inByteBuffer.put((byte)'l');
    inByteBuffer.put((byte)'l');
    inByteBuffer.put((byte)'o');
    inByteBuffer.flip();

    ByteBuffer outByteBuffer =
        javaCppNonDirectByteBufferConversion(inByteBuffer, inByteBuffer.capacity());
    assertThat(outByteBuffer.isDirect()).isFalse();
    assertThat(outByteBuffer).isEqualTo(inByteBuffer);
    assertThat(outByteBuffer.array()).isEqualTo(new byte[] {'h', 'e', 'l', 'l', 'o'});
  }

  @Test
  public void testJavaCppNonDirectByteBufferNotFullLengthConversion() {
    ByteBuffer inByteBuffer = ByteBuffer.allocate(5);
    inByteBuffer.put((byte)'h');
    inByteBuffer.put((byte)'e');
    inByteBuffer.put((byte)'l');
    inByteBuffer.put((byte)'l');
    inByteBuffer.put((byte)'o');
    inByteBuffer.flip();

    ByteBuffer outByteBuffer = javaCppNonDirectByteBufferConversion(inByteBuffer, 3);
    assertThat(outByteBuffer.isDirect()).isFalse();
    assertThat(outByteBuffer.array()).isEqualTo(new byte[] {'h', 'e', 'l'});
  }

  @Test
  public void testGetJavaExceptionMessage() {
    assertThat(getJavaExceptionMessage(new RuntimeException("Test exception")))
        .isEqualTo("Test exception");
  }

  @Test
  public void testJavaCppStreamIntelConversion() {
    EnvoyStreamIntel streamIntel = new EnvoyStreamIntel(
        /* streamId= */ 1,
        /* connectionId= */ 2,
        /* attemptCount= */ 3,
        /* consumedBytesFromResponse= */ 4);
    assertThat(javaCppStreamIntelConversion(streamIntel)).isEqualTo(streamIntel);
  }

  @Test
  public void testJavaCppFinalStreamIntelConversion() {
    EnvoyFinalStreamIntel finalStreamIntel = new EnvoyFinalStreamIntel(
        /* streamStartMs= */ 1,
        /* dnsStartMs= */ 2,
        /* dnsEndMs= */ 3,
        /* connectStartMs= */ 4,
        /* connectEndMs= */ 5,
        /* sslStartMs= */ 6,
        /* sslEndMs= */ 7,
        /* sendingStartMs= */ 8,
        /* sendingEndMs= */ 9,
        /* responseStartMs= */ 10,
        /* streamEndMs= */ 11,
        /* socketReused= */ true,
        /* sentByteCount= */ 13,
        /* receivedByteCount= */ 14,
        /* responseFlags= */ 15,
        /* upstreamProtocol= */ 16);
    assertThat(javaCppFinalStreamIntelConversion(finalStreamIntel)).isEqualTo(finalStreamIntel);
  }
}
