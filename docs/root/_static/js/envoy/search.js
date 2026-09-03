/**
 * The ⌘K overlay.
 *
 * Built on the `searchindex.js` Sphinx already generates, so there is no
 * external service, no key to rotate and nothing extra to fetch at build time.
 * The index is loaded on first open, not on page load.
 *
 * The overlay searches page and section titles, which is what a quick switcher
 * is for. Full-text search stays on /search.html, one Enter away, and remains
 * the only search when JavaScript is unavailable.
 */

const MAX_RESULTS = 24;

/** Top-level path segment -> the area label shown above a group of results. */
const AREAS = {
  'api-docs': 'API reference',
  'api-v3': 'API reference',
  configuration: 'Configuration',
  extending: 'Extending Envoy',
  faq: 'FAQ',
  intro: 'Introduction',
  operations: 'Operations',
  start: 'Getting started',
  version_history: 'Version history',
  xds: 'xDS protocol',
};

/** A dotted proto symbol: a lower-case package then CamelCase segments. */
const SYMBOL = /^(?:[a-z][\w]*\.)+[A-Z]\w*(?:\.[A-Z]\w*)*$/;

let index = null;
let loading = null;

function areaOf(docname) {
  const segment = docname.split('/')[0];
  if (AREAS[segment]) {
    return AREAS[segment];
  }
  if (!segment || segment === docname) {
    return 'Documentation';
  }
  return segment.replace(/[-_]/g, ' ').replace(/^./, (c) => c.toUpperCase());
}

function loadIndex(url) {
  if (index) {
    return Promise.resolve(index);
  }
  if (loading) {
    return loading;
  }

  loading = new Promise((resolve, reject) => {
    // searchindex.js calls Search.setIndex(). Stand in for it just long enough
    // to capture the payload, then put back whatever was there before.
    const previous = Object.prototype.hasOwnProperty.call(window, 'Search') ?
      window.Search : undefined;

    window.Search = {
      setIndex(payload) {
        index = payload;
        if (previous && typeof previous.setIndex === 'function') {
          previous.setIndex(payload);
        }
      },
    };

    const script = document.createElement('script');
    script.src = url;
    script.addEventListener('load', () => {
      if (previous === undefined) {
        delete window.Search;
      } else {
        window.Search = previous;
      }
      script.remove();
      index ? resolve(index) : reject(new Error('search index was empty'));
    });
    script.addEventListener('error', () => {
      if (previous === undefined) {
        delete window.Search;
      } else {
        window.Search = previous;
      }
      script.remove();
      reject(new Error('search index failed to load'));
    });

    document.head.append(script);
  });

  return loading;
}

/**
 * Every page title and section heading, flattened into one searchable list.
 * `alltitles` maps a heading to the documents and anchors it appears in.
 */
function entriesFrom(payload) {
  const entries = [];
  const seen = new Set();

  Object.entries(payload.alltitles || {}).forEach(([title, locations]) => {
    locations.forEach(([doc, anchor]) => {
      const docname = payload.docnames[doc];
      if (docname === undefined) {
        return;
      }
      const key = `${docname}#${anchor || ''}`;
      if (seen.has(key)) {
        return;
      }
      seen.add(key);
      entries.push({
        title,
        docname,
        anchor: anchor || null,
        // A page title outranks a heading inside a page.
        weight: anchor ? 0 : 1,
      });
    });
  });

  return entries;
}

function score(title, query) {
  const haystack = title.toLowerCase();
  const at = haystack.indexOf(query);
  if (at === -1) {
    return -1;
  }
  if (at === 0) {
    return 3;
  }
  // A match that starts a word beats one buried mid-token.
  return /[\s.\-_/]/.test(haystack[at - 1]) ? 2 : 1;
}

function search(payload, query) {
  const needle = query.trim().toLowerCase();
  if (!needle) {
    return [];
  }

  return entriesFrom(payload)
    .map((entry) => ({entry, rank: score(entry.title, needle)}))
    .filter((hit) => hit.rank > 0)
    .sort((a, b) =>
      (b.rank + b.entry.weight) - (a.rank + a.entry.weight) ||
      a.entry.title.length - b.entry.title.length)
    .slice(0, MAX_RESULTS)
    .map((hit) => hit.entry);
}

function highlight(text, query) {
  const at = text.toLowerCase().indexOf(query.trim().toLowerCase());
  const fragment = document.createDocumentFragment();

  if (at === -1) {
    fragment.append(text);
    return fragment;
  }

  const mark = document.createElement('mark');
  mark.textContent = text.slice(at, at + query.trim().length);
  fragment.append(text.slice(0, at), mark, text.slice(at + query.trim().length));
  return fragment;
}

