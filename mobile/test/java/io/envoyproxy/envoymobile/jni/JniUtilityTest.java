package io.envoyproxy.envoymobile.jni;

import static com.google.common.truth.Truth.assertThat;

import com.google.protobuf.Struct;
import com.google.protobuf.Value;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

import java.util.HashMap;
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
  public void testCppStringMapToJavaStringMap() {
    Map<String, String> map = new HashMap<>();
    map.put("key1", "value1");
    map.put("key2", "value2");
    map.put("key3", "value3");
    assertThat(javaCppMapConversion(map)).isEqualTo(map);
  }
}
