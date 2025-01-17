package io.envoyproxy.envoymobile

import com.google.common.truth.Truth.assertThat
import io.envoyproxy.envoymobile.engine.EnvoyEngine
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentCaptor
import org.mockito.Captor
import org.mockito.Mockito.mock
import org.mockito.Mockito.verify
import org.mockito.MockitoAnnotations
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class PulseClientImplTest {
  private var envoyEngine: EnvoyEngine = mock(EnvoyEngine::class.java)

  @Captor private lateinit var elementsCaptor: ArgumentCaptor<String>

  @Captor private lateinit var tagsCaptor: ArgumentCaptor<MutableMap<String, String>>

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
    verify(envoyEngine)
      .recordCounterInc(elementsCaptor.capture(), tagsCaptor.capture(), countCaptor.capture())
    assertThat(elementsCaptor.value).isEqualTo("test.stat")
    assertThat(countCaptor.value).isEqualTo(1)
    assertThat(tagsCaptor.value.size).isEqualTo(0)
  }

  @Test
  fun `counter delegates to engine with tags and count`() {
    val pulseClient = PulseClientImpl(envoyEngine)
    val counter =
      pulseClient.counter(
        Element("test"),
        Element("stat"),
        tags = TagsBuilder().add("testKey1", "testValue1").add("testKey2", "testValue2").build()
      )
    counter.increment(5)
    val countCaptor = ArgumentCaptor.forClass(Int::class.java)
    verify(envoyEngine)
      .recordCounterInc(elementsCaptor.capture(), tagsCaptor.capture(), countCaptor.capture())
    assertThat(elementsCaptor.value).isEqualTo("test.stat")
    assertThat(countCaptor.value).isEqualTo(5)

    val tagCaptorValue = tagsCaptor.value
    assertThat(tagCaptorValue.get("testKey1")).isEqualTo("testValue1")
    assertThat(tagCaptorValue.get("testKey2")).isEqualTo("testValue2")
  }
}
