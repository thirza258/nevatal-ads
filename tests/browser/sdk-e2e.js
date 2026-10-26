'use strict';

const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const ROOT = process.env.ADCONTROL_ROOT || path.resolve(__dirname, '..', '..');
const IDS_FILE = process.env.ADCONTROL_IDS_FILE || '/tmp/adcontrol_stack_ids';
let pass = 0;
let fail = 0;

function check(label, condition) {
  if (condition) {
    pass += 1;
    console.log('PASS  ' + label);
  } else {
    fail += 1;
    console.log('FAIL  ' + label);
  }
}

function readEnv() {
  const env = {};
  const envFile = process.env.ADCONTROL_ENV_FILE || path.join(ROOT, 'deploy', '.env');
  if (!fs.existsSync(envFile)) {
    if (process.env.ADMIN_EMAIL && process.env.ADMIN_PASSWORD) return env;
    throw new Error('Missing ' + envFile + '; set ADMIN_EMAIL and ADMIN_PASSWORD to run without it');
  }
  for (const line of fs.readFileSync(envFile, 'utf8').split(/\r?\n/)) {
    if (!line || line.startsWith('#')) continue;
    const idx = line.indexOf('=');
    if (idx > 0) env[line.slice(0, idx)] = line.slice(idx + 1);
  }
  return env;
}

function baseUrl(env) {
  if (process.env.ADCONTROL_BASE) {
    return process.env.ADCONTROL_BASE.replace(/\/+$/, '');
  }
  const port = process.env.ADCONTROL_TEST_PORT || env.HTTP_PORT || '18080';
  return 'http://127.0.0.1:' + port;
}

