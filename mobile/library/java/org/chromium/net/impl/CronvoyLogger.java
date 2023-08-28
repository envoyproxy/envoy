package org.chromium.net.impl;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
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
  private int mFilesize = 0;
  private String mFileName = null;
  static final String LOG_TAG = CronvoyUrlRequestContext.class.getSimpleName();

  public CronvoyLogger() {}

  public void stopLogging() { mFileName = null; }

  @Override
  public void log(String str) {
    if (mFileName != null) {
      try {
        Path path = Paths.get(mFileName);
        // For now, just delete the file if it gets overlarge.
        // If we need to we can copy the first half.
        if (Files.size(path) > mFilesize) {
          File file = new File(mFileName);
          file.delete();
          file.createNewFile();
        }

        Files.write(path, str.getBytes());
      } catch (IOException e) {
        android.util.Log.e(LOG_TAG, "Failed to log message", e);
      }
    }
  }

  public void setNetLogToFile(String fileName) {
    try {
      mFilesize = 0;
      mFileName = fileName;
      File file = new File(mFileName);
      file.createNewFile();
    } catch (IOException e) {
      android.util.Log.e(LOG_TAG, "Failed to start logging", e);
    }
  }

  public void setNetLogToDisk(String dirPath, int maxSize) {
    try {
      mFilesize = maxSize;
      mFileName = dirPath;
      File file = new File(mFileName);
      File directory = file.getParentFile();
      if (directory != null) {
        directory.mkdirs();
      }
      file.createNewFile();
    } catch (IOException e) {
      android.util.Log.e(LOG_TAG, "Failed to start logging", e);
    }
  }
}
