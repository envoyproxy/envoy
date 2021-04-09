package io.envoyproxy.envoymobile.engine;

import android.app.Application;
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;
import io.envoyproxy.envoymobile.engine.types.EnvoyOnEngineRunning;
import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor;

/* Android-specific implementation of the `EnvoyEngine` interface. */
public class AndroidEngineImpl implements EnvoyEngine {
  private final Application application;
  private final EnvoyEngine envoyEngine;

  public AndroidEngineImpl(Application application) {
    this.application = application;
    this.envoyEngine = new EnvoyEngineImpl();
    AndroidJniLibrary.load(application.getBaseContext());
    AndroidNetworkMonitor.load(application.getBaseContext());
  }

  @Override
  public EnvoyHTTPStream startStream(EnvoyHTTPCallbacks callbacks) {
    return envoyEngine.startStream(callbacks);
  }

  @Override
  public void terminate() {
    envoyEngine.terminate();
  }

  @Override
  public int runWithConfig(String configurationYAML, String logLevel,
                           EnvoyOnEngineRunning onEngineRunning) {
    // re-enable lifecycle-based stat flushing when https://github.com/lyft/envoy-mobile/issues/748
    // gets fixed. AndroidAppLifecycleMonitor monitor = new AndroidAppLifecycleMonitor();
    // application.registerActivityLifecycleCallbacks(monitor);
    return envoyEngine.runWithConfig(configurationYAML, logLevel, onEngineRunning);
  }

  @Override
  public int runWithConfig(EnvoyConfiguration envoyConfiguration, String logLevel,
                           EnvoyOnEngineRunning onEngineRunning) {
    // re-enable lifecycle-based stat flushing when https://github.com/lyft/envoy-mobile/issues/748
    // gets fixed. AndroidAppLifecycleMonitor monitor = new AndroidAppLifecycleMonitor();
    // application.registerActivityLifecycleCallbacks(monitor);
    return envoyEngine.runWithConfig(envoyConfiguration, logLevel, onEngineRunning);
  }

  @Override
  public int recordCounterInc(String elements, int count) {
    return envoyEngine.recordCounterInc(elements, count);
  }

  @Override
  public int recordGaugeSet(String elements, int value) {
    return envoyEngine.recordGaugeSet(elements, value);
  }

  @Override
  public int recordGaugeAdd(String elements, int amount) {
    return envoyEngine.recordGaugeAdd(elements, amount);
  }

  @Override
  public int recordGaugeSub(String elements, int amount) {
    return envoyEngine.recordGaugeSub(elements, amount);
  }

  @Override
  public int recordHistogramDuration(String elements, int durationMs) {
    return envoyEngine.recordHistogramDuration(elements, durationMs);
  }

  @Override
  public int recordHistogramValue(String elements, int value) {
    return envoyEngine.recordHistogramValue(elements, value);
  }

  @Override
  public int registerStringAccessor(String accessorName, EnvoyStringAccessor accessor) {
    return envoyEngine.registerStringAccessor(accessorName, accessor);
  }
}
