/**
 * This file contains JavaScript functionality to periodically fetch JSON stats from
 * the Envoy server, and display the top 50 (by default) stats in order of how often
 * they've changed since the page was brought up. This can be useful to find potentially
 * problematic listeners or clusters or other high-cardinality subsystems in Envoy whose
 * activity can be quickly examined for potential problems. The more active a stat, the
 * more likely it is to reflect behavior of note.
 */

// Follow-ups:
//   * top-n algorithm to limit compute overhead with high cardinality stats with user control of N.
//   * alternate sorting criteria, reverse-sort controls, etc.
//   * render histograms
//   * detect when user is not looking at page and stop or slow down pinging the server
//   * hierarchical display
//   * json flavor to send hierarchical names to save serialization/deserialization costs
//   * pause auto-refresh for at least 5 seconds when editng fields.
//   * don't auto-refresh when there is error -- provide a button to re-retry.
//   * update URL history after editing params
//   * erase spurious 'stats' link at lop left
//   * consider removing histogram mode during active display, and overlay summary graphics
//   * rename bucket mode "none" to "summary"
//   * improve graphics
//   * log Y axis
//   * deal with large # of buckets (combine adjacent?).
//    --  Say: max 24 buckets and interpolate in C++
//   * integrate interval view.

/**
 * Maps a stat name to a record containing name, value, and a use-count. This
 * map is rebuilt every 5 seconds.
 */
const nameStatsMap = new Map();

/**
 * The first time this script loads, it will write PRE element at the end of body.
 * This is hooked to the `DOMContentLoaded` event.
 */
let activeStatsPreElement = null;

/**
 * A table into which we render histograms.
 */
let activeStatsHistogramsDiv = null;

/**
 * A small div for displaying status and error messages.
 */
let statusDiv = null;

/**
 * This magic string is derived from C++ in StatsHtmlRender::urlHandler to uniquely
 * name each parameter. In the stats page there is only one parameter, so it's
 * always param-1. The reason params are numbered is that on the home page, "/",
 * all the admin endpoints have uniquely numbered parameters.
 */
const paramIdPrefix = 'param-1-stats-';

let postRenderTestHook = null;

/**
 * To make testing easier, provide a hook for tests to set, to enable tests
 * to block on rendering.
 *
 * @param {!function()} hook
 */
function setRenderTestHook(hook) { // eslint-disable-line no-unused-vars
  if (postRenderTestHook != null) {
    throw new Exception('setRenderTestHook called with hook already pending');
  }
  postRenderTestHook = hook;
}

/**
 * Hook that's run on DOMContentLoaded to create the HTML elements (just one
 * PRE right now) and kick off the periodic JSON updates.
 */
function initHook() {
  statusDiv = document.createElement('div');
  statusDiv.className = 'error-status-line';
  activeStatsPreElement = document.createElement('pre');
  activeStatsPreElement.id = 'active-content-pre';
  const table = document.createElement('table');
  table.classList.add('histogram-body', 'histogram-column');
  activeStatsHistogramsDiv = document.createElement('div');
  //activeStatsHistogramsTable.id = 'active-content-histograms-table';
  document.body.appendChild(statusDiv);
  document.body.appendChild(activeStatsPreElement);
  document.body.appendChild(activeStatsHistogramsDiv);
  loadStats();
}

/**
 * Initiates an Ajax request for the stats JSON based on the stats parameters.
 */
async function loadStats() {
  const makeQueryParam = (name) => name + '=' + encodeURIComponent(
      document.getElementById(paramIdPrefix + name).value);
  const params = ['filter', 'type', 'histogram_buckets'];
  const url = '/stats?format=json&usedonly&' + params.map(makeQueryParam).join('&');
  try {
    const response = await fetch(url);
    const data = await response.json();
    renderStats(data);
  } catch (e) {
    statusDiv.textContent = 'Error fetching ' + url + ': ' + e;
  }

  // Update stats every 5 seconds by default.
  window.setTimeout(loadStats, 1000*loadSettingOrUseDefault('active-update-interval', 5));
}

/**
 * Function used to sort stat records. The highest priority is update frequency,
 * then value (higher values are likely more interesting), and finally alphabetic,
 * in forward order.
 *
 * @param {!Object} a
 * @param {!Object} b
 * @return {number}
 */
function compareStatRecords(a, b) {
  // Sort higher change-counts first.
  if (a.change_count != b.change_count) {
    return b.change_count - a.change_count;
  }

  // Secondarily put higher values first -- they are often more interesting.
  if (b.value != a.value) {
    return b.value - a.value;
  }

  // Fall back to forward alphabetic sort.
  if (a.name < b.name) {
    return -1;
  }
  if (a.name > b.name) {
    return 1;
  }
  return 0;
}

