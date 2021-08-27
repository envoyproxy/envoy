package io.envoyproxy.envoymobile.engine;

import android.content.Context;
import io.envoyproxy.envoymobile.engine.types.EnvoyEventTracker;
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;
import io.envoyproxy.envoymobile.engine.types.EnvoyLogger;
import io.envoyproxy.envoymobile.engine.types.EnvoyOnEngineRunning;
import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor;

import java.util.Map;

/* Android-specific implementation of the `EnvoyEngine` interface. */
public class AndroidEngineImpl implements EnvoyEngine {
  private final EnvoyEngine envoyEngine;

  /**
   * @param runningCallback Called when the engine finishes its async startup and begins running.
   */
  public AndroidEngineImpl(Context context, EnvoyOnEngineRunning runningCallback,
                           EnvoyLogger logger, EnvoyEventTracker eventTracker) {
    this.envoyEngine = new EnvoyEngineImpl(runningCallback, logger, eventTracker);
    AndroidJniLibrary.load(context);
    AndroidNetworkMonitor.load(context, envoyEngine);
  }

  @Override
  public EnvoyHTTPStream startStream(EnvoyHTTPCallbacks callbacks, boolean explicitFlowControl) {
    return envoyEngine.startStream(callbacks, explicitFlowControl);
  }

  public int runWithTemplate(String configurationYAML, EnvoyConfiguration envoyConfiguration,
                             String logLevel) {
    return envoyEngine.runWithTemplate(configurationYAML, envoyConfiguration, logLevel);
  }

  @Override
  public int runWithConfig(EnvoyConfiguration envoyConfiguration, String logLevel) {
    return envoyEngine.runWithConfig(envoyConfiguration, logLevel);
  }

  @Override
  public void terminate() {
    envoyEngine.terminate();
  }

  @Override
  public void flushStats() {
    envoyEngine.flushStats();
  }

  @Override
  public String dumpStats() {
    return envoyEngine.dumpStats();
  }

  @Override
  public int recordCounterInc(String elements, Map<String, String> tags, int count) {
    return envoyEngine.recordCounterInc(elements, tags, count);
  }

  @Override
  public int recordGaugeSet(String elements, Map<String, String> tags, int value) {
    return envoyEngine.recordGaugeSet(elements, tags, value);
  }

  @Override
  public int recordGaugeAdd(String elements, Map<String, String> tags, int amount) {
    return envoyEngine.recordGaugeAdd(elements, tags, amount);
  }

  @Override
  public int recordGaugeSub(String elements, Map<String, String> tags, int amount) {
    return envoyEngine.recordGaugeSub(elements, tags, amount);
  }

  @Override
  public int recordHistogramDuration(String elements, Map<String, String> tags, int durationMs) {
    return envoyEngine.recordHistogramDuration(elements, tags, durationMs);
  }

  @Override
  public int recordHistogramValue(String elements, Map<String, String> tags, int value) {
    return envoyEngine.recordHistogramValue(elements, tags, value);
  }

  @Override
  public int registerStringAccessor(String accessorName, EnvoyStringAccessor accessor) {
    return envoyEngine.registerStringAccessor(accessorName, accessor);
  }

  @Override
  public void drainConnections() {
    envoyEngine.drainConnections();
  }
}
