function count(text, separator) {
  return text.split(separator).length - 1;
}

// Blocks the async flow until the fetch completes and the page renders.
async function waitForRender(iframe) {
  return new Promise((resolve, reject) => {
    iframe.contentWindow.setRenderTestHook(() => {
      resolve();
    });
  });
}

function activeContent(iframe) {
  const idoc = iframe.contentWindow.document;
  const pre = idoc.getElementById('active-content-pre');
  assertTrue(pre, "pre not found");
  return pre.textContent;
}

async function testDefaults(iframme) {
  const idoc = iframe.contentWindow.document;
  await waitForRender(iframe);
  const content = activeContent(iframe);
  assertTrue(content.indexOf('server.memory_allocated: ') != -1, "stat not found");
  assertEq(50, count(content, "\n"));
}


async function testCustomMaxCount(iframme) {
  const idoc = iframe.contentWindow.document;
  const update_interval = idoc.getElementById('active-update-interval');
  update_interval.value = 1;

  // Now set the update interval to 5 and re-count lines.
  const max_display_count = idoc.getElementById('active-max-display-count');
  max_display_count.value = 5;

  // Render for two iterations as the first render may race with the update
  // to display-count. To avoid flakes, we render a second time.
  await waitForRender(iframe);
  await waitForRender(iframe);
  const content = activeContent(iframe);
  assertEq(5, count(content, "\n"));
}

addTest('/stats?format=active', "testDefaults", testDefaults);
addTest('/stats?format=active', "testCustomMaxCount", testCustomMaxCount);
