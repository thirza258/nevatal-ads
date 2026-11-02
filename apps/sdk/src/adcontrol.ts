/**
 * AdControl Browser Delivery SDK (v1.3.0)
 *
 * Loads centrally managed creatives, renders them with safe DOM APIs, and
 * reports a single qualified impression per rendered placement/campaign.
 *
 * Fail-open by design: every public method catches its own errors and never
 * throws into the host page. Click tracking happens in the server redirect so
 * the browser never posts to a destination the administrator has not signed.
 */

(function () {
  'use strict';

  type PlacementFormat = 'responsive_banner' | 'image' | 'text_link' | 'native_card' | 'product_card';
  type FallbackBehavior = 'hide' | 'blank' | 'custom';

  interface PlacementConfig {
    campaign_id: string; // empty for an active placement with no eligible ad

    placement_id: string;
    placement_key: string;
    format: PlacementFormat;
    placement_format: PlacementFormat;
    width: number;
    height: number;
    fallback_behavior: FallbackBehavior;
    title: string;
    body: string;
    image_url: string;
    alt_text: string;
    cta_label: string;
    click_url: string;
    source_type?: string;
    affiliate?: boolean;
    disclosure_text?: string;
    merchant_name?: string;
    price_text?: string;
    currency?: string;
    frequency_cap_impressions?: number;
    frequency_cap_window_hours?: number;
    audience_segment_id?: string;
    experiment_id?: string;
    experiment_variant_id?: string;
  }

  interface SiteDeliveryConfig {
    site_key: string;
    site_name: string;
    config_version: number;
    refresh_interval_seconds?: number;
    max_stale_seconds?: number;
    placements: Record<string, PlacementConfig>;
  }

  interface SDKOptions {
    siteKey?: string;
    apiBase?: string;
    debug?: boolean;
    refreshIntervalMs?: number;
    maxStaleMs?: number;
  }

  interface AdControlPublic {
    version: string;
    init(options?: SDKOptions): AdControlPublic;
    scan(): Promise<void>;
    refresh(): Promise<void>;
    destroy(): void;
    getVersion(): string;
  }

  interface ScriptConfiguration {
    siteKey: string | null;
    apiBase: string;
    stylesheetUrl: string;
    debug: boolean;
  }

  interface ImpressionWatch {
    campaignId: string;
    placementKey: string;
    eventId: string;
    ratio: number;
    timer: number | null;
    done: boolean;
    frequencyCap: number;
    frequencyWindowHours: number;
  }

  const SDK_VERSION = '1.3.0';
  const DEFAULT_AFFILIATE_DISCLOSURE =
    'Affiliate link: we may earn a commission from qualifying purchases.';
  const DEFAULT_REFRESH_MS = 30_000;
  const DEFAULT_MAX_STALE_MS = 300_000;
  const FETCH_TIMEOUT_MS = 8_000;
  const IMPRESSION_MS = 1_000;
  const MIN_REFRESH_MS = 15_000;
  const MAX_REFRESH_MS = 300_000;
  const VISITOR_STORAGE_KEY = 'adcontrol_session_v1';
  const FREQUENCY_STORAGE_PREFIX = 'adcontrol_freq_v1:';

  const win = window as unknown as Record<string, unknown> & Window;

  // Do not double-install if the snippet was accidentally included twice.
  const existing = (win as any).AdControl;
  if (existing && typeof existing.getVersion === 'function') return;

  let initialized = false;
  let stopped = true;
  let activeSiteKey: string | null = null;
  let apiBase = '';
  let stylesheetUrl = '';
  let debugMode = false;

  let siteConfig: SiteDeliveryConfig | null = null;
  let configFetchedAt = 0;
  let configEtag: string | null = null;
  let refreshIntervalMs = DEFAULT_REFRESH_MS;
  let maxStaleMs = DEFAULT_MAX_STALE_MS;

  let refreshTimer: number | null = null;
  let scanTimer: number | null = null;
  let fetchInFlight: Promise<SiteDeliveryConfig | null> | null = null;
  let scanRunning = false;
  let scanAgain = false;

  let intersectionObserver: IntersectionObserver | null = null;
  let mutationObserver: MutationObserver | null = null;

  const renderedElements = new Set<HTMLElement>();
  const renderedSignatures = new WeakMap<Element, string>();
  const fallbackSignatures = new WeakMap<Element, string>();
  const impressionWatches = new WeakMap<Element, ImpressionWatch>();
  const watchedElements = new Set<Element>();
  const completedImpressions = new WeakMap<Element, string>();

  function log(message: string, ...args: unknown[]) {
    if (!debugMode) return;
    try {
      console.log('[AdControl SDK]', message, ...args);
    } catch (_) {
      // Logging must never break the page.
    }
  }

  function clampNumber(value: unknown, minimum: number, maximum: number, fallback: number): number {
    const number = typeof value === 'number' && Number.isFinite(value) ? value : Number(value);
    if (!Number.isFinite(number)) return fallback;
    return Math.max(minimum, Math.min(maximum, number));
  }

  function normalizeOrigin(value: string): string {
    if (!value) return '';
    try {
      return new URL(value, window.location.href).origin;
    } catch (_) {
      return '';
    }
  }

  function generateEventId(): string {
    try {
      if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') {
        return 'imp-' + crypto.randomUUID();
      }
      if (typeof crypto !== 'undefined' && typeof crypto.getRandomValues === 'function') {
        const bytes = new Uint8Array(18);
        crypto.getRandomValues(bytes);
        let value = '';
        for (let index = 0; index < bytes.length; index += 1) {
          value += bytes[index].toString(16).padStart(2, '0');
        }
        return 'imp-' + value;
      }
    } catch (_) {
      // Fall through to the non-cryptographic identifier below.
    }
    return 'imp-' + Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 14);
  }

  function sessionVisitorId(): string {
    try {
      const existing = window.sessionStorage.getItem(VISITOR_STORAGE_KEY);
      if (existing && /^[A-Za-z0-9._:-]{8,128}$/.test(existing)) return existing;
      const generated = generateEventId().replace(/^imp-/, 'acs-');
      window.sessionStorage.setItem(VISITOR_STORAGE_KEY, generated);
      return generated;
    } catch (_) {
      return '';
    }
  }

  function deliveryContextParams(): string {
    const params = new URLSearchParams();
    const visitor = sessionVisitorId();
    if (visitor) params.set('visitor_id', visitor);
    try {
      const current = new URLSearchParams(window.location.search);
      for (const key of ['utm_source', 'utm_medium', 'utm_campaign', 'utm_term', 'utm_content']) {
        const value = (current.get(key) || '').trim();
        if (value) params.set(key, value.slice(0, 160));
      }
    } catch (_) {
      // Attribution parameters are optional.
    }
    const text = params.toString();
    return text ? '?' + text : '';
  }

  function frequencyEntries(campaignId: string, windowHours: number): number[] {
    try {
      const raw = window.localStorage.getItem(FREQUENCY_STORAGE_PREFIX + campaignId);
      if (!raw) return [];
      const parsed = JSON.parse(raw) as unknown;
      if (!Array.isArray(parsed)) return [];
      const cutoff = Date.now() - Math.max(1, windowHours) * 3_600_000;
      return parsed
        .filter((value): value is number => typeof value === 'number' && Number.isFinite(value))
        .filter((value) => value > cutoff);
    } catch (_) {
      return [];
    }
  }

  function frequencyCapReached(ad: PlacementConfig): boolean {
    const cap = Number(ad.frequency_cap_impressions) || 0;
    if (cap <= 0 || !ad.campaign_id) return false;
    const windowHours = Number(ad.frequency_cap_window_hours) || 24;
    return frequencyEntries(ad.campaign_id, windowHours).length >= cap;
  }

  function recordFrequency(campaignId: string, cap: number, windowHours: number) {
    if (cap <= 0) return;
    try {
      const entries = frequencyEntries(campaignId, windowHours);
      entries.push(Date.now());
      window.localStorage.setItem(
        FREQUENCY_STORAGE_PREFIX + campaignId,
        JSON.stringify(entries.slice(-Math.max(cap, 1))),
      );
    } catch (_) {
      // If storage is unavailable, frequency capping cannot be enforced.
    }
  }

  function readScriptConfiguration(): ScriptConfiguration {
    let script: HTMLScriptElement | null = document.currentScript as HTMLScriptElement | null;
    if (!script || !script.src) {
      const scripts = document.querySelectorAll<HTMLScriptElement>('script[data-site-key]');
      script = scripts.length > 0 ? scripts[scripts.length - 1] : script;
    }

    const siteKey = script ? script.getAttribute('data-site-key') : null;
    const explicitApi = script ? script.getAttribute('data-api-base') || '' : '';
    let scriptOrigin = '';
    try {
      if (script && script.src) scriptOrigin = new URL(script.src).origin;
    } catch (_) {
      scriptOrigin = '';
    }
    const resolvedApi = normalizeOrigin(explicitApi) || scriptOrigin;

    let stylesheet = '';
    if (resolvedApi) {
      stylesheet = resolvedApi + '/sdk/v1/adcontrol.css';
    } else if (script && script.src) {
      try {
        stylesheet = new URL('adcontrol.css', script.src).toString();
      } catch (_) {
        stylesheet = '/sdk/v1/adcontrol.css';
      }
    } else {
      stylesheet = '/sdk/v1/adcontrol.css';
    }

    return {
      siteKey: siteKey && siteKey.trim() ? siteKey.trim() : null,
      apiBase: resolvedApi,
      stylesheetUrl: stylesheet,
      debug: Boolean(script && script.hasAttribute('data-debug')),
    };
  }

  function ensureStylesheet() {
    try {
      if (document.getElementById('adcontrol-sdk-styles')) return;
      const link = document.createElement('link');
      link.id = 'adcontrol-sdk-styles';
      link.rel = 'stylesheet';
      link.href = stylesheetUrl;
      // The SDK never blocks rendering on its stylesheet.
      (document.head || document.documentElement).appendChild(link);
    } catch (_) {
      // Unstyled ads still render; never break the host page.
    }
  }

  function isConfigFresh(): boolean {
    return Boolean(siteConfig) && Date.now() - configFetchedAt <= maxStaleMs;
  }

  function resolveClickUrl(raw: string): string | null {
    if (!raw || typeof raw !== 'string') return null;
    let url: URL;
    try {
      url = new URL(raw, apiBase || window.location.origin);
    } catch (_) {
      return null;
    }
    if (url.protocol !== 'https:' && url.protocol !== 'http:') return null;
    if (url.username || url.password) return null;
    // Only server-issued token paths are accepted. No query-string destination
    // is ever read or forwarded by the SDK.
    if (url.pathname.indexOf('/click/') !== 0 && url.pathname.indexOf('/r/') !== 0) return null;

    const expectedOrigin = normalizeOrigin(apiBase) || window.location.origin;
    if (expectedOrigin && url.origin !== expectedOrigin) return null;
    return url.toString();
  }

  function safeImageUrl(raw: string): string | null {
    if (!raw || typeof raw !== 'string') return null;
    try {
      const url = new URL(raw, window.location.href);
      if (url.protocol !== 'https:' && url.protocol !== 'http:') return null;
      if (url.username || url.password) return null;
      return url.toString();
    } catch (_) {
      return null;
    }
  }

  function minimizeReferrer(): string {
    if (!document.referrer) return '';
    try {
      return new URL(document.referrer).origin;
    } catch (_) {
      return '';
    }
  }

  function postEvent(payload: Record<string, unknown>, retry: boolean) {
    const endpoint = (apiBase || '') + '/delivery/v1/events';
    const body = JSON.stringify(payload);

    try {
      if (typeof navigator !== 'undefined' && typeof navigator.sendBeacon === 'function') {
        // text/plain makes this a CORS-safelisted request: no preflight while
        // the page is being unloaded. The API parses the body as JSON.
        const blob = new Blob([body], { type: 'text/plain;charset=UTF-8' });
        if (navigator.sendBeacon(endpoint, blob)) return;
      }
    } catch (_) {
      // Fall through to fetch.
    }

    const attempt = () => {
      fetch(endpoint, {
        method: 'POST',
        mode: 'cors',
        credentials: 'omit',
        cache: 'no-store',
        keepalive: true,
        headers: { 'Content-Type': 'text/plain;charset=UTF-8' },
        body,
      }).catch(() => {
        if (retry) {
          window.setTimeout(() => {
            fetch(endpoint, {
              method: 'POST',
              mode: 'cors',
              credentials: 'omit',
              cache: 'no-store',
              keepalive: true,
              headers: { 'Content-Type': 'text/plain;charset=UTF-8' },
              body,
            }).catch(() => {});
          }, 1_500);
        }
      });
    };
    attempt();
  }

  function sendImpression(placementKey: string, campaignId: string, eventId: string) {
    if (!activeSiteKey) return;
    const payload = {
      site_key: activeSiteKey,
      event_type: 'impression',
      event_id: eventId,
      placement_key: placementKey,
      campaign_id: campaignId,
      page_origin: window.location.origin,
      referrer: minimizeReferrer(),
    };
    try {
      postEvent(payload, true);
      log('Impression queued', { placementKey, campaignId, eventId });
    } catch (_) {
      // Analytics must never break the page.
    }
  }

  function clearWatch(element: Element) {
    const watch = impressionWatches.get(element);
    if (watch && watch.timer !== null) {
      window.clearTimeout(watch.timer);
      watch.timer = null;
    }
    watchedElements.delete(element);
    try {
      intersectionObserver?.unobserve(element);
    } catch (_) {
      // Ignore.
    }
  }

  function scheduleImpressionTimer(element: Element, watch: ImpressionWatch) {
    if (watch.timer !== null) {
      window.clearTimeout(watch.timer);
      watch.timer = null;
    }
    if (watch.done || watch.ratio < 0.5) return;
    if (typeof document !== 'undefined' && document.visibilityState === 'hidden') return;

    watch.timer = window.setTimeout(() => {
      watch.timer = null;
      if (watch.done || watch.ratio < 0.5) return;
      if (typeof document !== 'undefined' && document.visibilityState === 'hidden') return;
      watch.done = true;
      completedImpressions.set(element, watch.campaignId);
      recordFrequency(watch.campaignId, watch.frequencyCap, watch.frequencyWindowHours);
      sendImpression(watch.placementKey, watch.campaignId, watch.eventId);
      watchedElements.delete(element);
      try {
        intersectionObserver?.unobserve(element);
      } catch (_) {
        // Ignore.
      }
    }, IMPRESSION_MS);
  }

  function handleIntersections(entries: IntersectionObserverEntry[]) {
    for (const entry of entries) {
      const element = entry.target;
      const watch = impressionWatches.get(element);
      if (!watch || watch.done) {
        clearWatch(element);
        continue;
      }
      watch.ratio = entry.isIntersecting ? entry.intersectionRatio : 0;
      scheduleImpressionTimer(element, watch);
    }
  }

  function watchImpression(element: HTMLElement, placementKey: string, ad: PlacementConfig) {
    if (completedImpressions.get(element) === ad.campaign_id) return;

    const existingWatch = impressionWatches.get(element);
    if (existingWatch && existingWatch.campaignId === ad.campaign_id) {
      // A config refresh kept the same campaign. Preserve its in-flight timer
      // and its already-counted state so refreshes never inflate impressions.
      return;
    }
    if (existingWatch) clearWatch(element);

    if (typeof IntersectionObserver === 'undefined') {
      // Without the API we cannot establish 50%/1s visibility. Do not count a
      // view that may never have been displayed.
      log('IntersectionObserver unavailable; impression not counted');
      return;
    }

    const watch: ImpressionWatch = {
      campaignId: ad.campaign_id,
      placementKey,
      eventId: generateEventId(),
      ratio: 0,
      timer: null,
      done: false,
      frequencyCap: Number(ad.frequency_cap_impressions) || 0,
      frequencyWindowHours: Number(ad.frequency_cap_window_hours) || 24,
    };
    impressionWatches.set(element, watch);
    watchedElements.add(element);
    try {
      intersectionObserver?.observe(element);
    } catch (_) {
      watchedElements.delete(element);
    }
  }

  function getPlacementElements(): HTMLElement[] {
    try {
      return Array.from(document.querySelectorAll<HTMLElement>('[data-adcontrol-placement]'));
    } catch (_) {
      return [];
    }
  }

  function clearElementContents(element: HTMLElement) {
    clearWatch(element);
    try {
      element.replaceChildren();
    } catch (_) {
      while (element.firstChild) element.removeChild(element.firstChild);
    }
    element.removeAttribute('data-adcontrol-campaign');
    element.removeAttribute('data-adcontrol-state');
    renderedSignatures.delete(element);
    fallbackSignatures.delete(element);
    renderedElements.delete(element);
    completedImpressions.delete(element);
    impressionWatches.delete(element);
  }

  function clearAllRenderedAds() {
    for (const element of Array.from(renderedElements)) {
      clearElementContents(element);
    }
    renderedElements.clear();
    watchedElements.clear();
  }

  function findPlaceholder(element: HTMLElement): HTMLElement | null {
    for (const child of Array.from(element.children)) {
      if (child instanceof HTMLElement && child.classList.contains('adcontrol-placeholder')) {
        return child;
      }
    }
    return null;
  }

  function markEmpty(element: HTMLElement) {
    if (renderedElements.has(element)) {
      clearElementContents(element);
    }
    const placeholder = findPlaceholder(element);
    if (placeholder) placeholder.remove();
    fallbackSignatures.delete(element);
    element.setAttribute('data-adcontrol-state', 'empty');
  }

  function applyFallback(element: HTMLElement, placementKey: string, ad: PlacementConfig) {
    const behavior = ad.fallback_behavior === 'blank' ? 'blank' : 'hide';
    const width = Number(ad.width) || 0;
    const height = Number(ad.height) || 0;
    const signature = [placementKey, behavior, width, height].join('\u001f');

    if (
      !renderedElements.has(element) &&
      fallbackSignatures.get(element) === signature &&
      element.getAttribute('data-adcontrol-state') === 'empty'
    ) {
      return;
    }

    if (renderedElements.has(element)) {
      clearElementContents(element);
    }
    clearWatch(element);
    element.removeAttribute('data-adcontrol-campaign');
    element.setAttribute('data-adcontrol-state', 'empty');

    const existing = findPlaceholder(element);
    if (behavior === 'blank') {
      let placeholder = existing;
      if (!placeholder) {
        placeholder = document.createElement('div');
        placeholder.className = 'adcontrol-placeholder';
        placeholder.setAttribute('data-adcontrol-fallback', 'blank');
        placeholder.setAttribute('aria-hidden', 'true');
        element.replaceChildren(placeholder);
      } else {
        placeholder.replaceChildren();
      }
      placeholder.style.maxWidth = '';
      placeholder.style.minHeight = '';
      if (width > 0 && width <= 4096) placeholder.style.maxWidth = width + 'px';
      if (height > 0 && height <= 4096) placeholder.style.minHeight = height + 'px';
    } else if (existing) {
      existing.remove();
    }

    fallbackSignatures.set(element, signature);
  }

  function signatureFor(ad: PlacementConfig): string {
    return [
      ad.campaign_id,
      ad.format || '',
      ad.placement_format || '',
      ad.title || '',
      ad.body || '',
      ad.image_url || '',
      ad.alt_text || '',
      ad.cta_label || '',
      ad.source_type || '',
      ad.affiliate ? 'affiliate' : '',
      ad.disclosure_text || '',
      ad.merchant_name || '',
      ad.price_text || '',
      ad.currency || '',
      String(ad.width || 0),
      String(ad.height || 0),
    ].join('\u001f');
  }

  function makeAnchor(clickUrl: string, className: string): HTMLAnchorElement {
    const link = document.createElement('a');
    link.className = className;
    link.href = clickUrl;
    link.target = '_blank';
    link.rel = 'noopener noreferrer';
    link.setAttribute('data-adcontrol-click', 'true');
    return link;
  }

  function addBadge(parent: HTMLElement, text: string, className: string) {
    const badge = document.createElement('span');
    badge.className = className;
    badge.textContent = text;
    parent.appendChild(badge);
  }

  function addBannerFallback(link: HTMLElement, ad: PlacementConfig) {
    const fallback = document.createElement('span');
    fallback.className = 'adcontrol-banner-fallback';
    const title = document.createElement('span');
    title.className = 'adcontrol-banner-fallback-title';
    title.textContent = ad.title || ad.cta_label || 'Sponsored offer';
    fallback.appendChild(title);
    if (ad.body) {
      const body = document.createElement('span');
      body.className = 'adcontrol-banner-fallback-body';
      body.textContent = ad.body;
      fallback.appendChild(body);
    }
    link.appendChild(fallback);
  }

  function createImage(ad: PlacementConfig, className: string, link: HTMLElement): HTMLImageElement | null {
    const url = safeImageUrl(ad.image_url);
    if (!url) return null;
    const image = document.createElement('img');
    image.className = className;
    image.src = url;
    image.alt = ad.alt_text || ad.title || 'Advertisement';
    image.loading = 'lazy';
    image.decoding = 'async';
    image.referrerPolicy = 'no-referrer';
    image.addEventListener(
      'error',
      () => {
        image.hidden = true;
        if (!link.querySelector('.adcontrol-banner-fallback')) {
          addBannerFallback(link, ad);
        }
      },
      { once: true },
    );
    return image;
  }

  function applyDimensions(wrapper: HTMLElement, ad: PlacementConfig) {
    const width = Number(ad.width) || 0;
    const height = Number(ad.height) || 0;
    if (width > 0 && width <= 4096) {
      wrapper.style.maxWidth = width + 'px';
    }
    if (width > 0 && height > 0 && width <= 4096 && height <= 4096) {
      wrapper.style.setProperty('--adcontrol-aspect', width + ' / ' + height);
    }
  }

  function renderBanner(wrapper: HTMLElement, ad: PlacementConfig, clickUrl: string) {
    const link = makeAnchor(clickUrl, 'adcontrol-banner');
    const label = ad.title || ad.alt_text || 'Sponsored offer';
    link.setAttribute('aria-label', label + ' (advertisement)');

    const image = createImage(ad, 'adcontrol-banner-img', link);
    if (image) {
      link.appendChild(image);
    } else {
      addBannerFallback(link, ad);
    }
    addBadge(link, 'Ad', 'adcontrol-badge');
    wrapper.appendChild(link);
  }

  function renderNative(wrapper: HTMLElement, ad: PlacementConfig, clickUrl: string) {
    const card = makeAnchor(clickUrl, 'adcontrol-card');
    if (ad.image_url) {
      const media = document.createElement('span');
      media.className = 'adcontrol-native-media';
      const image = createImage(ad, 'adcontrol-native-img', card);
      if (image) {
        media.appendChild(image);
        addBadge(media, 'Sponsored', 'adcontrol-badge');
        card.appendChild(media);
      }
    }

    const body = document.createElement('span');
    body.className = 'adcontrol-native-body';

    if (ad.title) {
      const title = document.createElement('span');
      title.className = 'adcontrol-native-title';
      title.textContent = ad.title;
      body.appendChild(title);
    }
    if (ad.body) {
      const description = document.createElement('span');
      description.className = 'adcontrol-native-desc';
      description.textContent = ad.body;
      body.appendChild(description);
    }
    const cta = document.createElement('span');
    cta.className = 'adcontrol-native-cta';
    cta.textContent = ad.cta_label || 'Learn more';
    body.appendChild(cta);
    card.appendChild(body);
    wrapper.appendChild(card);
  }

  function renderProductCard(wrapper: HTMLElement, ad: PlacementConfig, clickUrl: string) {
    const card = document.createElement('article');
    card.className = 'adcontrol-product-card';
    if (ad.affiliate) card.setAttribute('data-adcontrol-affiliate', 'true');

    const link = makeAnchor(clickUrl, 'adcontrol-product-link');
    link.setAttribute('aria-label', (ad.title || 'Sponsored product') + ' (advertisement)');

    const media = document.createElement('span');
    media.className = 'adcontrol-product-media';
    const image = createImage(ad, 'adcontrol-product-img', link);
    if (image) media.appendChild(image);
    addBadge(media, ad.affiliate ? 'Affiliate' : 'Ad', 'adcontrol-badge');
    link.appendChild(media);

    const body = document.createElement('span');
    body.className = 'adcontrol-product-body';

    if (ad.merchant_name) {
      const merchant = document.createElement('span');
      merchant.className = 'adcontrol-product-merchant';
      merchant.textContent = ad.merchant_name;
      body.appendChild(merchant);
    }
    if (ad.title) {
      const title = document.createElement('span');
      title.className = 'adcontrol-product-title';
      title.textContent = ad.title;
      body.appendChild(title);
    }
    if (ad.body) {
      const description = document.createElement('span');
      description.className = 'adcontrol-product-desc';
      description.textContent = ad.body;
      body.appendChild(description);
    }
    if (ad.price_text) {
      const price = document.createElement('span');
      price.className = 'adcontrol-product-price';
      // price_text is the operator-entered display string and normally already
      // includes its currency. currency is retained as structured metadata.
      price.textContent = ad.price_text;
      body.appendChild(price);
    }
    const cta = document.createElement('span');
    cta.className = 'adcontrol-product-cta';
    cta.textContent = ad.cta_label || 'View offer';
    body.appendChild(cta);
    link.appendChild(body);
    card.appendChild(link);

    // Disclosure is deliberately outside the anchor: it is always visible,
    // cannot be hidden behind the link target, and applies to the card as a
    // whole. Affiliate cards never render without disclosure text.
    if (ad.affiliate || ad.disclosure_text) {
      const disclosure = document.createElement('p');
      disclosure.className = 'adcontrol-disclosure';
      disclosure.setAttribute('role', 'note');
      disclosure.textContent = ad.disclosure_text || DEFAULT_AFFILIATE_DISCLOSURE;
      card.appendChild(disclosure);
    }

    wrapper.appendChild(card);
  }

  function renderTextLink(wrapper: HTMLElement, ad: PlacementConfig, clickUrl: string) {
    const link = makeAnchor(clickUrl, 'adcontrol-text-link');
    addBadge(link, 'Ad', 'adcontrol-text-badge');
    const text = document.createElement('span');
    text.className = 'adcontrol-text-label';
    text.textContent = ad.title || ad.cta_label || 'Special offer';
    link.appendChild(text);
    wrapper.appendChild(link);
  }

  function renderAd(element: HTMLElement, placementKey: string, ad: PlacementConfig): boolean {
    const clickUrl = resolveClickUrl(ad.click_url);
    if (!clickUrl) {
      log('Skipped placement with an invalid or missing signed click URL', placementKey);
      markEmpty(element);
      return false;
    }

    try {
      clearWatch(element);
      element.replaceChildren();
      fallbackSignatures.delete(element);
      impressionWatches.delete(element);

      const wrapper = document.createElement('div');
      wrapper.className = 'adcontrol-root';
      applyDimensions(wrapper, ad);

      const format: PlacementFormat = ad.format || ad.placement_format;
      if (format === 'image' || format === 'responsive_banner') {
        renderBanner(wrapper, ad, clickUrl);
      } else if (format === 'text_link') {
        renderTextLink(wrapper, ad, clickUrl);
      } else if (format === 'product_card') {
        renderProductCard(wrapper, ad, clickUrl);
      } else {
        renderNative(wrapper, ad, clickUrl);
      }

      element.appendChild(wrapper);
      element.setAttribute('data-adcontrol-campaign', ad.campaign_id);
      element.setAttribute('data-adcontrol-state', 'filled');
      renderedElements.add(element);
      renderedSignatures.set(element, signatureFor(ad));
      watchImpression(element, placementKey, ad);
      return true;
    } catch (err) {
      log('Failed to render placement', placementKey, err);
      markEmpty(element);
      return false;
    }
  }

  function updateClickUrl(element: HTMLElement, ad: PlacementConfig) {
    const anchor = element.querySelector<HTMLAnchorElement>('a[data-adcontrol-click]');
    if (!anchor) return;
    const clickUrl = resolveClickUrl(ad.click_url);
    if (clickUrl && anchor.href !== clickUrl) {
      anchor.href = clickUrl;
    }
  }

  function scanPass(): void {
    if (stopped || !activeSiteKey) return;
    if (!siteConfig) return;
    if (!isConfigFresh()) {
      log('Delivery config exceeded its maximum stale window');
      clearAllRenderedAds();
      return;
    }

    ensureStylesheet();
    const elements = getPlacementElements();
    const seen = new Set<HTMLElement>();

    for (const element of elements) {
      seen.add(element);
      const placementKey = (element.getAttribute('data-adcontrol-placement') || '').trim();
      if (!placementKey) {
        markEmpty(element);
        continue;
      }

      const ad = siteConfig.placements[placementKey];
      if (!ad) {
        // Unknown or inactive placement: never render or reserve space.
        markEmpty(element);
        continue;
      }
      if (!ad.campaign_id) {
        // The placement is active but has no eligible campaign. Honor its
        // configured fallback rather than guessing from the host markup.
        applyFallback(element, placementKey, ad);
        continue;
      }
      if (frequencyCapReached(ad)) {
        log('Frequency cap reached for campaign', ad.campaign_id);
        applyFallback(element, placementKey, ad);
        continue;
      }

      const signature = signatureFor(ad);
      if (renderedElements.has(element) && renderedSignatures.get(element) === signature) {
        // Same campaign and creative: keep the DOM stable, refresh only the
        // short-lived signed click URL, and never re-count the impression.
        updateClickUrl(element, ad);
        continue;
      }

      renderAd(element, placementKey, ad);
    }

    // Placement elements removed from the DOM or whose attribute was removed
    // should not leave stale clickable ads behind.
    for (const element of Array.from(renderedElements)) {
      if (!seen.has(element) || !element.isConnected) {
        clearElementContents(element);
      }
    }
  }

  async function runScan(): Promise<void> {
    if (stopped) return;
    if (scanRunning) {
      scanAgain = true;
      return;
    }
    scanRunning = true;
    try {
      do {
        scanAgain = false;
        if ((!siteConfig || !isConfigFresh()) && activeSiteKey) {
          await fetchConfig();
        }
        scanPass();
      } while (scanAgain && !stopped);
    } catch (err) {
      log('Scan failed', err);
    } finally {
      scanRunning = false;
    }
  }

  function scheduleScan() {
    if (stopped) return;
    if (scanTimer !== null) window.clearTimeout(scanTimer);
    scanTimer = window.setTimeout(() => {
      scanTimer = null;
      void runScan();
    }, 150);
  }

  function handleFetchFailure(status: number): void {
    if (status === 403 || status === 404 || status === 410) {
      // A disabled site, rotated key, or unauthorized origin must take effect
      // immediately; never leave previously rendered ads clickable.
      siteConfig = null;
      configFetchedAt = 0;
      configEtag = null;
      clearAllRenderedAds();
      return;
    }
    if (!isConfigFresh()) {
      clearAllRenderedAds();
    }
  }

  async function fetchConfig(): Promise<SiteDeliveryConfig | null> {
    if (!activeSiteKey) return null;
    if (fetchInFlight) return fetchInFlight;

    const controller = typeof AbortController !== 'undefined' ? new AbortController() : null;
    const timeout =
      controller !== null
        ? window.setTimeout(() => {
            try {
              controller.abort();
            } catch (_) {
              // Ignore.
            }
          }, FETCH_TIMEOUT_MS)
        : null;

    const headers: Record<string, string> = { Accept: 'application/json' };
    if (configEtag) headers['If-None-Match'] = configEtag;

    const endpoint = (apiBase || '') + '/delivery/v1/sites/' +
      encodeURIComponent(activeSiteKey) + '/config' + deliveryContextParams();

    const task = (async (): Promise<SiteDeliveryConfig | null> => {
      try {
        const response = await fetch(endpoint, {
          method: 'GET',
          mode: 'cors',
          credentials: 'omit',
          cache: 'no-cache',
          headers,
          signal: controller ? controller.signal : undefined,
        });

        if (response.status === 304) {
          configFetchedAt = Date.now();
          return siteConfig;
        }

        if (!response.ok) {
          log('Config request failed', response.status);
          handleFetchFailure(response.status);
          return isConfigFresh() ? siteConfig : null;
        }

        const data = (await response.json()) as SiteDeliveryConfig;
        if (
          !data ||
          typeof data !== 'object' ||
          typeof data.site_key !== 'string' ||
          typeof data.config_version !== 'number' ||
          !data.placements ||
          typeof data.placements !== 'object'
        ) {
          throw new Error('Malformed delivery configuration');
        }

        siteConfig = data;
        configFetchedAt = Date.now();
        configEtag = response.headers.get('ETag');

        const nextRefresh = Number(data.refresh_interval_seconds) * 1000;
        if (Number.isFinite(nextRefresh) && nextRefresh > 0) {
          refreshIntervalMs = clampNumber(nextRefresh, MIN_REFRESH_MS, MAX_REFRESH_MS, DEFAULT_REFRESH_MS);
        }
        const nextMaxStale = Number(data.max_stale_seconds) * 1000;
        if (Number.isFinite(nextMaxStale) && nextMaxStale > 0) {
          maxStaleMs = clampNumber(nextMaxStale, 30_000, 3_600_000, DEFAULT_MAX_STALE_MS);
        }

        log('Loaded config v' + data.config_version + ' for ' + data.site_key);
        return data;
      } catch (err) {
        log('Config fetch error', err);
        if (!isConfigFresh()) clearAllRenderedAds();
        return isConfigFresh() ? siteConfig : null;
      } finally {
        if (timeout !== null) window.clearTimeout(timeout);
      }
    })();

    fetchInFlight = task;
    try {
      return await task;
    } finally {
      fetchInFlight = null;
    }
  }

  function refreshNow(): Promise<void> {
    if (stopped || !activeSiteKey) return Promise.resolve();
    // Keep the last good config and its ETag. A failed refresh can still use
    // the recent cache within max_stale_seconds; a successful one updates it.
    return fetchConfig()
      .then(() => runScan())
      .catch(() => {
        if (!isConfigFresh()) clearAllRenderedAds();
      });
  }

  function scheduleRefresh() {
    if (refreshTimer !== null) {
      window.clearTimeout(refreshTimer);
      refreshTimer = null;
    }
    if (stopped || !initialized) return;
    refreshTimer = window.setTimeout(() => {
      refreshTimer = null;
      void refreshNow().finally(() => scheduleRefresh());
    }, refreshIntervalMs);
  }

  function setupObservers() {
    if (typeof IntersectionObserver !== 'undefined' && !intersectionObserver) {
      try {
        intersectionObserver = new IntersectionObserver(handleIntersections, { threshold: [0, 0.5, 1] });
      } catch (_) {
        intersectionObserver = null;
      }
    }

    if (typeof MutationObserver !== 'undefined' && !mutationObserver) {
      try {
        mutationObserver = new MutationObserver(() => scheduleScan());
        mutationObserver.observe(document.documentElement, {
          childList: true,
          subtree: true,
          attributes: true,
          attributeFilter: ['data-adcontrol-placement'],
        });
      } catch (_) {
        mutationObserver = null;
      }
    }
  }

  function onVisibilityChange() {
    if (document.visibilityState === 'visible') {
      for (const element of Array.from(watchedElements)) {
        const watch = impressionWatches.get(element);
        if (watch && !watch.done) scheduleImpressionTimer(element, watch);
      }
      if (!siteConfig || Date.now() - configFetchedAt > refreshIntervalMs) {
        scheduleScan();
      }
    } else {
      for (const element of Array.from(watchedElements)) {
        const watch = impressionWatches.get(element);
        if (watch && watch.timer !== null) {
          window.clearTimeout(watch.timer);
          watch.timer = null;
        }
      }
    }
  }

  function onRouteChange() {
    scheduleScan();
  }

  function start() {
    if (!activeSiteKey || stopped === false) return;
    stopped = false;
    initialized = true;
    ensureStylesheet();
    setupObservers();
    document.addEventListener('visibilitychange', onVisibilityChange);
    window.addEventListener('popstate', onRouteChange);
    window.addEventListener('hashchange', onRouteChange);
    void runScan();
    scheduleRefresh();
  }

  function teardown(clearAds: boolean) {
    stopped = true;
    if (clearAds) clearAllRenderedAds();
    if (refreshTimer !== null) {
      window.clearTimeout(refreshTimer);
      refreshTimer = null;
    }
    if (scanTimer !== null) {
      window.clearTimeout(scanTimer);
      scanTimer = null;
    }
    if (mutationObserver) {
      try {
        mutationObserver.disconnect();
      } catch (_) {
        // Ignore.
      }
      mutationObserver = null;
    }
    if (intersectionObserver) {
      try {
        intersectionObserver.disconnect();
      } catch (_) {
        // Ignore.
      }
      intersectionObserver = null;
    }
    for (const element of Array.from(watchedElements)) {
      const watch = impressionWatches.get(element);
      if (watch && watch.timer !== null) window.clearTimeout(watch.timer);
    }
    watchedElements.clear();
    try {
      document.removeEventListener('visibilitychange', onVisibilityChange);
      window.removeEventListener('popstate', onRouteChange);
      window.removeEventListener('hashchange', onRouteChange);
    } catch (_) {
      // Ignore.
    }
  }

  const AdControl: AdControlPublic = {
    version: SDK_VERSION,

    init: function (options?: SDKOptions): AdControlPublic {
      try {
        const scriptConfig = readScriptConfiguration();
        const nextSiteKey = (options && options.siteKey) || scriptConfig.siteKey;
        if (!nextSiteKey) {
          log('No data-site-key on the script tag and no siteKey passed to init()');
          return AdControl;
        }

        const nextApiBase = normalizeOrigin((options && options.apiBase) || scriptConfig.apiBase);
        const siteChanged = nextSiteKey !== activeSiteKey || nextApiBase !== apiBase;
        debugMode = Boolean((options && options.debug) || scriptConfig.debug);

        if (initialized && !siteChanged) {
          void runScan();
          return AdControl;
        }

        if (!stopped) teardown(true);
        activeSiteKey = nextSiteKey;
        apiBase = nextApiBase;
        stylesheetUrl = scriptConfig.stylesheetUrl;
        if (nextApiBase) {
          stylesheetUrl = nextApiBase + '/sdk/v1/adcontrol.css';
        }
        siteConfig = null;
        configFetchedAt = 0;
        configEtag = null;
        refreshIntervalMs =
          options && options.refreshIntervalMs
            ? clampNumber(options.refreshIntervalMs, MIN_REFRESH_MS, MAX_REFRESH_MS, DEFAULT_REFRESH_MS)
            : DEFAULT_REFRESH_MS;
        maxStaleMs =
          options && options.maxStaleMs
            ? clampNumber(options.maxStaleMs, 30_000, 3_600_000, DEFAULT_MAX_STALE_MS)
            : DEFAULT_MAX_STALE_MS;

        const begin = () => {
          if (stopped) start();
        };
        if (document.readyState === 'loading') {
          document.addEventListener('DOMContentLoaded', begin, { once: true });
        } else {
          begin();
        }
      } catch (err) {
        log('Init failed', err);
      }
      return AdControl;
    },

    scan: function (): Promise<void> {
      try {
        if (!initialized) return Promise.resolve();
        return runScan();
      } catch (_) {
        return Promise.resolve();
      }
    },

    refresh: function (): Promise<void> {
      try {
        if (!initialized) return Promise.resolve();
        return refreshNow();
      } catch (_) {
        return Promise.resolve();
      }
    },

    destroy: function (): void {
      try {
        teardown(true);
        initialized = false;
        activeSiteKey = null;
        siteConfig = null;
        configFetchedAt = 0;
      } catch (_) {
        // Ignore.
      }
    },

    getVersion: function (): string {
      return SDK_VERSION;
    },
  };

  (win as any).AdControl = AdControl;

  // Auto-initialise when the deployment snippet provides a site key.
  try {
    const autoConfig = readScriptConfiguration();
    if (autoConfig.siteKey) {
      AdControl.init({ siteKey: autoConfig.siteKey, apiBase: autoConfig.apiBase, debug: autoConfig.debug });
    }
  } catch (err) {
    log('Auto-init failed', err);
  }
})();
