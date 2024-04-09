package io.envoyproxy.envoymobile.jni;

import static com.google.common.truth.Truth.assertThat;

import com.google.protobuf.Struct;
import com.google.protobuf.Value;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

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
}
