package io.envoyproxy.envoymobile.jni;

import static org.assertj.core.api.Assertions.assertThat;

import com.google.protobuf.Struct;
import com.google.protobuf.Value;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class JniUtilityTest {
  public JniUtilityTest() { System.loadLibrary("envoy_jni_utility_test"); }

  //================================================================================
  // Native methods for testing.
  //================================================================================
  public static native byte[] protoJavaByteArrayConversion(byte[] source);

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
}
