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

function dynamicContent(iframe) {
  const idoc = iframe.contentWindow.document;
  const pre = idoc.getElementById('dynamic-content-pre');
  assertTrue(pre, "pre not found");
  return pre.textContent;
}

async function testDefaults(iframme) {
  const idoc = iframe.contentWindow.document;
  await waitForRender(iframe);
  const content = dynamicContent(iframe);
  assertTrue(content.indexOf('server.memory_allocated: ') != -1, "stat not found");
  assertEq(50, count(content, "\n"));
}


async function testCustomMaxCount(iframme) {
  const idoc = iframe.contentWindow.document;
  const update_interval = idoc.getElementById('dynamic-update-interval');
  update_interval.value = 1;

  // Now set the update interval to 5 and re-count lines.
  const max_display_count = idoc.getElementById('dynamic-max-display-count');
  max_display_count.value = 5;
  await waitForRender(iframe);
  await waitForRender(iframe);
  const content = dynamicContent(iframe);
  assertEq(5, count(content, "\n"));
}

addTest('/stats?format=dynamic', "testDefaults", testDefaults);
addTest('/stats?format=dynamic', "testCustomMaxCount", testCustomMaxCount);
