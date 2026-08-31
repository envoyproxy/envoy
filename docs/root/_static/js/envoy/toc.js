/**
 * Page table of contents: outline rendering plus scroll tracking.
 *
 * Generated proto pages list entries such as
 * `config.listener.v3.Listener.ConnectionBalanceConfig.ExactBalance`. Every one
 * repeats a namespace the reader already knows, and the segment that actually
 * identifies it wraps mid-word. Here the package moves to the column heading
 * and each entry keeps only its own trailing segment, letting the existing
 * indentation carry the hierarchy.
 */

/**
 * protodoc titles enums and services with the kind in front of the name. The
 * word is stripped here so the outline shows the symbol; the kind itself is
 * shown by the coloured dot that envoy/proto.js adds.
 */
const KIND_PREFIXES = [/^Enum\s+/i, /^Service\s+/i];

/** A dotted proto symbol: a lower-case package then CamelCase segments. */
const SYMBOL = /^(?:[a-z][\w]*\.)+[A-Z]\w*(?:\.[A-Z]\w*)*$/;

function packageOf(symbol) {
  const match = /^(?:[a-z][\w]*\.)+/.exec(symbol);
  return match ? match[0].slice(0, -1) : null;
}

function stripPackage(symbol) {
  return symbol.replace(/^(?:[a-z][\w]*\.)+/, '');
}

/** The longest common package across every symbol on the page, if any. */
function commonPackage(symbols) {
  const packages = new Set(symbols.map(packageOf).filter(Boolean));
  return packages.size === 1 ? packages.values().next().value : null;
}

function renderSymbolToc(toc, links) {
  const entries = links.map((link) => {
    const text = link.textContent.trim();
    const symbol = KIND_PREFIXES.reduce((name, pattern) => name.replace(pattern, ''), text);

    return {link, symbol, isSymbol: SYMBOL.test(symbol)};
  });

  const symbols = entries.filter((entry) => entry.isSymbol);

  // Only reshape a page that is predominantly a symbol index.
  if (symbols.length < 2 || symbols.length < entries.length / 2) {
    return;
  }

  const shared = commonPackage(symbols.map((entry) => entry.symbol));
  if (shared) {
    const title = toc.querySelector('.envoy-page-toc-title');
    if (title) {
      title.textContent = shared;
    }
  }

  toc.classList.add('envoy-symbol-toc');

  symbols.forEach((entry) => {
    // One segment per entry. The package is in the column heading and the
    // nesting is in the indentation, so the trailing segment is the only part
    // that still identifies this symbol. The full name stays in the tooltip.
    const segments = stripPackage(entry.symbol).split('.');

    entry.link.textContent = segments[segments.length - 1];
    entry.link.title = entry.symbol;
  });
}

function trackScroll(toc, links) {
  const targets = links.map((link) => {
    const href = link.getAttribute('href');
    const id = decodeURIComponent(href.slice(1));
    return {link, heading: document.getElementById(id)};
  }).filter((target) => target.heading);

  if (!targets.length) {
    return;
  }

  let scheduled = false;

  function update() {
    scheduled = false;
    const offset = 110;
    let current = targets[0];

    targets.forEach((target) => {
      if (target.heading.getBoundingClientRect().top <= offset) {
        current = target;
      }
    });

    targets.forEach((target) => {
      const isCurrent = target === current;
      target.link.classList.toggle('is-current', isCurrent);
      if (isCurrent) {
        target.link.setAttribute('aria-current', 'location');
      } else {
        target.link.removeAttribute('aria-current');
      }
    });
  }

  function schedule() {
    if (!scheduled) {
      scheduled = true;
      window.requestAnimationFrame(update);
    }
  }

  window.addEventListener('scroll', schedule, {passive: true});
  window.addEventListener('resize', schedule);
  update();
}

export function init() {
  const toc = document.querySelector('.envoy-page-toc');
  const layout = document.querySelector('.envoy-content-layout');
  if (!toc || !layout) {
    return;
  }

  const links = Array.from(toc.querySelectorAll('a[href^="#"]'))
    .filter((link) => link.getAttribute('href') !== '#');

  // A page with one heading has no outline worth a column.
  if (links.length < 2) {
    toc.hidden = true;
    layout.classList.remove('envoy-has-page-toc');
    return;
  }

  renderSymbolToc(toc, links);
  trackScroll(toc, links);
}
