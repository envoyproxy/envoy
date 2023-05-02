package test.kotlin.integration

import java.io.IOException
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.SocketTimeoutException
import java.util.concurrent.CountDownLatch
import java.util.concurrent.atomic.AtomicReference

class TestStatsdServer {
  private val shutdownLatch: CountDownLatch = CountDownLatch(1)
  private val awaitNextStat: AtomicReference<CountDownLatch> = AtomicReference(CountDownLatch(0))
  private val matchCriteria: AtomicReference<(String) -> Boolean> = AtomicReference()

  private var thread: Thread? = null

  @Throws(IOException::class)
  fun runAsync(port: Int) {
    val socket = DatagramSocket(port)
    socket.setSoTimeout(1000) // 1 second

    thread = Thread(
      fun() {
        val buffer = ByteArray(256)
        while (shutdownLatch.getCount() != 0L) {
          val packet = DatagramPacket(buffer, buffer.size)
          try {
            socket.receive(packet)
          } catch (e: SocketTimeoutException) {
            // continue to next loop
            continue
          } catch (e: Exception) {
            // TODO(snowp): Bubble up this error somehow.
            return
          }

          // TODO(snowp): Parse (or use a parser) so we can extract out individual metric names
          // better.
          val received = String(packet.getData(), packet.getOffset(), packet.getLength())
          val maybeMatch = matchCriteria.get()
          if (maybeMatch != null && maybeMatch.invoke(received)) {
            matchCriteria.set(null)
            awaitNextStat.get().countDown()
          }
        }
      }
    )
    thread!!.start()
  }

  // Creates and returns a CountDownLatch for the given predicate's condition being met.
  // Callers of this method must call await() on the returned CountDownLatch to await the
  // predicate condition being met.
  fun statMatchingLatch(predicate: (String) -> Boolean): CountDownLatch {
    val latch = CountDownLatch(1)
    awaitNextStat.set(latch)
    matchCriteria.set(predicate)
    return latch
  }

  @Throws(InterruptedException::class)
  fun shutdown() {
    shutdownLatch.countDown()
    thread?.join()
  }
}
