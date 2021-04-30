package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyEngine
import org.assertj.core.api.Assertions.assertThat
import org.junit.Before
import org.junit.Test
import org.mockito.ArgumentCaptor
import org.mockito.Captor
import org.mockito.Mockito.mock
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mockito.MockitoAnnotations

class PulseClientImplTest {
  private var envoyEngine: EnvoyEngine = mock(EnvoyEngine::class.java)

  @Captor
  private lateinit var elementsCaptor: ArgumentCaptor<String>

  @Captor
  private lateinit var tagsCaptor: ArgumentCaptor<MutableMap<String, String>>

  @Before
  fun setup() {
    MockitoAnnotations.initMocks(this)
  }

  @Test
  fun `counter delegates to engine`() {
    val pulseClient = PulseClientImpl(envoyEngine)
    val counter = pulseClient.counter(Element("test"), Element("stat"))
    counter.increment()
    val countCaptor = ArgumentCaptor.forClass(Int::class.java)
    verify(envoyEngine).recordCounterInc(
      elementsCaptor.capture(), tagsCaptor.capture(), countCaptor.capture()
    )
    assertThat(elementsCaptor.getValue()).isEqualTo("test.stat")
    assertThat(countCaptor.getValue()).isEqualTo(1)
    assertThat(tagsCaptor.getValue().size).isEqualTo(0)
  }

  @Test
  fun `counter delegates to engine with tags and count`() {
    val pulseClient = PulseClientImpl(envoyEngine)
    val counter = pulseClient.counter(
      Element("test"), Element("stat"),
      tags = TagsBuilder().add("testKey1", "testValue1").add("testKey2", "testValue2").build()
    )
    counter.increment(5)
    val countCaptor = ArgumentCaptor.forClass(Int::class.java)
    verify(envoyEngine).recordCounterInc(
      elementsCaptor.capture(), tagsCaptor.capture(), countCaptor.capture()
    )
    assertThat(elementsCaptor.getValue()).isEqualTo("test.stat")
    assertThat(countCaptor.getValue()).isEqualTo(5)

    val tagCaptorValue = tagsCaptor.getValue()
    assertThat(tagCaptorValue.get("testKey1")).isEqualTo("testValue1")
    assertThat(tagCaptorValue.get("testKey2")).isEqualTo("testValue2")
  }

  @Test
  fun `gauge delegates to engine with tags and value for set`() {
    val pulseClient = PulseClientImpl(envoyEngine)
    val gauge = pulseClient.gauge(
      Element("test"), Element("stat"),
      tags = TagsBuilder().add("testKey1", "testValue1").add("testKey2", "testValue2").build()
    )
    gauge.set(5)
    val valueCaptor = ArgumentCaptor.forClass(Int::class.java)
    verify(envoyEngine).recordGaugeSet(
      elementsCaptor.capture(), tagsCaptor.capture(), valueCaptor.capture()
    )
    assertThat(elementsCaptor.getValue()).isEqualTo("test.stat")
    assertThat(valueCaptor.getValue()).isEqualTo(5)

    val tagCaptorValue = tagsCaptor.getValue()
    assertThat(tagCaptorValue.get("testKey1")).isEqualTo("testValue1")
    assertThat(tagCaptorValue.get("testKey2")).isEqualTo("testValue2")
  }

  @Test
  fun `gauge delegates to engine with tags and amount for add`() {
    val pulseClient = PulseClientImpl(envoyEngine)
    val gauge = pulseClient.gauge(
      Element("test"), Element("stat"),
      tags = TagsBuilder().add("testKey1", "testValue1").add("testKey2", "testValue2").build()
    )
    gauge.add(5)
    val amountCaptor = ArgumentCaptor.forClass(Int::class.java)
    verify(envoyEngine).recordGaugeAdd(
      elementsCaptor.capture(), tagsCaptor.capture(), amountCaptor.capture()
    )
    assertThat(elementsCaptor.getValue()).isEqualTo("test.stat")
    assertThat(amountCaptor.getValue()).isEqualTo(5)

    val tagCaptorValue = tagsCaptor.getValue()
    assertThat(tagCaptorValue.get("testKey1")).isEqualTo("testValue1")
    assertThat(tagCaptorValue.get("testKey2")).isEqualTo("testValue2")
  }

  @Test
  fun `gauge delegates to engine with tags and amount for sub`() {
    val pulseClient = PulseClientImpl(envoyEngine)
    val gauge = pulseClient.gauge(
      Element("test"), Element("stat"),
      tags = TagsBuilder().add("testKey1", "testValue1").add("testKey2", "testValue2").build()
    )
    gauge.add(5)
    gauge.sub(5)
    val amountCaptor = ArgumentCaptor.forClass(Int::class.java)
    verify(envoyEngine).recordGaugeSub(
      elementsCaptor.capture(), tagsCaptor.capture(), amountCaptor.capture()
    )
    assertThat(elementsCaptor.getValue()).isEqualTo("test.stat")
    assertThat(amountCaptor.getValue()).isEqualTo(5)

    val tagCaptorValue = tagsCaptor.getValue()
    assertThat(tagCaptorValue.get("testKey1")).isEqualTo("testValue1")
    assertThat(tagCaptorValue.get("testKey2")).isEqualTo("testValue2")
  }

  @Test
  fun `timer delegates to engine with tags and duration for record`() {
    val pulseClient = PulseClientImpl(envoyEngine)
    val timer = pulseClient.timer(
      Element("test"), Element("stat"),
      tags = TagsBuilder().add("testKey1", "testValue1").add("testKey2", "testValue2").build()
    )

    timer.completeWithDuration(5)

    val durationCaptor = ArgumentCaptor.forClass(Int::class.java)
    verify(envoyEngine).recordHistogramDuration(
      elementsCaptor.capture(), tagsCaptor.capture(), durationCaptor.capture()
    )
    assertThat(elementsCaptor.getValue()).isEqualTo("test.stat")
    assertThat(durationCaptor.getValue()).isEqualTo(5)

    val tagCaptorValue = tagsCaptor.getValue()
    assertThat(tagCaptorValue.get("testKey1")).isEqualTo("testValue1")
    assertThat(tagCaptorValue.get("testKey2")).isEqualTo("testValue2")
  }

  @Test
  fun `distribution delegates to engine with tags and value for record`() {
    val pulseClient = PulseClientImpl(envoyEngine)
    val distribution = pulseClient.distribution(
      Element("test"), Element("stat"),
      tags = TagsBuilder().add("testKey1", "testValue1").add("testKey2", "testValue2").build()
    )

    distribution.recordValue(5)

    val valueCaptor = ArgumentCaptor.forClass(Int::class.java)

    verify(envoyEngine).recordHistogramValue(
      elementsCaptor.capture(), tagsCaptor.capture(), valueCaptor.capture()
    )
    assertThat(elementsCaptor.getValue()).isEqualTo("test.stat")
    assertThat(valueCaptor.getValue()).isEqualTo(5)

    val tagCaptorValue = tagsCaptor.getValue()
    assertThat(tagCaptorValue.get("testKey1")).isEqualTo("testValue1")
    assertThat(tagCaptorValue.get("testKey2")).isEqualTo("testValue2")
  }
}
