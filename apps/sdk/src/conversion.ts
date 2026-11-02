/**
 * AdControl Conversion & Attribution SDK
 *
 * A separate, fail-open snippet for advertiser landing pages and thank-you
 * pages. It captures first-party UTM/referrer attribution and reports
 * conversions to a configured conversion source. It does not render ads and
 * does not require the delivery SDK.
 */

(function () {
  'use strict';

  interface TrackOptions {
    eventName?: string;
    event_name?: string;
    externalId?: string;
    external_id?: string;
    orderId?: string;
    order_id?: string;
    value?: number | string;
    value_cents?: number;
    commission?: number | string;
    commission_cents?: number;
    currency?: string;
    campaignId?: string;
    campaign_id?: string;
    offerId?: string;
    offer_id?: string;
    notes?: string;
  }

  interface Touch {
    utm_source: string;
    utm_medium: string;
    utm_campaign: string;
    utm_term: string;
    utm_content: string;
    gclid: string;
    fbclid: string;
    msclkid: string;
    ttclid: string;
    landing_page: string;
    referrer: string;
    captured_at: string;
  }

  interface ConversionApi {
    track(options?: TrackOptions): Promise<boolean>;
    pixelUrl(options?: TrackOptions): string;
    getAttribution(): { first_touch: Touch | null; last_touch: Touch | null; visitor_id: string };
    getVisitorId(): string;
  }

  interface ScriptConfig {
    sourceKey: string;
    apiBase: string;
    debug: boolean;
  }

  const SOURCE_STORAGE_PREFIX = 'adcontrol_conv_first_v1:';
  const LAST_STORAGE_PREFIX = 'adcontrol_conv_last_v1:';
  const VISITOR_STORAGE_KEY = 'adcontrol_conv_visitor_v1';
  const VISITOR_TTL_MS = 180 * 24 * 60 * 60 * 1000;

  const win = window as unknown as Record<string, unknown> & Window;
  const existing = (win as unknown as { AdControlConversion?: ConversionApi }).AdControlConversion;
  if (existing && typeof existing.track === 'function') return;

  let debugMode = false;

  function log(message: string, ...args: unknown[]) {
    if (!debugMode) return;
    try {
      console.log('[AdControl Conversion]', message, ...args);
    } catch (_) {
      // Never break the advertiser page.
    }
  }

  function safeStorageGet(key: string): string {
    try {
      return window.localStorage.getItem(key) || '';
    } catch (_) {
      return '';
    }
  }

  function safeStorageSet(key: string, value: string) {
    try {
      window.localStorage.setItem(key, value);
    } catch (_) {
      // Storage may be unavailable in private or restricted contexts.
    }
  }

  function parseJson<T>(raw: string): T | null {
    if (!raw) return null;
    try {
      return JSON.parse(raw) as T;
    } catch (_) {
      return null;
    }
  }

  function normalizeOrigin(value: string): string {
    if (!value) return '';
    try {
      return new URL(value, window.location.href).origin;
    } catch (_) {
      return '';
    }
  }

  function randomId(prefix: string): string {
    try {
      if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') {
        return prefix + crypto.randomUUID();
      }
      if (typeof crypto !== 'undefined' && typeof crypto.getRandomValues === 'function') {
        const bytes = new Uint8Array(16);
        crypto.getRandomValues(bytes);
        let value = '';
        for (let index = 0; index < bytes.length; index += 1) {
          value += bytes[index].toString(16).padStart(2, '0');
        }
        return prefix + value;
      }
    } catch (_) {
      // Fall through.
    }
    return prefix + Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 14);
  }

  function scriptConfig(): ScriptConfig {
    let script: HTMLScriptElement | null = document.currentScript as HTMLScriptElement | null;
    if (!script || !script.src) {
      const scripts = document.querySelectorAll<HTMLScriptElement>('script[data-source-key]');
      script = scripts.length > 0 ? scripts[scripts.length - 1] : script;
    }
    const sourceKey = script ? (script.getAttribute('data-source-key') || '').trim() : '';
    let scriptOrigin = '';
    try {
      if (script && script.src) scriptOrigin = new URL(script.src).origin;
    } catch (_) {
      scriptOrigin = '';
    }
    const explicitBase = script ? script.getAttribute('data-api-base') || '' : '';
    return {
      sourceKey,
      apiBase: normalizeOrigin(explicitBase) || scriptOrigin,
      debug: Boolean(script && script.hasAttribute('data-debug')),
    };
  }

  function currentTouch(): Touch {
    let params: URLSearchParams;
    try {
      params = new URLSearchParams(window.location.search);
    } catch (_) {
      params = new URLSearchParams();
    }
    const value = (key: string) => {
      try {
        return (params.get(key) || '').trim().slice(0, 160);
      } catch (_) {
        return '';
      }
    };
    let referrer = '';
    if (document.referrer) {
      try {
        const ref = new URL(document.referrer);
        if (ref.origin !== window.location.origin) referrer = ref.origin;
      } catch (_) {
        referrer = '';
      }
    }
    return {
      utm_source: value('utm_source'),
      utm_medium: value('utm_medium'),
      utm_campaign: value('utm_campaign'),
      utm_term: value('utm_term'),
      utm_content: value('utm_content'),
      gclid: value('gclid'),
      fbclid: value('fbclid'),
      msclkid: value('msclkid'),
      ttclid: value('ttclid'),
      landing_page: window.location.href.slice(0, 2048),
      referrer,
      captured_at: new Date().toISOString(),
    };
  }

  function getVisitorId(): string {
    const raw = safeStorageGet(VISITOR_STORAGE_KEY);
    const stored = parseJson<{ id?: string; created_at?: string }>(raw);
    if (stored && typeof stored.id === 'string' && stored.id.length >= 8) {
      const createdAt = stored.created_at ? Date.parse(stored.created_at) : 0;
      if (Number.isFinite(createdAt) && Date.now() - createdAt < VISITOR_TTL_MS) {
        return stored.id;
      }
    }
    const id = randomId('acv_');
    safeStorageSet(VISITOR_STORAGE_KEY, JSON.stringify({ id, created_at: new Date().toISOString() }));
    return id;
  }

  function readTouch(prefix: string, sourceKey: string): Touch | null {
    const key = prefix + sourceKey;
    const parsed = parseJson<Touch>(safeStorageGet(key));
    if (!parsed || typeof parsed.captured_at !== 'string') return null;
    const capturedAt = Date.parse(parsed.captured_at);
    if (!Number.isFinite(capturedAt) || Date.now() - capturedAt > VISITOR_TTL_MS) return null;
    return parsed;
  }

  function getAttribution(): { first_touch: Touch | null; last_touch: Touch | null; visitor_id: string } {
    const config = scriptConfig();
    const sourceKey = config.sourceKey;
    if (!sourceKey) return { first_touch: null, last_touch: null, visitor_id: '' };
    const visitorId = getVisitorId();
    const current = currentTouch();
    let first = readTouch(SOURCE_STORAGE_PREFIX, sourceKey);
    if (!first) {
      first = current;
      safeStorageSet(SOURCE_STORAGE_PREFIX + sourceKey, JSON.stringify(first));
    }
    // Every page view is the latest touch, including direct visits. This keeps
    // last-touch attribution honest instead of ignoring direct traffic.
    safeStorageSet(LAST_STORAGE_PREFIX + sourceKey, JSON.stringify(current));
    return { first_touch: first, last_touch: current, visitor_id: visitorId };
  }

  function cents(value: number): number {
    if (!Number.isFinite(value) || value < 0) return 0;
    return Math.round(value * 100);
  }

  function normalizePayload(options: TrackOptions): { sourceKey: string; apiBase: string; payload: Record<string, unknown> } {
    const config = scriptConfig();
    const attribution = getAttribution();
    const last = attribution.last_touch;
    const eventName = String(options.eventName || options.event_name || 'purchase').trim().toLowerCase();
    const externalId = String(
      options.externalId || options.external_id || options.orderId || options.order_id || '',
    ).trim();
    const valueCents =
      typeof options.value_cents === 'number'
        ? Math.max(0, Math.round(options.value_cents))
        : cents(Number(options.value || 0));
    const commissionCents =
      typeof options.commission_cents === 'number'
        ? Math.max(0, Math.round(options.commission_cents))
        : cents(Number(options.commission || 0));
    const payload: Record<string, unknown> = {
      source_key: config.sourceKey,
      event_name: eventName || 'purchase',
      external_id: externalId || randomId('acv-'),
      value_cents: valueCents,
      commission_cents: commissionCents,
      currency: String(options.currency || 'USD').toUpperCase().slice(0, 3),
      campaign_id: String(options.campaignId || options.campaign_id || ''),
      offer_id: String(options.offerId || options.offer_id || ''),
      visitor_id: attribution.visitor_id,
      page_url: window.location.href.slice(0, 2048),
      referrer: document.referrer.slice(0, 2048),
      first_touch: attribution.first_touch || {},
      last_touch: last || {},
      occurred_at: new Date().toISOString(),
    };
    if (last) {
      payload.utm_source = last.utm_source;
      payload.utm_medium = last.utm_medium;
      payload.utm_campaign = last.utm_campaign;
      payload.utm_term = last.utm_term;
      payload.utm_content = last.utm_content;
    }
    if (options.notes) payload.notes = String(options.notes).slice(0, 1000);
    return { sourceKey: config.sourceKey, apiBase: config.apiBase, payload };
  }

  function track(options?: TrackOptions): Promise<boolean> {
    try {
      const normalized = normalizePayload(options || {});
      if (!normalized.sourceKey) {
        log('Missing data-source-key on the conversion script tag');
        return Promise.resolve(false);
      }
      const endpoint = (normalized.apiBase || '') + '/conversion/v1/events';
      const body = JSON.stringify(normalized.payload);
      const request = fetch(endpoint, {
        method: 'POST',
        mode: 'cors',
        credentials: 'omit',
        cache: 'no-store',
        keepalive: true,
        headers: { 'Content-Type': 'text/plain;charset=UTF-8' },
        body,
      })
        .then((response) => response.ok)
        .catch(() => false);
      log('Conversion queued', normalized.payload);
      return request;
    } catch (error) {
      log('Conversion tracking failed', error);
      return Promise.resolve(false);
    }
  }

  function pixelUrl(options?: TrackOptions): string {
    try {
      const config = scriptConfig();
      if (!config.sourceKey) return '';
      const resolved = normalizePayload(options || {});
      const params = new URLSearchParams();
      const add = (key: string, value: unknown) => {
        const text = String(value == null ? '' : value);
        if (text) params.set(key, text);
      };
      add('source_key', config.sourceKey);
      add('event_name', resolved.payload.event_name);
      add('external_id', resolved.payload.external_id);
      add('value_cents', resolved.payload.value_cents);
      add('commission_cents', resolved.payload.commission_cents);
      add('currency', resolved.payload.currency);
      add('campaign_id', resolved.payload.campaign_id);
      add('offer_id', resolved.payload.offer_id);
      add('visitor_id', resolved.payload.visitor_id);
      add('page_url', resolved.payload.page_url);
      add('referrer', resolved.payload.referrer);
      add('utm_source', resolved.payload.utm_source);
      add('utm_medium', resolved.payload.utm_medium);
      add('utm_campaign', resolved.payload.utm_campaign);
      add('utm_term', resolved.payload.utm_term);
      add('utm_content', resolved.payload.utm_content);
      return (config.apiBase || '') + '/conversion/v1/pixel.gif?' + params.toString();
    } catch (_) {
      return '';
    }
  }

  const api: ConversionApi = {
    track,
    pixelUrl,
    getAttribution,
    getVisitorId,
  };

  (win as unknown as { AdControlConversion: ConversionApi }).AdControlConversion = api;

  // Capture attribution as early as possible so first/last touch is available
  // even if the page later navigates or the conversion call happens elsewhere.
  try {
    const config = scriptConfig();
    debugMode = config.debug;
    if (config.sourceKey) getAttribution();
  } catch (error) {
    log('Attribution capture failed', error);
  }
})();
