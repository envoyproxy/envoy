package org.chromium.net.impl;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import io.envoyproxy.envoymobile.engine.types.EnvoyLogger;

final class CronvoyLogger implements EnvoyLogger {
  private FileWriter mWriter;

  public CronvoyLogger() {}

  public void stopLogging() {
    try {
      if (mWriter != null) {
        mWriter.close();
        mWriter = null;
      }
    } catch (IOException e) {
      System.err.println("Encountered " + e.toString() + " when trying to stopLogging");
    }
  }

  @Override
  public void log(String str) {
    if (mWriter != null) {
      try {
        mWriter.write(str);
      } catch (IOException e) {
        System.err.println("Encountered " + e.toString() + " when trying to log");
      }
    }
  }

  public void setNetLogToFile(String fileName) {
    try {
      File file = new File(fileName);
      mWriter = new FileWriter(file, true);
    } catch (IOException e) {
      System.err.println("Encountered " + e.toString() + " when trying to setNetLogToFile");
    }
  }

  public void setNetLogToDisk(String dirPath, int maxSize) {
    try {
      // TODO(alyssawilk) do we need to create the directory?
      File file = new File(dirPath);
      mWriter = new FileWriter(file, true);
    } catch (IOException e) {
      System.err.println("Encountered " + e.toString() + " when trying to setNetLogToDisk");
    }
    // TODO(alyssawilk) implement size checks.
  }
}