async function main() {
  const env = readEnv();
  const BASE = baseUrl(env);
  const ids = fs.readFileSync(IDS_FILE, 'utf8').trim().split('\n');
  const siteId = ids[0];
  const siteKey = ids[1];
  const headerCampaignId = ids[2];

  const loginRes = await fetch(BASE + '/api/v1/auth/login', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      email: process.env.ADMIN_EMAIL || env.ADMIN_EMAIL,
      password: process.env.ADMIN_PASSWORD || env.ADMIN_PASSWORD,
    }),
  });
  if (!loginRes.ok) throw new Error('login failed: ' + loginRes.status);
  const login = await loginRes.json();
  const auth = { Authorization: 'Bearer ' + login.token, 'Content-Type': 'application/json' };

  const suffix = Date.now().toString(36);
  const placementKey = 'in-article-' + suffix;
  const hidePlacementKey = 'fallback-hide-' + suffix;

  async function apiAs(authHeaders, path, opts) {
    const res = await fetch(BASE + path, Object.assign({ headers: authHeaders }, opts));
    const text = await res.text();
    if (!res.ok) throw new Error(path + ' failed: ' + res.status + ' ' + text);
    return text ? JSON.parse(text) : null;
  }

  async function api(path, opts) {
    return apiAs(auth, path, opts);
  }

  const placement = await api('/api/v1/placements', {
    method: 'POST',
    body: JSON.stringify({
      site_id: siteId,
      placement_key: placementKey,
      name: 'SPA article placement',
      format: 'native_card',
      width: 0,
      height: 0,
      fallback_behavior: 'blank',
      status: 'active',
    }),
  });
  const campaign = await api('/api/v1/campaigns', {
    method: 'POST',
    body: JSON.stringify({
      site_id: siteId,
      placement_id: placement.id,
      name: 'SPA test campaign',
      destination_url: 'https://example.com/from-spa',
      format: 'native_card',
      title: 'Dynamically inserted advertisement',
      body: 'This creative was attached to a placement added after page load.',
      image_url: '',
      alt_text: 'Dynamically inserted advertisement',
      cta_label: 'Open',
      status: 'draft',
      priority: 1,
      weight: 100,
    }),
  });
  await api('/api/v1/campaigns/' + campaign.id + '/publish', { method: 'POST' });
  await api('/api/v1/placements', {
    method: 'POST',
    body: JSON.stringify({
      site_id: siteId,
      placement_key: hidePlacementKey,
      name: 'Hide fallback placement',
      format: 'responsive_banner',
      width: 640,
      height: 120,
      fallback_behavior: 'hide',
      status: 'active',
    }),
  });

  async function summary() {
    return api('/api/v1/analytics/summary?site_id=' + encodeURIComponent(siteId));
  }

  const before = await summary();

  // Phase 2 fixture: provision a publisher, verify an owned site, and place a
  // Shopee offer as a product card. The browser checks below ensure the SDK
  // renders the disclosure and links only through the first-party /r/ route.
  const publisherEmail = 'affiliate-e2e-' + suffix + '@browser.test';
  const publisherPassword = 'e2e-affiliate-password-' + suffix;
  await api('/api/v1/publishers', {
    method: 'POST',
    body: JSON.stringify({
      name: 'Browser Affiliate Publisher',
      owner_email: publisherEmail,
      password: publisherPassword,
      status: 'active',
      contact_email: publisherEmail,
      timezone: 'UTC',
    }),
  });
  const publisherLoginRes = await fetch(BASE + '/api/v1/auth/login', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ email: publisherEmail, password: publisherPassword }),
  });
  if (!publisherLoginRes.ok) throw new Error('publisher login failed: ' + publisherLoginRes.status);
  const publisherLogin = await publisherLoginRes.json();
  const publisherAuth = {
    Authorization: 'Bearer ' + publisherLogin.token,
    'Content-Type': 'application/json',
  };
  const pubApi = (path, opts) => apiAs(publisherAuth, path, opts);

  const productSite = await pubApi('/api/v1/sites', {
    method: 'POST',
    body: JSON.stringify({
      name: 'Browser Product Site',
      primary_domain: 'products.browser.test',
      allowed_domains: ['products.browser.test', BASE.replace(/^https?:\/\//, '')],
      default_disclosure_text: 'Affiliate disclosure: this site may earn a commission.',
      status: 'active',
    }),
  });
  await api('/api/v1/sites/' + productSite.id + '/verify', {
    method: 'POST',
    body: JSON.stringify({ method: 'manual' }),
  });
  const productPlacementKey = 'product-' + suffix;
  const productPlacement = await pubApi('/api/v1/placements', {
    method: 'POST',
    body: JSON.stringify({
      site_id: productSite.id,
      placement_key: productPlacementKey,
      name: 'Browser product card',
      format: 'product_card',
      width: 0,
      height: 0,
      fallback_behavior: 'hide',
      allowed_source_types: ['affiliate'],
      max_items: 1,
      disclosure_mode: 'required',
      status: 'active',
    }),
  });
  const affiliateUrl =
    'https://shopee.example.com/product/9001/9002?smtt=0.123&affiliate_id=ABC&plus=1%2B2&empty=';
  const offer = await pubApi('/api/v1/affiliate-offers', {
    method: 'POST',
    body: JSON.stringify({
      provider: 'shopee_affiliate',
      offer_name: 'Browser Shopee product',
      product_title: 'Browser Test Coffee Grinder',
      product_image_url: 'https://images.example.com/browser-product.jpg',
      product_description: 'A product card rendered by the AdControl browser SDK.',
      merchant_name: 'Shopee Test Merchant',
      affiliate_url: affiliateUrl,
      canonical_product_url: 'https://shopee.example.com/product/9001',
      price_text: 'IDR 249.000',
      currency: 'IDR',
      availability_checked_at: '',
      disclosure_text: 'Affiliate disclosure: we may earn a commission from this product.',
      status: 'draft',
    }),
  });
  await pubApi('/api/v1/affiliate-offers/' + offer.id + '/validate', { method: 'POST' });
  await pubApi('/api/v1/affiliate-offers/' + offer.id + '/publish', { method: 'POST' });
  const productCampaign = await pubApi('/api/v1/affiliate-offers/' + offer.id + '/placements', {
    method: 'POST',
    body: JSON.stringify({
      placement_id: productPlacement.id,
      name: 'Browser product card campaign',
      status: 'draft',
      starts_at: '',
      ends_at: '',
      priority: 1,
      weight: 100,
    }),
  });
  await pubApi('/api/v1/campaigns/' + productCampaign.id + '/publish', { method: 'POST' });

  const browser = await chromium.launch({ headless: true });
  const context = await browser.newContext({ viewport: { width: 1280, height: 900 }, locale: 'en-US' });
  const page = await context.newPage();
  const cspViolations = [];
  const pageErrors = [];
  page.on('console', (msg) => {
    const text = msg.text();
    if (/Content Security Policy|Refused to/i.test(text)) cspViolations.push(text);
  });
  page.on('pageerror', (err) => pageErrors.push(String(err)));
  await page.route('https://images.example.com/**', (route) => route.abort());

  const demoUrl = BASE + '/demo/?' + new URLSearchParams({
    site_key: siteKey,
    placements: 'header-banner',
  }).toString();
  await page.goto(demoUrl, { waitUntil: 'domcontentloaded', timeout: 30000 });

  const headerSelector = '[data-adcontrol-placement="header-banner"]';
  await page.waitForSelector(headerSelector + '[data-adcontrol-state="filled"]', { timeout: 20000 });
  check('SDK renders the hosted demo placement', true);

  const headerInfo = await page.$eval(headerSelector, (el) => {
    const link = el.querySelector('a[data-adcontrol-click]');
    return {
      text: el.textContent || '',
      href: link ? link.href : '',
      rel: link ? link.getAttribute('rel') : '',
      target: link ? link.getAttribute('target') : '',
      aria: link ? link.getAttribute('aria-label') : '',
      state: el.getAttribute('data-adcontrol-state'),
      campaign: el.getAttribute('data-adcontrol-campaign'),
      stylesheet: !!document.getElementById('adcontrol-sdk-styles'),
      cookie: document.cookie,
      localStorageEntries: localStorage.length,
      sessionStorageEntries: sessionStorage.length,
    };
  });
  check('Rendered ad carries the required Ad label', /Ad/.test(headerInfo.text));
  check('Rendered ad exposes a signed click URL on the API origin', headerInfo.href.indexOf(BASE + '/click/') === 0);
  check('Click link opens safely in a new tab', headerInfo.rel === 'noopener noreferrer' && headerInfo.target === '_blank');
  check('Click link has an accessible advertisement label', /advertisement/i.test(headerInfo.aria));
  check('SDK records its state attribute', headerInfo.state === 'filled');
  check('SDK loads external CSS', headerInfo.stylesheet);
  check('SDK writes no cookies', headerInfo.cookie === '');
  check('SDK writes no localStorage identifiers', headerInfo.localStorageEntries === 0);
  check('SDK writes no sessionStorage identifiers', headerInfo.sessionStorageEntries === 0);

  await page.$eval(headerSelector, (el) => el.scrollIntoView({ block: 'center' }));
  await page.waitForTimeout(2500);
  const afterHeader = await summary();
  check('Impression is counted after 50% visibility for one second', Number(afterHeader.impressions) === Number(before.impressions) + 1);

  // Insert a placement after page load, far below the fold, to exercise the SPA scanner.
  await page.evaluate((key) => {
    const spacer = document.createElement('div');
    spacer.id = 'spa-spacer';
    spacer.style.height = '2400px';
    spacer.textContent = 'Spacer content';
    document.body.appendChild(spacer);
    const host = document.createElement('div');
    host.id = 'spa-host';
    const ad = document.createElement('div');
    ad.setAttribute('data-adcontrol-placement', key);
    host.appendChild(ad);
    document.body.appendChild(host);
  }, placementKey);

  const spaSelector = '[data-adcontrol-placement="' + placementKey + '"]';
  await page.waitForSelector(spaSelector + '[data-adcontrol-state="filled"]', { timeout: 20000 });
  check('MutationObserver renders a placement inserted after load', true);
  const offscreen = await page.$eval(spaSelector, (el) => {
    const rect = el.getBoundingClientRect();
    return { top: rect.top, visible: rect.top < window.innerHeight && rect.bottom > 0 };
  });
  check('Dynamically inserted ad starts offscreen', offscreen.visible === false);

  await page.waitForTimeout(2500);
  const afterOffscreen = await summary();
  check('Offscreen ad is not counted as an impression', Number(afterOffscreen.impressions) === Number(afterHeader.impressions));

  await page.$eval(spaSelector, (el) => el.scrollIntoView({ block: 'center' }));
  await page.waitForTimeout(2500);
  const afterSpa = await summary();
  check('SPA-inserted ad counts one impression when visible', Number(afterSpa.impressions) === Number(afterOffscreen.impressions) + 1);

  await page.evaluate(() => window.AdControl.refresh());
  await page.evaluate(() => window.AdControl.refresh());
  await page.waitForTimeout(1500);
  const afterRefresh = await summary();
  check('Refreshes do not double-count impressions', Number(afterRefresh.impressions) === Number(afterSpa.impressions));

  // Pausing the campaign must remove the creative after a config refresh.
  await api('/api/v1/campaigns/' + headerCampaignId + '/pause', { method: 'POST' });
  await page.evaluate(() => window.AdControl.refresh());
  await page.waitForFunction(
    (sel) => {
      const el = document.querySelector(sel);
      return el && el.getAttribute('data-adcontrol-state') === 'empty' && !el.querySelector('.adcontrol-root');
    },
    headerSelector,
    { timeout: 20000 },
  );
  check('Paused campaign is removed from an open page on refresh', true);
  const blankFallback = await page.$eval(headerSelector, (el) => {
    const placeholder = el.querySelector('.adcontrol-placeholder');
    return {
      state: el.getAttribute('data-adcontrol-state'),
      hasPlaceholder: !!placeholder,
      minHeight: placeholder ? getComputedStyle(placeholder).minHeight : '',
      maxWidth: placeholder ? getComputedStyle(placeholder).maxWidth : '',
      hasAdRoot: !!el.querySelector('.adcontrol-root'),
      hasClick: !!el.querySelector('a[data-adcontrol-click]'),
    };
  });
  check('Blank fallback reserves a placeholder', blankFallback.hasPlaceholder);
  check('Blank fallback reserves the configured height', blankFallback.minHeight === '90px');
  check('Blank fallback respects the configured width', blankFallback.maxWidth === '728px');
  check('Blank fallback is not a clickable ad', !blankFallback.hasAdRoot && !blankFallback.hasClick);

  await page.evaluate((key) => {
    const host = document.createElement('div');
    host.id = 'hide-fallback-host';
    const ad = document.createElement('div');
    ad.setAttribute('data-adcontrol-placement', key);
    host.appendChild(ad);
    document.body.appendChild(host);
  }, hidePlacementKey);
  const hideSelector = '[data-adcontrol-placement="' + hidePlacementKey + '"]';
  // An intentionally empty container has no box, so wait on the state
  // attribute rather than Playwright's "visible" definition.
  await page.waitForFunction(
    (sel) => {
      const el = document.querySelector(sel);
      return el && el.getAttribute('data-adcontrol-state') === 'empty';
    },
    hideSelector,
    { timeout: 20000 },
  );
  const hideFallback = await page.$eval(hideSelector, (el) => ({
    hasPlaceholder: !!el.querySelector('.adcontrol-placeholder'),
    childCount: el.childElementCount,
    text: (el.textContent || '').trim(),
  }));
  check('Hide fallback adds no placeholder', !hideFallback.hasPlaceholder);
  check('Hide fallback leaves the container empty', hideFallback.childCount === 0 && hideFallback.text === '');

  const spinnerStillFilled = await page.$eval(spaSelector, (el) => el.getAttribute('data-adcontrol-state'));
  check('Unrelated placement remains after pause', spinnerStillFilled === 'filled');

  // Phase 2 affiliate product card. This is a separate publisher site so it
  // cannot be affected by the pause above.
  const productDemoUrl = BASE + '/demo/?' + new URLSearchParams({
    site_key: productSite.public_site_key,
    placements: productPlacementKey,
  }).toString();
  await page.goto(productDemoUrl, { waitUntil: 'domcontentloaded', timeout: 30000 });
  const productSelector = '[data-adcontrol-placement="' + productPlacementKey + '"]';
  await page.waitForSelector(productSelector + '[data-adcontrol-state="filled"]', { timeout: 20000 });
  check('SDK renders an affiliate product card', true);

  const productCard = await page.$eval(productSelector, (el) => {
    const card = el.querySelector('.adcontrol-product-card');
    const link = el.querySelector('a[data-adcontrol-click]');
    const disclosure = card ? card.querySelector('.adcontrol-disclosure') : null;
    const disclosureStyle = disclosure ? getComputedStyle(disclosure) : null;
    const disclosureRect = disclosure ? disclosure.getBoundingClientRect() : null;
    return {
      affiliate: card ? card.getAttribute('data-adcontrol-affiliate') : '',
      badge: card ? (card.querySelector('.adcontrol-badge') || {}).textContent || '' : '',
      merchant: card ? (card.querySelector('.adcontrol-product-merchant') || {}).textContent || '' : '',
      title: card ? (card.querySelector('.adcontrol-product-title') || {}).textContent || '' : '',
      price: card ? (card.querySelector('.adcontrol-product-price') || {}).textContent || '' : '',
      disclosureText: disclosure ? (disclosure.textContent || '').trim() : '',
      disclosureRole: disclosure ? disclosure.getAttribute('role') : '',
      disclosureVisible: !!disclosure && !!disclosureStyle &&
        disclosureStyle.display !== 'none' && disclosureStyle.visibility !== 'hidden' &&
        Number(disclosureStyle.opacity) > 0 && !!disclosureRect && disclosureRect.height > 0,
      disclosureOutsideLink: !!(disclosure && link && !link.contains(disclosure)),
      href: link ? link.href : '',
      rel: link ? link.getAttribute('rel') : '',
      target: link ? link.getAttribute('target') : '',
      aria: link ? link.getAttribute('aria-label') : '',
      htmlLeaksAffiliateDestination: (el.innerHTML || '').indexOf('shopee.example.com') !== -1,
    };
  });
  check('Product card is marked as affiliate', productCard.affiliate === 'true');
  check('Product card has an Affiliate label', /Affiliate/i.test(productCard.badge));
  check('Product card shows the merchant', productCard.merchant === 'Shopee Test Merchant');
  check('Product card shows the product title', productCard.title === 'Browser Test Coffee Grinder');
  check('Product card shows the price', productCard.price === 'IDR 249.000');
  check('Affiliate disclosure is visible', productCard.disclosureVisible);
  check('Affiliate disclosure is outside the clickable link', productCard.disclosureOutsideLink);
  check('Affiliate disclosure is announced as a note', productCard.disclosureRole === 'note');
  check('Affiliate disclosure text is substantive', productCard.disclosureText.length >= 20);
  check('Affiliate card links through the first-party /r/ route', productCard.href.indexOf(BASE + '/r/') === 0);
  check('Affiliate link opens safely in a new tab', productCard.rel === 'noopener noreferrer' && productCard.target === '_blank');
  check('Affiliate link has an accessible advertisement label', /advertisement/i.test(productCard.aria));
  check('Affiliate destination is not exposed in the rendered DOM', !productCard.htmlLeaksAffiliateDestination);

  check('No CSP violations occurred in the demo host page', cspViolations.length === 0);
  check('No uncaught page errors occurred', pageErrors.length === 0);

  if (cspViolations.length) console.log('CSP:', cspViolations.slice(0, 5));
  if (pageErrors.length) console.log('Page errors:', pageErrors.slice(0, 5));

  await browser.close();

  console.log('\nRESULT: ' + pass + ' passed, ' + fail + ' failed');
  process.exit(fail === 0 ? 0 : 1);
}

main().catch((err) => {
  console.error('Browser test crashed:', err);
  process.exit(1);
});
