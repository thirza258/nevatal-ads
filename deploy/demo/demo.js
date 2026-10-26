(function () {
  'use strict';

  var DEFAULT_PLACEMENTS = ['header-banner', 'in-article', 'sidebar-card', 'footer-banner'];
  var SITE_KEY_PATTERN = /^[A-Za-z0-9_-]{8,128}$/;
  var PLACEMENT_KEY_PATTERN = /^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$/;

  var status = document.getElementById('demo-status');
  var form = document.getElementById('site-key-form');
  var input = document.getElementById('site-key');
  var originLabel = document.getElementById('demo-origin');
  var list = document.getElementById('placement-list');

  if (originLabel) originLabel.textContent = window.location.origin;

  function setStatus(message, tone) {
    if (!status) return;
    status.textContent = message;
    status.className = tone ? 'status status-' + tone : 'status';
  }

  function readPlacements() {
    var raw = new URLSearchParams(window.location.search).get('placements') || '';
    var candidates = raw
      ? raw.split(',')
      : DEFAULT_PLACEMENTS;
    var seen = {};
    var keys = [];
    candidates.forEach(function (candidate) {
      var key = (candidate || '').trim();
      if (!PLACEMENT_KEY_PATTERN.test(key) || seen[key] || keys.length >= 12) return;
      seen[key] = true;
      keys.push(key);
    });
    return keys.length ? keys : DEFAULT_PLACEMENTS;
  }

  function buildPlacementElements(keys) {
    if (!list) return;
    list.replaceChildren();
    keys.forEach(function (key, index) {
      var section = document.createElement('section');
      section.className = 'placement-card';

      var heading = document.createElement('div');
      heading.className = 'placement-heading';

      var title = document.createElement('h3');
      title.textContent = 'Placement ' + (index + 1);

      var code = document.createElement('code');
      code.textContent = key;

      heading.appendChild(title);
      heading.appendChild(code);

      var stage = document.createElement('div');
      stage.className = 'placement-stage';

      var container = document.createElement('div');
      container.setAttribute('data-adcontrol-placement', key);
      stage.appendChild(container);

      section.appendChild(heading);
      section.appendChild(stage);
      list.appendChild(section);
    });
  }

  function loadSdk(siteKey) {
    var script = document.createElement('script');
    script.src = '/sdk/v1/adcontrol.js';
    script.async = true;
    script.setAttribute('data-site-key', siteKey);
    script.setAttribute('data-api-base', window.location.origin);
    script.addEventListener('error', function () {
      setStatus(
        'The AdControl SDK could not be loaded. The rest of this page is unaffected.',
        'error',
      );
    });
    script.addEventListener('load', function () {
      setStatus(
        'SDK loaded. Waiting for delivery configuration from the API…',
        'info',
      );
      window.setTimeout(function () {
        var filled = document.querySelectorAll('[data-adcontrol-state="filled"]').length;
        if (filled > 0) {
          setStatus(
            filled + ' placement' + (filled === 1 ? '' : 's') + ' filled. Impressions are counted after 1 second at 50% visibility.',
            'ok',
          );
        } else {
          setStatus(
            'SDK loaded, but no ad was delivered. Check that the site key is active, a campaign is live, and this origin is in the site allowlist.',
            'warn',
          );
        }
      }, 5000);
    });
    document.head.appendChild(script);
  }

  var params = new URLSearchParams(window.location.search);
  var rawKey = (params.get('site_key') || '').trim();
  var siteKey = SITE_KEY_PATTERN.test(rawKey) ? rawKey : '';
  var placements = readPlacements();

  buildPlacementElements(placements);

  if (!rawKey) {
    setStatus(
      'Add ?site_key=<your public site key> to this URL, or paste a key below. The demo page itself always renders.',
      'info',
    );
    if (form) form.hidden = false;
  } else if (!siteKey) {
    setStatus('That site key does not look valid. Copy the public key from the AdControl Websites page.', 'error');
    if (form) form.hidden = false;
  } else {
    setStatus('Loading the AdControl SDK…', 'info');
    if (form) form.hidden = true;
    loadSdk(siteKey);
  }

  if (form && input) {
    form.addEventListener('submit', function (event) {
      event.preventDefault();
      var entered = input.value.trim();
      if (!SITE_KEY_PATTERN.test(entered)) {
        setStatus('Enter a valid public site key (letters, digits, underscore, or dash).', 'error');
        input.focus();
        return;
      }
      var next = new URL(window.location.href);
      next.searchParams.set('site_key', entered);
      next.searchParams.set('placements', placements.join(','));
      window.location.assign(next.toString());
    });
  }
})();
