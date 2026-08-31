(function() {
  'use strict';

  const root = document.documentElement;
  const themeKey = 'envoy-docs-theme';
  const themeMedia = window.matchMedia('(prefers-color-scheme: dark)');

  function storedTheme() {
    try {
      const value = window.localStorage.getItem(themeKey);
      return value === 'light' || value === 'dark' ? value : null;
    } catch (error) {
      return null;
    }
  }

  function setTheme(theme, persist) {
    root.dataset.envoyTheme = theme;
    root.style.colorScheme = theme;

    if (persist) {
      try {
        window.localStorage.setItem(themeKey, theme);
      } catch (error) {
        // The selected theme still applies when storage is unavailable.
      }
    }

    document.querySelectorAll('[data-envoy-theme-toggle]').forEach((button) => {
      button.setAttribute(
        'aria-label', theme === 'dark' ? 'Switch to light theme' : 'Switch to dark theme');
    });
  }

  setTheme(storedTheme() || (themeMedia.matches ? 'dark' : 'light'), false);

  function ready() {
    setTheme(root.dataset.envoyTheme === 'dark' ? 'dark' : 'light', false);

    const navToggle = document.querySelector('[data-envoy-nav-toggle]');
    const navSidebar = document.querySelector('.wy-nav-side');
    const navBackdrop = document.querySelector('.envoy-nav-backdrop');
    const navContent = document.querySelector('.wy-nav-content-wrap');
    const topbar = document.querySelector('.envoy-doc-topbar');
    const navMedia = window.matchMedia('(max-width: 900px)');
    let navOpen = false;
    let returnFocus = null;

    document.querySelectorAll('[data-envoy-theme-toggle]').forEach((button) => {
      button.addEventListener('click', () => {
        setTheme(root.dataset.envoyTheme === 'dark' ? 'light' : 'dark', true);
      });
    });

    const followSystemTheme = (event) => {
      if (!storedTheme()) {
        setTheme(event.matches ? 'dark' : 'light', false);
      }
    };

    if (typeof themeMedia.addEventListener === 'function') {
      themeMedia.addEventListener('change', followSystemTheme);
    } else {
      themeMedia.addListener(followSystemTheme);
    }

    document.querySelectorAll('[data-envoy-search-shortcut]').forEach((shortcut) => {
      shortcut.textContent =
        /Mac|iPhone|iPad/.test(window.navigator.platform) ? '⌘ K' : 'Ctrl K';
    });

    function focusableElements(container) {
      return Array.from(container.querySelectorAll(
        'a[href], button:not([disabled]), input:not([disabled]), ' +
        'select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])'
      )).filter((element) => !element.hidden && element.offsetParent !== null);
    }

    function setNavigation(open, restoreFocus) {
      if (!navToggle || !navSidebar || !navBackdrop) {
        return;
      }

      navOpen = open && navMedia.matches;
      document.body.classList.toggle('envoy-nav-open', navOpen);
      navToggle.setAttribute('aria-expanded', String(navOpen));
      navToggle.setAttribute(
        'aria-label', navOpen ? 'Close documentation navigation' :
          'Open documentation navigation');
      navBackdrop.hidden = !navOpen;

      const navigationHidden = navMedia.matches && !navOpen;
      navSidebar.inert = navigationHidden;
      if (navigationHidden) {
        navSidebar.setAttribute('aria-hidden', 'true');
      } else {
        navSidebar.removeAttribute('aria-hidden');
      }

      [navContent, topbar].forEach((element) => {
        if (!element) {
          return;
        }
        element.inert = navOpen;
        if (navOpen) {
          element.setAttribute('aria-hidden', 'true');
        } else {
          element.removeAttribute('aria-hidden');
        }
      });

      if (navOpen) {
        returnFocus = document.activeElement;
        const focusable = focusableElements(navSidebar);
        if (focusable.length) {
          window.requestAnimationFrame(() => focusable[0].focus());
        }
      } else if (restoreFocus && returnFocus instanceof HTMLElement) {
        returnFocus.focus();
      }
    }

    if (navSidebar) {
      navSidebar.id = 'envoy-doc-sidebar';
    }

    setNavigation(false, false);

    if (navToggle) {
      navToggle.addEventListener('click', () => setNavigation(!navOpen, true));
    }

    document.querySelectorAll('[data-envoy-nav-close]').forEach((button) => {
      button.addEventListener('click', () => setNavigation(false, true));
    });

    if (navSidebar) {
      navSidebar.addEventListener('click', (event) => {
        if (event.target.closest('a[href]') && navMedia.matches) {
          setNavigation(false, false);
        }
      });
    }

    document.addEventListener('keydown', (event) => {
      if (event.key === 'Escape') {
        document.querySelectorAll('details[open]').forEach((details) => {
          details.removeAttribute('open');
        });
        if (navOpen) {
          event.preventDefault();
          setNavigation(false, true);
        }
        return;
      }

      if (event.key === 'Tab' && navOpen && navSidebar) {
        const focusable = focusableElements(navSidebar);
        if (!focusable.length) {
          return;
        }

        const first = focusable[0];
        const last = focusable[focusable.length - 1];
        if (event.shiftKey && document.activeElement === first) {
          event.preventDefault();
          last.focus();
        } else if (!event.shiftKey && document.activeElement === last) {
          event.preventDefault();
          first.focus();
        }
      }

      if ((event.metaKey || event.ctrlKey) && !event.altKey &&
          event.key.toLowerCase() === 'k') {
        event.preventDefault();
        const desktopSearch = document.getElementById('envoy-doc-search-input');
        const sidebarSearch = document.getElementById('envoy-sidebar-search-input');

        if (navMedia.matches && (!desktopSearch || desktopSearch.offsetParent === null)) {
          setNavigation(true, false);
          window.requestAnimationFrame(() => sidebarSearch && sidebarSearch.focus());
        } else if (desktopSearch) {
          desktopSearch.focus();
          desktopSearch.select();
        }
      }
    });

    const closeNavigationAtBreakpoint = () => setNavigation(false, false);

    if (typeof navMedia.addEventListener === 'function') {
      navMedia.addEventListener('change', closeNavigationAtBreakpoint);
    } else {
      navMedia.addListener(closeNavigationAtBreakpoint);
    }

    const versionMenus = Array.from(document.querySelectorAll('.envoy-version-menu'));
    versionMenus.forEach((menu) => {
      menu.addEventListener('toggle', () => {
        if (menu.open) {
          versionMenus.filter((other) => other !== menu).forEach((other) => {
            other.removeAttribute('open');
          });
        }
      });
    });

    document.addEventListener('click', (event) => {
      versionMenus.forEach((menu) => {
        if (menu.open && !menu.contains(event.target)) {
          menu.removeAttribute('open');
        }
      });
    });

    const pageToc = document.querySelector('.envoy-page-toc');
    const contentLayout = document.querySelector('.envoy-content-layout');
    if (pageToc && contentLayout) {
      const tocLinks = Array.from(pageToc.querySelectorAll('a[href^="#"]')).filter(
        (link) => link.getAttribute('href') !== '#');
      const sections = tocLinks.map((link) => {
        const id = decodeURIComponent(link.getAttribute('href').slice(1));
        return {link: link, heading: document.getElementById(id)};
      }).filter((item) => item.heading);

      if (sections.length < 2) {
        pageToc.hidden = true;
        contentLayout.classList.remove('envoy-has-page-toc');
      } else {
        let scheduled = false;
        const updateCurrentSection = () => {
          scheduled = false;
          const offset = 110;
          let current = sections[0];

          sections.forEach((section) => {
            if (section.heading.getBoundingClientRect().top <= offset) {
              current = section;
            }
          });

          sections.forEach((section) => {
            const isCurrent = section === current;
            section.link.classList.toggle('is-current', isCurrent);
            if (isCurrent) {
              section.link.setAttribute('aria-current', 'location');
            } else {
              section.link.removeAttribute('aria-current');
            }
          });
        };

        const scheduleCurrentSection = () => {
          if (!scheduled) {
            scheduled = true;
            window.requestAnimationFrame(updateCurrentSection);
          }
        };

        window.addEventListener('scroll', scheduleCurrentSection, {passive: true});
        window.addEventListener('resize', scheduleCurrentSection);
        updateCurrentSection();
      }
    }
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', ready);
  } else {
    ready();
  }
})();
