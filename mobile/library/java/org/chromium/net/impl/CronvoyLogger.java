package org.chromium.net.impl;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import io.envoyproxy.envoymobile.engine.types.EnvoyLogger;

/*
 * CronvoyLogger
 *
 * This class bridges Envoy and Cronet logging by providing an EnvoyLogger with Envoy's log API
 * which also has Cronet-style setNetLogToFile and setNetLogToDisk functions.
 *
 * The Envoy engine is supplied the logger on start-up but will only log at the configured log
 * level (Cronvoy defaults logging off). When logging is desired, the CronvoyUrlRequestContext
 * sets the Envoy log level to TRACE or DEBUG (based on if logAll is set), and passes the desired
 * log info to the CronvoyLogger. The CronvoyLogger will then in append pass Envoy log messages to
 * the desired file until CronvoyUrlRequestContext.stopNetLog disables Envoy logging.
 *
 */
final class CronvoyLogger implements EnvoyLogger {
  private FileWriter mWriter;
  static final String LOG_TAG = CronvoyUrlRequestContext.class.getSimpleName();

  public CronvoyLogger() {}

  public void stopLogging() {
    try {
      if (mWriter != null) {
        mWriter.close();
        mWriter = null;
      }
    } catch (IOException e) {
      android.util.Log.e(LOG_TAG, "Failed to stop logging", e);
    }
  }

  @Override
  public void log(String str) {
    if (mWriter != null) {
      try {
        mWriter.write(str);
      } catch (IOException e) {
        android.util.Log.e(LOG_TAG, "Failed to log message", e);
      }
    }
  }

  public void setNetLogToFile(String fileName) {
    try {
      File file = new File(fileName);
      mWriter = new FileWriter(file, true);
    } catch (IOException e) {
      android.util.Log.e(LOG_TAG, "Failed to start logging", e);
    }
  }

  public void setNetLogToDisk(String dirPath, int maxSize) {
    try {
      // TODO(alyssawilk) do we need to create the directory?
      File file = new File(dirPath);
      mWriter = new FileWriter(file, true);
    } catch (IOException e) {
      android.util.Log.e(LOG_TAG, "Failed to start logging", e);
    }
    // TODO(alyssawilk) implement size checks.
  }
}