/**
 * The active display has additional settings for tweaking it -- this helper extracts numeric
 * values from text widgets
 *
 * @param {string} id
 * @param {number} defaultValue
 * @return {number}
 */
function loadSettingOrUseDefault(id, defaultValue) {
  const elt = document.getElementById(id);
  const value = parseInt(elt.value);
  if (Number.isNaN(value) || value <= 0) {
    console.log('Invalid ' + id + ': invalid positive number');
    return defaultValue;
  }
  return value;
}

/**
 * Rendering function which interprets the Json response from the server, updates
 * the most-frequently-used map, reverse-sorts by use-count, and serializes the
 * top ordered stats into the PRE element created in initHook.
 *
 * @param {!Object} data
 */
function renderStats(data) {
  sortedStats = [];
  for (stat of data.stats) {
    if (!stat.name) {
      const histograms = stat.histograms;
      if (histograms) {
        if (histograms.supported_quantiles) {
          renderHistogramSummary(histograms);
        } else if (histograms.supported_percentiles && histograms.details) {
          renderHistogramDetail(histograms.supported_percentiles, histograms.details);
        } else if (histograms.length && histograms[0].name) {
          renderHistogramDisjoint(histograms);
        }
        continue;
      }
    }
    let statRecord = nameStatsMap.get(stat.name);
    if (statRecord) {
      if (statRecord.value != stat.value) {
        statRecord.value = stat.value;
        ++statRecord.change_count;
      }
    } else {
      statRecord = {name: stat.name, value: stat.value, change_count: 0};
      nameStatsMap.set(stat.name, statRecord);
    }
    sortedStats.push(statRecord);
  }

  // Sorts all the stats. This is inefficient; we should just pick the top N
  // based on field "active-max-display-count" and sort those. The best
  // algorithms for this require a heap or priority queue. JS implementations
  // of those can be found, but that would bloat this relatively modest amount
  // of code, and compel us to do a better job writing tests.
  sortedStats.sort(compareStatRecords);

  const max = loadSettingOrUseDefault('active-max-display-count', 50);
  let index = 0;
  let text = '';
  for (const statRecord of sortedStats) {
    if (++index > max) {
      break;
    }
    text += `${statRecord.name}: ${statRecord.value} (${statRecord.change_count})\n`;
  }
  activeStatsPreElement.textContent = text;

  // If a post-render test-hook has been established, call it, but clear
  // the hook first, so that the callee can set a new hook if it wants to.
  if (postRenderTestHook) {
    const hook = postRenderTestHook;
    postRenderTestHook = null;
    hook();
  }

  statusDiv.textContent = '';
}

function renderHistogramDisjoint(histograms) {
  activeStatsHistogramsDiv.replaceChildren();
  for (histogram of histograms) {
    const div = document.createElement('div');

    const label = document.createElement('div');
    label.textContent = histogram.name;
    div.appendChild(label);

    let maxValue = 0;
    for (bucket of histogram.buckets) {
      maxValue = Math.max(maxValue, bucket.cumulative);
    }

    const graphics = document.createElement('div');
    graphics.className = 'histogram-graphics';
    for (bucket of histogram.buckets) {
      const span = document.createElement('span');
      const percent = maxValue == 0 ? 0 : Math.round((100 * bucket.cumulative) / maxValue);
      span.style.height = '' + percent + '%';
      graphics.appendChild(span);
    }
    div.appendChild(graphics);

    const labels = document.createElement('div');
    labels.className = 'histogram-labels';
    for (bucket of histogram.buckets) {
      const span = document.createElement('span');
      span.textContent = bucket.upper_bound + ':' + bucket.cumulative;
      labels.appendChild(span);
    }
    div.appendChild(labels);

    activeStatsHistogramsDiv.appendChild(div);
  }
}

const log_10 = Math.log(10);
function log10(num) {
  return Math.log(num) / log_10;
}

// https://stackoverflow.com/questions/4059147/check-if-a-variable-is-a-string-in-javascript
function isString(x) {
  return Object.prototype.toString.call(x) === "[object String]";
}

