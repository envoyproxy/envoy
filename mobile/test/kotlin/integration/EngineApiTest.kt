package test.kotlin.integration

import com.google.protobuf.NullValue
import com.google.protobuf.Struct
import com.google.protobuf.Value
import io.envoyproxy.envoymobile.Element
import io.envoyproxy.envoymobile.EngineBuilder
import io.envoyproxy.envoymobile.LogLevel
import io.envoyproxy.envoymobile.engine.JniLibrary
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.assertj.core.api.Assertions.assertThat
import org.junit.Test

class EngineApiTest {
  init {
    JniLibrary.loadTestLibrary()
  }

  @Test
  fun `verify engine APIs`() {
    val countDownLatch = CountDownLatch(1)
    val engine =
      EngineBuilder()
        .addLogLevel(LogLevel.INFO)
        .setNodeId("node-id")
        .setNodeLocality("region", "zone", "subzone")
        .setNodeMetadata(
          Struct.newBuilder()
            .putFields("string_value", Value.newBuilder().setStringValue("string").build())
            .putFields("number_value", Value.newBuilder().setNumberValue(123.0).build())
            .putFields("bool_value", Value.newBuilder().setBoolValue(true).build())
            .putFields("null_value", Value.newBuilder().setNullValue(NullValue.NULL_VALUE).build())
            .putFields(
              "struct_value",
              Value.newBuilder()
                .setStructValue(
                  Struct.newBuilder()
                    .putFields(
                      "nested_value",
                      Value.newBuilder().setStringValue("nested_string").build()
                    )
                    .build()
                )
                .build()
            )
            .build()
        )
        .setOnEngineRunning { countDownLatch.countDown() }
        .build()

    assertThat(countDownLatch.await(30, TimeUnit.SECONDS)).isTrue()

    engine.pulseClient().counter(Element("foo"), Element("bar")).increment(1)

    assertThat(engine.dumpStats()).contains("pulse.foo.bar: 1")

    engine.terminate()
  }
}
