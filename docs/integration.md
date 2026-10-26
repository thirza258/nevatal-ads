# Publisher integration

This guide is for the sites you own that display AdControl ads. The host page
only needs a script tag and one container element per placement. All creative,
destination, schedule, and pause changes happen in the dashboard.

Production assets are served from **https://marketing.nevatal.id**.

## Prerequisites

1. Add the website in **Websites → Add website** with its primary origin, for
   example `https://www.example.com`.
2. Add every origin that will display the site's ads to **allowed domains**,
   including staging hosts and `https://example.com` if both apex and `www` serve
   pages. Scheme is stripped during matching; an entry may include a port.
3. Create a placement and a published campaign as described in the
   [README](../README.md#first-campaign).
4. Use the public `site_pub_…` key from the website page. It is safe to expose in
   HTML. The admin token and `SECRET_KEY` must never appear in a page.

For a **publisher-owned** site (Phase 2), the owning publisher account must be
active and the site must pass domain verification before delivery works:

1. An administrator provisions the publisher account in **Publishers**. There is
   no public publisher signup.
2. The publisher creates its own site. It starts unverified and delivery returns
   `404`.
3. Create the displayed DNS TXT record at
   `_adcontrol-challenge.<primary-domain>` with
   `adcontrol-site-verification=<token>`, then choose **Verify with DNS TXT**.
   An administrator can use manual verification when DNS cannot be changed.
4. Verification flips the site to verified and enables monetization. Changing the
   primary domain resets both and requires verification again.

Administrator-created (platform-owned) sites from Phase 1 remain verified and
monetization-enabled and do not need a DNS record.

## Shopee affiliate product cards

Product-card placements use manual affiliate offers owned by the publisher:

1. Open **Affiliate Offers → Add offer**. Paste the Shopee affiliate URL exactly
   as supplied. Do not shorten, decode, or edit its query string. The URL must be
   `https://`, have a public hostname, contain no username/password, and be no
   longer than 2048 characters.
2. Select **Validate**. AdControl checks the URL structure only. It does not
   fetch the destination, call Shopee, generate an affiliate link, verify stock,
   or confirm commission terms. Publish the offer when it is ready.
3. Create a `product_card` placement with `affiliate` in its allowed source
   types, then choose **Place offer** to create the campaign. The server copies
   the destination from the offer; the browser request cannot set it. Publish the
   campaign.
4. The SDK renders a labeled product card with the merchant, product title,
   price text, CTA, and a visible affiliate disclosure.

The disclosure is outside the clickable link, cannot be disabled for affiliate
cards, and uses this precedence: campaign override → offer disclosure → site
default → built-in fallback. Keep the text visible and legible. Do not overlay,
hide, or truncate it.

Affiliate clicks use the first-party `/r/<signed-token>` endpoint. The API
validates the token, site, placement, campaign, schedule, active offer, publisher
match, affiliate URL, and origin, records a click plus an outbound redirect, and
only then returns a `302` to the exact stored affiliate URL. There is no
client-supplied destination and no query-string override. See
[API reference: affiliate offers](api.md#affiliate-offers).

## Snippet

```html
<script async
        src="https://marketing.nevatal.id/sdk/v1/adcontrol.js"
        data-site-key="site_pub_your_public_key"></script>
```

Place it once, in the `<head>` or at the end of `<body>`. The SDK:

- fetches configuration from the same origin as the script,
- loads `/sdk/v1/adcontrol.css` itself,
- scans for placement elements at DOM ready and when elements are added or
  removed (SPAs),
- polls for configuration every 30 seconds.

If the script cannot load, or the API is unavailable, the host page continues to
run normally and ad containers stay empty.

### Optional script attributes

| Attribute | Meaning |
| --- | --- |
| `data-site-key` | Required for auto-initialization. The website's public key. |
| `data-api-base` | API origin when it differs from the script origin. Defaults to the script's origin. |
| `data-debug` | Enables console diagnostics. Do not enable in production. |

### Manual initialization

For a bundler or a tag manager that cannot use the script attributes:

```html
<script src="https://marketing.nevatal.id/sdk/v1/adcontrol.js"></script>
<script>
  AdControl.init({
    siteKey: 'site_pub_your_public_key',
    // apiBase: 'https://marketing.nevatal.id', // only when the assets are elsewhere
    refreshIntervalMs: 30000, // clamped to 15000–300000
    maxStaleMs: 300000,       // clamped to 30000–3600000
  });
</script>
```

`AdControl.scan()` forces an element rescan; `AdControl.refresh()` fetches
configuration immediately; `AdControl.destroy()` removes rendered ads and
listeners. All methods are safe to call repeatedly and never throw into the host
page.

## Placement markup

```html
<div data-adcontrol-placement="header-banner"></div>
```

The attribute value must exactly match the **placement key** in the dashboard
(letters, digits, `_`, `-`; starts with a letter or digit; up to 64 characters).

- Use one element per placement key per page. If duplicates exist, each is
  rendered and can produce its own qualified impression.
- Do not put fallback content inside the container: the SDK replaces the
  container's children when it renders. Use the platform's fallback behavior
  instead.
- Style the container's width in your own CSS. The SDK respects the placement's
  configured width/height for rendering and blank reservation, but it does not
  force outer-page layout.

### Dynamic pages and SPAs

The SDK observes the DOM for added or removed `data-adcontrol-placement`
elements and rescans on `pushState`/`popstate`/`hashchange`. If a router renders
placements after the initial scan, they are picked up automatically. If your
framework reuses a container for a different placement key, update the attribute
and call `AdControl.scan()` to make the change immediate.

## Formats

| Format | Rendering |
| --- | --- |
| `responsive_banner` | Image banner, or a text fallback card when no image is set or the image fails to load. Responsive width, capped height. |
| `image` | Same renderer as a responsive banner. |
| `text_link` | Inline labeled text link. |
| `native_card` | Labeled card with optional image, title, description, and CTA. |
| `product_card` | Product card for affiliate offers: merchant, image, title, description, price text, CTA, and a visible disclosure outside the link. Affiliate cards use `/r/`. |

All renderers use `document.createElement` and `textContent`; creative text is
never treated as HTML. Links open in a new tab with
`rel="noopener noreferrer"`. Every creative is labeled **Ad**, **Sponsored**,
**Sponsored offer**, or **Affiliate** and exposes an accessible name ending in
“(advertisement)”. Images use `alt` text from the campaign and
`referrerpolicy="no-referrer"`.

The delivery response and DOM contain no destination URL for any campaign. For
affiliate cards the destination exists only behind the signed `/r/` token; the
host page cannot read or rewrite it. Do not replace, intercept, or rebuild the
SDK's product link.

Only HTTPS image URLs should be used in production; HTTP images are blocked as
mixed content on HTTPS pages.

## Fallback behavior

Fallback is configured per placement and applies when the placement is active
but has no eligible campaign (paused, out of schedule, expired, or none
published).

| Value | Behavior |
| --- | --- |
| `hide` | AdControl content is cleared. The host container is left in place; any size your own CSS gives it still applies. |
| `blank` | A transparent `.adcontrol-placeholder` reserves the placement's configured width (max-width) and height (min-height) without showing an ad or a link. |
| `custom` | Accepted by the API for compatibility; the current SDK treats it as `hide`. The dashboard exposes `hide` and `blank`. |

An **inactive or unknown placement key** is never rendered and never reserves
space, regardless of fallback. This prevents stale markup from creating holes
after a placement is disabled.

When the whole site is disabled, the key is rotated, or the page origin becomes
disallowed, the SDK clears all rendered ads immediately rather than showing the
last known creative.

## Clicks

House/direct clicks go to a relative `/click/<signed-token>` URL resolved against
the API origin. Affiliate product-card clicks go to `/r/<signed-token>`. Both are
first-party paths; the API validates the signature and TTL and revalidates the
site, placement, campaign, schedule, destination/offer, and origin before
recording the click and redirecting.

- Tokens live for 24 hours and are refreshed by the SDK's periodic config
  refresh.
- Affiliate redirects preserve the stored URL exactly, including query-string
  encoding and parameter order. AdControl adds no tracking parameters.
- A paused/expired campaign, inactive offer, or disabled/unverified site returns
  a plain-text `410` page.
- A click that Firefox prefetches or a privacy tool strips the referrer may still
  work; an explicitly disallowed `Origin` is rejected.
- Do not wrap the SDK anchor, intercept clicks, or rewrite the click URL.
  Client-submitted click beacons are rejected by design.
- `/r/` records both the signed click and an observed outbound redirect. Neither
  event proves that a sale occurred or that commission was earned.

## Content Security Policy

The SDK does not use `eval`, `innerHTML`, inline scripts, or inline styles in
markup. With a strict CSP, allow the AdControl origins explicitly:

```http
Content-Security-Policy:
  default-src 'self';
  script-src 'self' https://marketing.nevatal.id;
  style-src 'self' https://marketing.nevatal.id;
  img-src 'self' https://marketing.nevatal.id https://img.example.com;
  connect-src 'self' https://marketing.nevatal.id;
  object-src 'none';
  base-uri 'self';
  frame-ancestors 'none';
```

Notes:

- `script-src` is needed for `adcontrol.js`; `connect-src` for the config fetch
  and impression beacons; `style-src` for `adcontrol.css`.
- `img-src` must include the hosts you use for creative images. `https:` is
  needed only if creatives may reference arbitrary HTTPS image hosts. Do not add
  broad `script-src`/`connect-src` allowances.
- On a local demo without TLS, substitute `http://127.0.0.1:18080` (or your
  `HTTP_PORT`) for the AdControl origin.
- `style-src 'unsafe-inline'` is **not** required. The SDK's own CSP-compatible
  behavior is covered by the browser suite.

The bundled dashboard, demo, and API responses already apply restrictive headers
from nginx. The policy above is for **publisher** pages.

## Subresource Integrity (optional)

SRI pins the exact SDK bytes. Compute the hash after every AdControl upgrade and
update the publisher HTML at the same time:

```bash
curl -fsS https://marketing.nevatal.id/sdk/v1/adcontrol.js \
  | openssl dgst -sha384 -binary \
  | openssl base64 -A
```

```html
<script async
        src="https://marketing.nevatal.id/sdk/v1/adcontrol.js"
        integrity="sha384-REPLACE_WITH_THE_COMPUTED_HASH"
        crossorigin="anonymous"
        data-site-key="site_pub_your_public_key"></script>
```

SRI requires CORS, which the SDK endpoint provides. A mismatch blocks the SDK
and therefore removes ads from the page (fail-safe, not fail-open). During a
rolling upgrade, a browser holding the previous cached SDK (up to five minutes)
can fail a new hash; deploy the hash change after allowing the old asset to
expire, or accept a brief ad outage. SRI is optional because this trade-off is
not always desirable.

## Local development

The bundled demo publisher is at `http://127.0.0.1:18080/demo/`. Add that origin
(`127.0.0.1:18080`, or your `HTTP_PORT`) to the test website's allowed domains,
then open:

```
http://127.0.0.1:18080/demo/?site_key=site_pub_your_key
```

The demo page renders its own layout and placement containers, then loads the
real SDK with `data-api-base` set to the current origin. It does not include
inline scripts, so the shipped CSP applies unchanged.

## Verifying an integration

1. Open the page with the browser console visible; add `data-debug` or call
   `AdControl.init({ debug: true, ... })` for SDK logs.
2. Confirm `/delivery/v1/sites/<key>/config` returns `200` from the page's origin
   and contains your placement. A `404` on a publisher-owned site usually means
   verification or monetization is incomplete.
3. Confirm the container receives `data-adcontrol-state="filled"` and a signed
   click link. The href must start with `/click/` for house/direct ads or `/r/`
   for affiliate product cards.
4. Leave the ad at least 50% visible for one second; confirm one impression in
   **Analytics**.
5. Click once; confirm the redirect lands on the configured destination and the
   click count increases. For an affiliate card, confirm **Outbound redirects**
   increments and the exact Shopee URL is preserved.
6. For a product card, confirm the disclosure is visible outside the clickable
   area and that the page response/DOM does not contain the affiliate
   destination.
7. Pause the campaign; within about a minute the ad should disappear (or its
   configured fallback should apply).
8. Open the page with AdControl unreachable (block the origin in DevTools); the
   page must still render and remain usable.

## Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| `403 Domain origin not authorized` | The page origin is missing from the site's allowed domains. Add the exact scheme-less host and port if non-default. |
| `404 Site not found or disabled` | Key was rotated, site disabled, or the key is mistyped. |
| `404 Site is not yet verified for monetization` | The publisher site has not passed DNS verification, monetization is disabled, or the publisher is suspended. Check **Websites → verification**. |
| Config is `200` but placement is missing | Placement is disabled, or the site is active but has no active placement; check the placement's status. |
| Placement present with `campaign_id: ""` | No eligible campaign. Check campaign status, schedule, placement assignment, allowed source types, offer status, and verification. |
| Affiliate card does not render | An affiliate campaign requires an active offer, a `product_card` placement allowing `affiliate`, matching publisher ownership, and a published campaign. Check the delivery config and offer status. |
| Ad renders but no impression | The ad was never 50% visible for one second, `IntersectionObserver` is unavailable, or the page is backgrounded. |
| Click returns `410` | The campaign was paused/expired, the offer was paused, the site/publisher was disabled, or the token is older than 24 hours; reload the page for a fresh token. |
| Click returns `403` | The click request's `Origin`/`Referer` is outside the allowlist. |
| Ads vanish after an update | A strict CSP blocked the new stylesheet/script, or an SRI hash is stale. Check the console. |
| Affiliate URL rejected | Use an absolute HTTPS URL with a public hostname, no userinfo, no leading/trailing whitespace, and at most 2048 characters. AdControl does not fetch or fix the URL for you. |
