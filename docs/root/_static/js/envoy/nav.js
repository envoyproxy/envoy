/**
 * Sidebar navigation: an accordion over the toctree captions, plus the mobile
 * drawer.
 *
 * The Envoy tree has nine top-level areas and thousands of pages behind them.
 * sphinx_rtd_theme renders every caption and its subtree at the same visual
 * weight, which gives no sense of place. Here each caption becomes a
 * <details> and only the section containing the current page starts open.
 */

const DESKTOP_QUERY = '(max-width: 960px)';

/** Groups each `p.caption` and the `ul` that follows it into a disclosure. */
function buildSections(menu) {
  const captions = Array.from(menu.querySelectorAll(':scope > p.caption'));
  if (!captions.length) {
    return;
  }

  captions.forEach((caption) => {
    const list = caption.nextElementSibling;
    if (!list || list.tagName !== 'UL') {
      return;
    }

    const label = caption.textContent.trim();
    const details = document.createElement('details');
    details.className = 'envoy-nav-section';

    const summary = document.createElement('summary');
    summary.textContent = label;
    details.append(summary);

    caption.replaceWith(details);
    details.append(list);

    // Open the section holding the current page; if the reader is on a page
    // that is in no toctree, leave the first section open as a starting point.
    if (list.querySelector('.current')) {
      details.open = true;
    }
  });

  const sections = Array.from(menu.querySelectorAll('.envoy-nav-section'));
  if (sections.length && !sections.some((section) => section.open)) {
    sections[0].open = true;
  }
}

function focusable(container) {
  return Array.from(container.querySelectorAll(
    'a[href], button:not([disabled]), input:not([disabled]), summary, ' +
    '[tabindex]:not([tabindex="-1"])'
  )).filter((element) => !element.hidden && element.offsetParent !== null);
}

export function init() {
  const menu = document.querySelector('.wy-menu-vertical');
  if (menu) {
    buildSections(menu);

    // Keep the current page in view when the sidebar is taller than the pane.
    const current = menu.querySelector('a.current');
    if (current) {
      window.requestAnimationFrame(() => {
        current.scrollIntoView({block: 'nearest'});
      });
    }
  }

  const toggle = document.querySelector('[data-envoy-nav-toggle]');
  const sidebar = document.querySelector('.wy-nav-side');
  const backdrop = document.querySelector('.envoy-nav-backdrop');
  const content = document.querySelector('.wy-nav-content-wrap');
  const topbar = document.querySelector('.envoy-doc-topbar');
  const mobile = window.matchMedia(DESKTOP_QUERY);

  if (!toggle || !sidebar || !backdrop) {
    return;
  }

  sidebar.id = sidebar.id || 'envoy-doc-sidebar';

  let open = false;
  let returnFocus = null;

  function setOpen(next, restoreFocus) {
    open = next && mobile.matches;
    document.body.classList.toggle('envoy-nav-open', open);
    toggle.setAttribute('aria-expanded', String(open));
    toggle.setAttribute(
      'aria-label', open ? 'Close documentation navigation' :
        'Open documentation navigation');
    backdrop.hidden = !open;

    const hidden = mobile.matches && !open;
    sidebar.inert = hidden;
    sidebar.toggleAttribute('aria-hidden', hidden);

    [content, topbar].forEach((element) => {
      if (!element) {
        return;
      }
      element.inert = open;
      element.toggleAttribute('aria-hidden', open);
    });

    if (open) {
      returnFocus = document.activeElement;
      const targets = focusable(sidebar);
      if (targets.length) {
        window.requestAnimationFrame(() => targets[0].focus());
      }
    } else if (restoreFocus && returnFocus instanceof HTMLElement) {
      returnFocus.focus();
    }
  }

  setOpen(false, false);

  toggle.addEventListener('click', () => setOpen(!open, true));

  document.querySelectorAll('[data-envoy-nav-close]').forEach((button) => {
    button.addEventListener('click', () => setOpen(false, true));
  });

  sidebar.addEventListener('click', (event) => {
    if (event.target.closest('a[href]') && mobile.matches) {
      setOpen(false, false);
    }
  });

  document.addEventListener('keydown', (event) => {
    if (!open) {
      return;
    }

    if (event.key === 'Escape') {
      event.preventDefault();
      setOpen(false, true);
      return;
    }

    if (event.key === 'Tab') {
      const targets = focusable(sidebar);
      if (!targets.length) {
        return;
      }

      const first = targets[0];
      const last = targets[targets.length - 1];
      if (event.shiftKey && document.activeElement === first) {
        event.preventDefault();
        last.focus();
      } else if (!event.shiftKey && document.activeElement === last) {
        event.preventDefault();
        first.focus();
      }
    }
  });

  mobile.addEventListener('change', () => setOpen(false, false));
}
