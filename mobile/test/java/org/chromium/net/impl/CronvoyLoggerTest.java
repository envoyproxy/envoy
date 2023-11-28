package org.chromium.net.impl;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.junit.Assert.assertEquals;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.SmallTest;
import org.junit.rules.ExpectedException;
import org.chromium.net.testing.Feature;
import org.junit.runner.RunWith;
import org.junit.Rule;
import org.junit.Test;
import org.chromium.net.impl.CronvoyLogger;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.Paths;

/**
 * Tests that ConvoyLogger works as expected.
 */
@RunWith(AndroidJUnit4.class)
public class CronvoyLoggerTest {

  @Rule public final ExpectedException thrown = ExpectedException.none();

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void logWithLoggerUnconfigured() throws Exception {
    CronvoyLogger logger = new CronvoyLogger();
    // Should be a no-op.
    logger.log("hello");
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testBasicLogToFile() throws Exception {
    File file = File.createTempFile("some-prefix", "file-ext");
    file.deleteOnExit();
    String filename = file.getAbsolutePath() + "foo"; // Pick a path that doesn't exist.
    CronvoyLogger logger = new CronvoyLogger();
    logger.setNetLogToFile(filename);
    logger.log("hello");
    logger.stopLogging();
    char[] buffer = new char[5000];
    byte[] bytes = Files.readAllBytes(Paths.get(filename));
    String fileContent = new String(bytes);
    assertEquals(fileContent, "hello");
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testBasicLogToDisk() throws Exception {
    File file = File.createTempFile("some-prefix", "basic-ext");
    file.deleteOnExit();
    String filename = file.getAbsolutePath() + "bar/foo"; // Pick a directory that doesn't exist.
    CronvoyLogger logger = new CronvoyLogger();
    logger.setNetLogToDisk(filename, 5000);
    logger.log("hello");
    logger.stopLogging();
    char[] buffer = new char[5000];
    byte[] bytes = Files.readAllBytes(Paths.get(filename + "/netlog.json"));
    String fileContent = new String(bytes);
    assertEquals(fileContent, "hello");
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testLogToDiskWithLimits() throws Exception {
    File file = File.createTempFile("some-prefix", "limits-ext");
    file.deleteOnExit();
    String filename = file.getAbsolutePath() + "bar";
    CronvoyLogger logger = new CronvoyLogger();
    logger.setNetLogToDisk(filename, 5);
    logger.log("hello!");
    logger.log("goodbye");
    logger.stopLogging();
    char[] buffer = new char[5000];
    byte[] bytes = Files.readAllBytes(Paths.get(filename + "/netlog.json"));
    String fileContent = new String(bytes);
    assertEquals("goodbye", fileContent);
  }
}