function renderHistogramDetail(supported_percentiles, detail) {
  activeStatsHistogramsDiv.replaceChildren();
  for (histogram of detail) {
    const div = document.createElement('div');
    const label = document.createElement('div');
    let name_and_percentiles = histogram.name;
    let i = 0;
    const percentile_values = histogram.percentiles;
    let minValue = null;
    let maxValue = null;
    const updateMinMaxValue = (value) => {
      if (minValue == null) {
        minValue = value;
        maxValue = value;
      } else if (value < minValue) {
        minValue = value;
      } else if (value > minValue) {
        maxValue = value;
      }
    };

    let values = [];

    for (i = 0; i < supported_percentiles.length; ++i) {
      const value = percentile_values[i].cumulative;
      name_and_percentiles += ' P' + supported_percentiles[i] + '=' + value;
      updateMinMaxValue(value);
      values.push([value, 'P' + supported_percentiles[i] + '=' + value]);
    }
    label.textContent = name_and_percentiles;
    div.appendChild(label);

    // First we can over the detailed bucket value and count info, and determine
    // some limits so we can scale the histogram data to (for now) consume
    // the desired width and height, which we'll express as percentages, so
    // the graphics stretch when the user stretches the window.

    let maxCount = 0;
    for (bucket of histogram.detail) {
      maxCount = Math.max(maxCount, bucket.count);
      updateMinMaxValue(bucket.value);
    }
    console.log("min=" + minValue + " max=" + maxValue);

    const width = maxValue - minValue;
    const scaledValue = value => 9*((value - minValue) / width) + 1; // between 1 and 10;
    const valueToPercent = value => Math.round(80 * log10(scaledValue(value)));

    const graphics = document.createElement('div');
    const labels = document.createElement('div');
    labels.className = 'histogram-labels';
    graphics.className = 'histogram-graphics';
    for (bucket of histogram.detail) {
/*
      const span = document.createElement('span');
      span.className = 'histogram-buckets';
      const percent = maxCount == 0 ? 0 : Math.round((100 * bucket.count) / maxCount);
      span.style.height = '' + percent + '%';
      //span.style.width = '' + valueToPercent(bucket.value) + '%';
      graphics.appendChild(span);
*/
      values.push([bucket.value, bucket.count]);
    }

    values.sort((a, b) => {
      if (a[0] < b[0]) {
        return -1;
      } else if (a[0] > b[0]) {
        return 1;
      } else {
        if (isString(a[1])) {
          if (isString(b[1])) {
            return a[1] < b[1];
          } else {
            return 1;
          }
        }
        if (isString(b[1])) {
          return -1;
        }
        return a[1] - b[1];
      }
    });
    for (a of values) {
      //console.log('' + a[0] + ': ' + a[1]);
      const span = document.createElement('span');
      const label = document.createElement('span');
      if (isString(a[1])) {
        span.className = 'histogram-percentile';
        label.textContent = a[1];
      } else {
        span.className = 'histogram-buckets';
        const percent = maxCount == 0 ? 0 : Math.round((100 * a[1]) / maxCount);
        span.style.height = '' + percent + '%';
        label.textContent = a[0] + ':' + a[1];
      }
      //span.style.width = '' + valueToPercent(a[1]) + '%';
      graphics.appendChild(span);
      labels.appendChild(label);
    }

    div.appendChild(graphics);
    div.appendChild(labels);

    activeStatsHistogramsDiv.appendChild(div);
  }
}

function renderHistogramSummary(histograms) {
  activeStatsHistogramsDiv.replaceChildren();

  const supported_quantiles = histograms.supported_quantiles;
  const labels = document.createElement('div');
  labels.className = 'histogram-labels';
  for (quantile of supported_quantiles) {
    const span = document.createElement('span');
    span.textContent = 'P' + quantile;
    labels.appendChild(span);
  }
  activeStatsHistogramsDiv.appendChild(labels);

  for (histogram of histograms.computed_quantiles) {
    const div = document.createElement('div');

    const label = document.createElement('div');
    label.textContent = histogram.name;
    div.appendChild(label);

    let maxValue = 0;
    let values = [];
    let prevValue = 0;
    for (obj of histogram.values) {
      const value = obj.cumulative - prevValue;
      prevValue = obj.cumulative;
      values.push(value);
      maxValue = Math.max(maxValue, value);
    }

    const graphics = document.createElement('div');
    graphics.className = 'histogram-graphics';
    for (value of values) {
      const span = document.createElement('span');
      const percent = maxValue == 0 ? 0 : Math.round((100 * value) / maxValue);
      span.style.height = '' + percent + '%';
      graphics.appendChild(span);
    }
    div.appendChild(graphics);

    const labels = document.createElement('div');
    labels.className = 'histogram-labels';
    for (value of values) {
      const span = document.createElement('span');
      span.textContent = Math.round(100 * value) / 100;
      labels.appendChild(span);
    }
    div.appendChild(labels);

    activeStatsHistogramsDiv.appendChild(div);
  }
}

// We don't want to trigger any DOM manipulations until the DOM is fully loaded.
addEventListener('DOMContentLoaded', initHook);
