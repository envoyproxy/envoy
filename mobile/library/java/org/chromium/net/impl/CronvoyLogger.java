package org.chromium.net.impl;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import io.envoyproxy.envoymobile.engine.types.EnvoyLogger;

final class CronvoyLogger implements EnvoyLogger {
  private FileWriter mWriter;

  public CronvoyLogger() { }

  public void stopLogging() {
      try {
    mWriter.close();
    mWriter = null;
      } catch (IOException e) {
      }
  }

  @Override
  public void log(String str) {
    if (mWriter != null) {
      try {
        mWriter.write(str);
      } catch (IOException e) {
      }
    }
  }

  public void setNetLogToFile(String fileName) {
      try {
    File file = new File(fileName);
    mWriter = new FileWriter(file, true);
      } catch (IOException e) {
      }
  }

  public void setNetLogToDisk(String dirPath, int maxSize) {
    // FIXME implement
  }
}
