

function testDynamic(iframme) {
  return new Promise((resolve, reject) => {
    iframe.contentWindow.setRenderTestHook(() => {
      try {
        const pre = iframe.contentWindow.document.getElementById('dynamic-content-pre');
        assertTrue(pre, "pre not found");
        assertTrue(pre.textContent.indexOf('server.memory_allocated: ') != -1, "stat not found");
        resolve();
      } catch (err) {
        reject(err);
      }
    });
  });
}

addTest('/stats?format=dynamic', "testDynamic", testDynamic);
