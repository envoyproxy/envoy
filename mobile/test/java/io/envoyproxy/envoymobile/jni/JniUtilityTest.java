package io.envoyproxy.envoymobile.jni;

import static com.google.common.truth.Truth.assertThat;

import com.google.protobuf.Struct;
import com.google.protobuf.Value;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

import java.util.Map;

@RunWith(RobolectricTestRunner.class)
public class JniUtilityTest {
  public JniUtilityTest() { System.loadLibrary("envoy_jni_utility_test"); }

  //================================================================================
  // Native methods for testing.
  //================================================================================
  public static native byte[] protoJavaByteArrayConversion(byte[] source);
  public static native Map<String, String> cppMapToJavaMap();

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
  public void testCppStringMapToJavaStringMap() {
    Map<String, String> map = cppMapToJavaMap();
    assertThat(map).containsExactly("key1", "value1", "key2", "value2", "key3", "value3");
  }
}
