package org.chromium.net.urlconnection;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.SmallTest;
import org.chromium.net.testing.CronetTestRule;
import org.chromium.net.testing.Feature;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Test for CronetURLStreamHandlerFactory.
 */
@RunWith(AndroidJUnit4.class)
@SuppressWarnings("deprecation")
public class CronetURLStreamHandlerFactoryTest {
  @Rule public final CronetTestRule mTestRule = new CronetTestRule();

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testRequireConfig() throws Exception {
    mTestRule.startCronetTestFramework();
    try {
      new CronetURLStreamHandlerFactory(null);
      fail();
    } catch (NullPointerException e) {
      assertEquals("CronetEngine is null.", e.getMessage());
    }
  }
}
