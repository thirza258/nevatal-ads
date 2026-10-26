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
  const auth = { Authorization: 'Bearer ' + login.token };

  // The browser suite pauses this campaign; reactivate it for this check.
  const publish = await fetch(BASE + '/api/v1/campaigns/' + headerCampaignId + '/publish', {
    method: 'POST',
    headers: auth,
  });
  if (!publish.ok) throw new Error('publish failed: ' + publish.status);

  const config = await (await fetch(BASE + '/delivery/v1/sites/' + siteKey + '/config')).json();
  check('Header placement active again', !!config.placements['header-banner']);

  const browser = await chromium.launch({ headless: true });
  const context = await browser.newContext({ viewport: { width: 1280, height: 900 } });
  const page = await context.newPage();
  const cspViolations = [];
  const pageErrors = [];
  page.on('console', (msg) => {
    const text = msg.text();
    if (/Content Security Policy|Refused to/i.test(text)) cspViolations.push(text);
  });
  page.on('pageerror', (err) => pageErrors.push(String(err)));

  const strictPolicy = [
    "default-src 'self'",
    "script-src 'self'",
    "style-src 'self'",
    "connect-src 'self'",
    "img-src 'self' data: https:",
    "font-src 'self'",
    "base-uri 'self'",
    "form-action 'self'",
    "frame-ancestors 'none'",
  ].join('; ');

  await page.route(BASE + '/demo/**', async (route) => {
    const response = await route.fetch();
    const headers = Object.assign({}, response.headers(), {
      'content-security-policy': strictPolicy,
    });
    await route.fulfill({ response, headers });
  });

  const demoUrl = BASE + '/demo/?' + new URLSearchParams({
    site_key: siteKey,
    placements: 'header-banner',
  }).toString();
  await page.goto(demoUrl, { waitUntil: 'domcontentloaded', timeout: 30000 });

  const selector = '[data-adcontrol-placement="header-banner"]';
  await page.waitForSelector(selector + '[data-adcontrol-state="filled"]', { timeout: 20000 });

  const result = await page.$eval(selector, (el) => {
    const wrapper = el.querySelector('.adcontrol-root');
    return {
      state: el.getAttribute('data-adcontrol-state'),
      inlineMaxWidth: wrapper ? wrapper.style.maxWidth : '',
      computedMaxWidth: wrapper ? getComputedStyle(wrapper).maxWidth : '',
      hasStylesheet: !!document.getElementById('adcontrol-sdk-styles'),
      label: el.textContent || '',
    };
  });

  check('SDK renders under style-src without unsafe-inline', result.state === 'filled');
  check('CSSOM dimension styles survive strict CSP', result.inlineMaxWidth === '728px');
  check('Computed dimensions survive strict CSP', result.computedMaxWidth === '728px');
  check('External SDK stylesheet loads under strict CSP', result.hasStylesheet);
  check('Ad label still rendered under strict CSP', /Ad/.test(result.label));
  check('No CSP violations under strict style policy', cspViolations.length === 0);
  check('No page errors under strict style policy', pageErrors.length === 0);

  if (cspViolations.length) console.log('CSP:', cspViolations.slice(0, 5));
  if (pageErrors.length) console.log('Page errors:', pageErrors.slice(0, 5));

  await browser.close();
  console.log('\nRESULT: ' + pass + ' passed, ' + fail + ' failed');
  process.exit(fail === 0 ? 0 : 1);
}

main().catch((err) => {
  console.error('Strict CSP test crashed:', err);
  process.exit(1);
});
