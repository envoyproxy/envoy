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
 * A div into which we render histograms.
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
  activeStatsHistogramsDiv = document.createElement('div');
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
  const params = ['filter', 'type'];
  const href = window.location.href;

  // Compute the fetch URL prefix based on the current URL, so that the admin
  // site can be hosted underneath a site-specific URL structure.
  const statsPos = href.indexOf('/stats?');
  if (statsPos == -1) {
    statusDiv.textContent = 'Cannot find /stats? in ' + href;
    return;
  }
  const prefix = href.substring(0, statsPos);
  const url = prefix + '/stats?format=json&usedonly&histogram_buckets=detailed&' +
        params.map(makeQueryParam).join('&');

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
      continue; // Skip histograms for now.
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
  renderHistograms(activeStatsHistogramsDiv, data);

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

// We don't want to trigger any DOM manipulations until the DOM is fully loaded.
addEventListener('DOMContentLoaded', initHook);
