package test.java.integration;

import android.content.Context;
import androidx.test.core.app.ApplicationProvider;
import io.envoyproxy.envoymobile.AndroidEngineBuilder;
import io.envoyproxy.envoymobile.Engine;
import io.envoyproxy.envoymobile.LogLevel;
import io.envoyproxy.envoymobile.engine.JniLibrary;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

import static com.google.common.truth.Truth.assertThat;

// NOLINT(namespace-envoy)

@RunWith(RobolectricTestRunner.class)
public class AndroidEngineStartUpTest {
  static { JniLibrary.loadTestLibrary(); }

  private final Context appContext = ApplicationProvider.getApplicationContext();

  @Test
  public void ensure_engine_starts_and_terminates() throws InterruptedException {
    Engine engine = new AndroidEngineBuilder(appContext)
                        .setLogLevel(LogLevel.DEBUG)
                        .setLogger((level, message) -> {
                          System.out.print(message);
                          return null;
                        })
                        .build();
    Thread.sleep(1000);
    engine.terminate();
    assertThat(true).isTrue();
  }
}
