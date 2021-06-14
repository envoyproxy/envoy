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
  private var latestStats: AtomicReference<String> = AtomicReference()

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
          latestStats.set(received)
          awaitNextStat.get().countDown()
        }
      }
    )
    thread!!.start()
  }

  fun mostRecentlyReceivedStat(): String {
    return latestStats.get()
  }

  fun awaitNextStat() {
    val latch = CountDownLatch(1)
    awaitNextStat.set(latch)
    latch.await()
  }

  @Throws(InterruptedException::class)
  fun shutdown() {
    shutdownLatch.countDown()
    thread?.join()
  }
}
