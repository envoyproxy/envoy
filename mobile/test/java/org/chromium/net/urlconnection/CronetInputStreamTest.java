package org.chromium.net.urlconnection;

import static com.google.common.truth.Truth.assertThat;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.SmallTest;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.concurrent.Callable;
import org.chromium.net.testing.CronetTestRule;
import org.chromium.net.testing.Feature;
import org.chromium.net.testing.StrictModeContext;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentMatchers;
import org.mockito.Mockito;
import org.mockito.invocation.InvocationOnMock;
import org.mockito.stubbing.Answer;

/**
 * Test for {@link CronetInputStream}.
 */
@RunWith(AndroidJUnit4.class)
public class CronetInputStreamTest {
  @Rule public final CronetTestRule mTestRule = new CronetTestRule();

  private CronetHttpURLConnection mMockConnection;

  @Before
  public void setUp() throws Exception {
    // Disable StrictMode constraints for mock initialization.
    try (StrictModeContext ignored = StrictModeContext.allowAllVmPolicies()) {
      mMockConnection = Mockito.mock(CronetHttpURLConnection.class);
    }
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testAvailable_closed_withoutException() throws Exception {
    runTestCase(underTest -> {
      underTest.setResponseDataCompleted(null);

      assertThat(underTest.available()).isEqualTo(0);
    });
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testAvailable_closed_withException() throws Exception {
    runTestCase(underTest -> {
      IOException expected = new IOException();
      underTest.setResponseDataCompleted(expected);

      IOException actual = assertThrowsIoException(() -> underTest.available());

      assertThat(actual).isSameInstanceAs(expected);
    });
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testAvailable_noReads() throws Exception {
    runTestCase(underTest -> { assertThat(underTest.available()).isEqualTo(0); });
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testAvailable_everythingRead() throws Exception {
    runTestCase(underTest -> {
      int bytesInBuffer = 10;

      Mockito.doAnswer(addZerosToBuffer(bytesInBuffer))
          .when(mMockConnection)
          .getMoreData(ArgumentMatchers.any());

      for (int i = 0; i < bytesInBuffer; i++) {
        underTest.read();
      }

      assertThat(underTest.available()).isEqualTo(0);
    });
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testAvailable_partiallyRead() throws Exception {
    runTestCase(underTest -> {
      int bytesInBuffer = 10;
      int consumed = 3;

      Mockito.doAnswer(addZerosToBuffer(bytesInBuffer))
          .when(mMockConnection)
          .getMoreData(ArgumentMatchers.any());

      for (int i = 0; i < consumed; i++) {
        underTest.read();
      }

      assertThat(underTest.available()).isEqualTo(bytesInBuffer - consumed);
    });
  }

  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testRead_afterDataCompleted() throws Exception {
    runTestCase(underTest -> {
      int bytesInBuffer = 10;
      int consumed = 3;

      Mockito.doAnswer(addZerosToBuffer(bytesInBuffer))
          .when(mMockConnection)
          .getMoreData(ArgumentMatchers.any());

      for (int i = 0; i < consumed; i++) {
        underTest.read();
      }

      IOException expected = new IOException();
      underTest.setResponseDataCompleted(expected);

      IOException actual = assertThrowsIoException(() -> underTest.read());

      assertThat(actual).isSameInstanceAs(expected);
    });
  }

  private void runTestCase(CronetInputStreamTestCase testCase) throws Exception {
    try (CronetInputStream underTest = new CronetInputStream(mMockConnection)) {
      testCase.runTestCase(underTest);
    }
  }

  private static Answer<Void> addZerosToBuffer(int count) {
    return new Answer<Void>() {
      @Override
      public Void answer(InvocationOnMock invocation) {
        ByteBuffer arg = (ByteBuffer)invocation.getArguments()[0];
        for (int i = 0; i < count; i++) {
          arg.put((byte)0);
        }
        return null;
      }
    };
  }

  private static IOException assertThrowsIoException(Callable<?> callable) throws Exception {
    try {
      callable.call();
    } catch (IOException e) {
      return e;
    } catch (Exception e) {
      throw e;
    }
    throw new AssertionError("No exception was thrown!");
  }

  private static interface CronetInputStreamTestCase {
    void runTestCase(CronetInputStream underTest) throws Exception;
  }
}