function renderTitle(entry, query) {
  const title = document.createElement('span');
  title.className = 'envoy-search-hit-title';

  if (SYMBOL.test(entry.title)) {
    // Show the package dimmed so the identifying tail carries the contrast.
    title.classList.add('envoy-search-hit-symbol');
    const split = entry.title.lastIndexOf('.', entry.title.search(/\.[A-Z]/) + 1);
    const cut = split > 0 ? split + 1 : 0;
    const pkg = document.createElement('span');
    pkg.className = 'envoy-search-hit-package';
    pkg.textContent = entry.title.slice(0, cut);
    title.append(pkg, highlight(entry.title.slice(cut), query));
  } else {
    title.append(highlight(entry.title, query));
  }

  return title;
}

export function init() {
  const trigger = document.querySelector('[data-envoy-search-open]');
  const dialog = document.querySelector('[data-envoy-search-dialog]');
  if (!trigger || !dialog || typeof dialog.showModal !== 'function') {
    return;
  }

  const input = dialog.querySelector('input[type="search"]');
  const list = dialog.querySelector('[data-envoy-search-results]');
  const count = dialog.querySelector('[data-envoy-search-count]');
  const indexUrl = dialog.dataset.envoySearchIndex;
  const root = dialog.dataset.envoyRoot || '';
  const searchPage = dialog.dataset.envoySearchPage || '';

  if (!input || !list || !indexUrl) {
    return;
  }

  let hits = [];
  let active = -1;

  document.querySelectorAll('[data-envoy-search-shortcut]').forEach((element) => {
    element.textContent =
      /Mac|iPhone|iPad/.test(window.navigator.platform) ? '⌘K' : 'Ctrl K';
  });

  function setActive(next) {
    const nodes = list.querySelectorAll('.envoy-search-hit');
    if (!nodes.length) {
      active = -1;
      return;
    }

    active = (next + nodes.length) % nodes.length;
    nodes.forEach((node, i) => node.classList.toggle('is-active', i === active));
    nodes[active].scrollIntoView({block: 'nearest'});
  }

  function message(text) {
    list.replaceChildren();
    const empty = document.createElement('p');
    empty.className = 'envoy-search-empty';
    empty.textContent = text;
    list.append(empty);
    if (count) {
      count.textContent = '';
    }
  }

  function render(query) {
    if (!index) {
      return;
    }

    hits = search(index, query);
    list.replaceChildren();
    active = -1;

    if (!query.trim()) {
      message('Search page and section titles. Press Enter for full text.');
      return;
    }

    if (!hits.length) {
      message(`No title matches “${query.trim()}”. Press Enter to search full text.`);
      return;
    }

    // Collect by area before rendering: the ranked order interleaves areas, and
    // a heading that reappears three times reads as three separate groups.
    const groups = new Map();
    hits.forEach((entry) => {
      const area = areaOf(entry.docname);
      if (!groups.has(area)) {
        groups.set(area, []);
      }
      groups.get(area).push(entry);
    });

    // A Map keeps insertion order, so the best-ranked area still comes first.
    groups.forEach((entries, area) => {
      const heading = document.createElement('p');
      heading.className = 'envoy-search-group';
      heading.textContent = area;
      list.append(heading);

      entries.forEach((entry) => {
        const link = document.createElement('a');
        link.className = 'envoy-search-hit';
        link.href =
          `${root}${entry.docname}.html${entry.anchor ? `#${entry.anchor}` : ''}`;

        const path = document.createElement('span');
        path.className = 'envoy-search-hit-path';
        path.textContent = entry.docname;

        link.append(renderTitle(entry, query), path);
        list.append(link);
      });
    });

    if (count) {
      count.textContent =
        `${hits.length} title${hits.length === 1 ? '' : 's'}`;
    }
    setActive(0);
  }

  function fullText() {
    if (!searchPage) {
      return;
    }
    window.location.href =
      `${searchPage}?q=${encodeURIComponent(input.value.trim())}`;
  }

  function open() {
    dialog.showModal();
    input.select();
    message('Loading the search index…');

    loadIndex(indexUrl)
      .then(() => render(input.value))
      .catch(() => message('The search index could not be loaded. Press Enter to search full text.'));
  }

  trigger.addEventListener('click', open);

  input.addEventListener('input', () => render(input.value));

  input.addEventListener('keydown', (event) => {
    if (event.key === 'ArrowDown') {
      event.preventDefault();
      setActive(active + 1);
    } else if (event.key === 'ArrowUp') {
      event.preventDefault();
      setActive(active - 1);
    } else if (event.key === 'Enter') {
      event.preventDefault();
      const nodes = list.querySelectorAll('.envoy-search-hit');
      if (active >= 0 && nodes[active]) {
        window.location.href = nodes[active].href;
      } else {
        fullText();
      }
    }
  });

  dialog.addEventListener('click', (event) => {
    // A click on the backdrop lands on the dialog itself.
    if (event.target === dialog) {
      dialog.close();
    }
  });

  document.addEventListener('keydown', (event) => {
    if ((event.metaKey || event.ctrlKey) && !event.altKey &&
        event.key.toLowerCase() === 'k') {
      event.preventDefault();
      if (dialog.open) {
        dialog.close();
      } else {
        open();
      }
    }
  });
}
