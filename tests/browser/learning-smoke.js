'use strict';

const { chromium } = require('playwright');

const BASE = (process.env.ADCONTROL_BASE || 'http://127.0.0.1:18080').replace(/\/+$/, '');
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

async function main() {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 390, height: 844 } });
  const errors = [];
  page.on('pageerror', (error) => errors.push('pageerror: ' + error.message));
  page.on('console', (message) => {
    if (message.type() === 'error') errors.push('console: ' + message.text());
  });

  await page.goto(BASE + '/', { waitUntil: 'networkidle' });
  await page.waitForSelector('h1', { timeout: 15000 });
  check('Learning home renders after lazy load', (await page.locator('h1').first().innerText()).trim().length > 0);
  check('Learning home title is set', (await page.title()).includes('AdControl Learning'));
  check('Learning home has a canonical URL', (await page.locator('link[rel="canonical"]').count()) === 1);
  check('Learning home fits a phone viewport',
    await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth + 1));

  await page.goto(BASE + '/courses', { waitUntil: 'networkidle' });
  await page.waitForSelector('h1', { timeout: 15000 });
  const courseLinks = page.locator('a[href^="/courses/"]');
  check('Public catalog renders course links', (await courseLinks.count()) > 0);
  const slug = await courseLinks.first().getAttribute('href');
  check('Course link is a public slug route', !!slug && /^\/courses\/[a-z0-9-]+$/.test(slug));

  await page.goto(BASE + slug, { waitUntil: 'networkidle' });
  await page.waitForSelector('h1', { timeout: 15000 });
  const courseTitle = (await page.title()).trim();
  check('Course detail renders a specific title', courseTitle.length > 0 && courseTitle !== 'AdControl Learning — Free digital marketing courses');
  check('Course detail canonical matches the route',
    (await page.locator('link[rel="canonical"]').getAttribute('href') || '').endsWith(slug));
  check('Course detail fits a phone viewport',
    await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth + 1));

  await page.goto(BASE + '/dashboard', { waitUntil: 'networkidle' });
  await page.waitForSelector('body', { timeout: 15000 });
  check('Dashboard entry renders content', (await page.locator('body').innerText()).includes('AdControl'));
  check('Public and dashboard routes produced no browser errors', errors.length === 0);

  if (errors.length) console.log(errors.join('\n'));
  await browser.close();
  console.log('\nRESULT: ' + pass + ' passed, ' + fail + ' failed');
  process.exit(fail === 0 ? 0 : 1);
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
