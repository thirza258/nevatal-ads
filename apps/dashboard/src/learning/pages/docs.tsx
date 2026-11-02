import React, { ReactNode } from 'react';
import {
  ArrowRight,
  BarChart3,
  CheckCircle2,
  Clock3,
  Code2,
  Eye,
  FileText,
  FlaskConical,
  Globe2,
  GraduationCap,
  History,
  Inbox,
  LayoutDashboard,
  LayoutGrid,
  LifeBuoy,
  Megaphone,
  MousePointerClick,
  Server,
  Settings,
  ShieldCheck,
  Ticket,
  UserCog,
  Users,
} from 'lucide-react';
import { Breadcrumbs, Container } from '../components';
import { Link } from '../router';
import { useSeo } from '../seo';

const INLINE_CODE = 'break-words rounded bg-slate-100 px-1.5 py-0.5 font-mono text-[0.8em] text-slate-800';

const Code: React.FC<{ children: ReactNode }> = ({ children }) => (
  <code className={INLINE_CODE}>{children}</code>
);

const CodeBlock: React.FC<{ children: string; label?: string }> = ({ children, label }) => (
  <figure className="mt-4">
    {label && (
      <figcaption className="mb-1.5 text-[11px] font-semibold uppercase tracking-[0.12em] text-slate-500">
        {label}
      </figcaption>
    )}
    <pre className="overflow-x-auto rounded-xl bg-slate-950 p-4 text-xs leading-6 text-slate-100">
      <code>{children}</code>
    </pre>
  </figure>
);

const Section: React.FC<{
  id: string;
  eyebrow: string;
  title: string;
  intro?: ReactNode;
  children: ReactNode;
}> = ({ id, eyebrow, title, intro, children }) => (
  <section id={id} aria-labelledby={`${id}-title`} className="lc-card scroll-mt-24 p-6 sm:p-8">
    <div className="lc-eyebrow">{eyebrow}</div>
    <h2 id={`${id}-title`} className="mt-2 text-2xl font-bold tracking-tight text-slate-950">
      {title}
    </h2>
    {intro && <div className="mt-3 max-w-3xl text-sm leading-7 text-slate-600">{intro}</div>}
    <div className="mt-6">{children}</div>
  </section>
);

const Area: React.FC<{ icon: ReactNode; title: string; children: ReactNode }> = ({
  icon,
  title,
  children,
}) => (
  <div className="rounded-xl border border-slate-200 bg-white p-5">
    <div className="flex items-center gap-3">
      <span className="flex h-9 w-9 shrink-0 items-center justify-center rounded-lg bg-teal-50 text-teal-800">
        {icon}
      </span>
      <h3 className="text-base font-bold text-slate-950">{title}</h3>
    </div>
    <div className="mt-3 space-y-2.5 text-sm leading-6 text-slate-600">{children}</div>
  </div>
);

const Bullets: React.FC<{ children: ReactNode }> = ({ children }) => (
  <ul className="list-disc space-y-2 pl-5">{children}</ul>
);

const Note: React.FC<{ tone?: 'info' | 'warning'; children: ReactNode }> = ({
  tone = 'info',
  children,
}) => (
  <div
    className={`mt-4 flex items-start gap-2.5 rounded-lg border p-3 text-xs leading-5 ${
      tone === 'warning'
        ? 'border-amber-200 bg-amber-50 text-amber-950'
        : 'border-sky-200 bg-sky-50 text-sky-950'
    }`}
  >
    <ShieldCheck className="mt-0.5 h-4 w-4 shrink-0" aria-hidden="true" />
    <div>{children}</div>
  </div>
);

const TOC_LINKS = [
  { id: 'how-it-works', label: 'How it works' },
  { id: 'roles', label: 'Roles & access' },
  { id: 'admin-guide', label: 'Administrator guide' },
  { id: 'publisher-guide', label: 'Publisher guide' },
  { id: 'learning', label: 'Learning & authoring' },
  { id: 'sdk', label: 'SDK integration' },
  { id: 'security', label: 'Security & measurement' },
  { id: 'troubleshooting', label: 'Troubleshooting' },
  { id: 'operators', label: 'For operators' },
];

const TOC: React.FC = () => (
  <ol className="mt-3 space-y-1">
    {TOC_LINKS.map((item, index) => (
      <li key={item.id}>
        <a
          href={`#${item.id}`}
          className="group flex items-center gap-2 rounded-lg px-2 py-1.5 text-sm text-slate-600 transition-colors hover:bg-teal-50 hover:text-teal-900"
        >
          <span className="w-4 text-[11px] font-semibold tabular-nums text-slate-400 group-hover:text-teal-700">
            {index + 1}.
          </span>
          {item.label}
        </a>
      </li>
    ))}
  </ol>
);

const AUDIENCES = [
  {
    href: '#admin-guide',
    icon: UserCog,
    title: 'Administrators',
    description: 'Run the dashboard, verify sites, and govern access.',
  },
  {
    href: '#publisher-guide',
    icon: Globe2,
    title: 'Publishers',
    description: 'Verify a site, place offers, and read your numbers.',
  },
  {
    href: '#learning',
    icon: GraduationCap,
    title: 'Learners & authors',
    description: 'Use the catalog and publish reviewed courses.',
  },
  {
    href: '#sdk',
    icon: Code2,
    title: 'Developers',
    description: 'Install the SDK and satisfy a strict CSP.',
  },
];

const FACTS = [
  { icon: Clock3, label: 'Propagation', value: 'About 60 seconds' },
  { icon: Eye, label: 'Impression rule', value: '50% visible for 1 second' },
  { icon: MousePointerClick, label: 'Clicks', value: 'Signed first-party redirects' },
  { icon: ShieldCheck, label: 'Fail-open', value: 'Host pages never break' },
];

const AreaList: React.FC<{ children: ReactNode }> = ({ children }) => (
  <div className="space-y-4">{children}</div>
);

const RoleTable: React.FC = () => (
  <div className="overflow-x-auto">
    <table className="w-full min-w-[40rem] border-collapse text-left text-sm">
      <caption className="sr-only">AdControl roles and what they can access</caption>
      <thead>
        <tr className="border-b border-slate-200 text-[11px] uppercase tracking-[0.12em] text-slate-500">
          <th scope="col" className="py-2 pr-4 font-semibold">Role</th>
          <th scope="col" className="py-2 pr-4 font-semibold">Works in</th>
          <th scope="col" className="py-2 font-semibold">What it can do</th>
        </tr>
      </thead>
      <tbody className="divide-y divide-slate-100 align-top">
        <tr>
          <td className="py-3 pr-4 font-semibold text-slate-900">admin</td>
          <td className="py-3 pr-4 text-slate-600">Dashboard and learning</td>
          <td className="py-3 text-slate-600">
            Full control of every tenant: websites, placements, campaigns, offers, publishers,
            inquiries, analytics, audit log, backups information, and learning staff accounts.
          </td>
        </tr>
        <tr>
          <td className="py-3 pr-4 font-semibold text-slate-900">publisher</td>
          <td className="py-3 pr-4 text-slate-600">Dashboard, own tenant only</td>
          <td className="py-3 text-slate-600">
            Manages its own sites, placements, campaigns, affiliate offers, and analytics. Cannot see
            or change another publisher’s inventory and cannot approve manual verification.
          </td>
        </tr>
        <tr>
          <td className="py-3 pr-4 font-semibold text-slate-900">analyst</td>
          <td className="py-3 pr-4 text-slate-600">Dashboard, read-only</td>
          <td className="py-3 text-slate-600">
            Read-only back-office access. The role exists in the schema, but this release has no
            in-product provisioning or tenant scoping for analysts.
          </td>
        </tr>
        <tr>
          <td className="py-3 pr-4 font-semibold text-slate-900">instructor / editor</td>
          <td className="py-3 pr-4 text-slate-600">Learning authoring</td>
          <td className="py-3 text-slate-600">
            Creates and edits course drafts. Editors and administrators review and publish. These
            accounts are created by an administrator in the authoring studio.
          </td>
        </tr>
        <tr>
          <td className="py-3 pr-4 font-semibold text-slate-900">learner</td>
          <td className="py-3 pr-4 text-slate-600">Learning</td>
          <td className="py-3 text-slate-600">
            Browses public courses, enrolls, marks lessons complete, takes quizzes, and exports or
            deletes their own account. Self-registration always creates a learner.
          </td>
        </tr>
      </tbody>
    </table>
  </div>
);

const FAQ_ITEMS = [
  {
    title: 'Placements stay empty',
    body: (
      <>
        Check, in order: the site is <strong>verified</strong> and not disabled; the script’s{' '}
        <Code>data-site-key</Code> matches the site; the page origin is in the site’s allowed
        origins; the placement key matches <Code>data-adcontrol-placement</Code> exactly; the
        placement allows the campaign’s source type; and a campaign is active or in schedule. Then
        open the Live Sandbox and the browser console with <Code>data-debug</Code> on a test page.
      </>
    ),
  },
  {
    title: 'DNS verification keeps failing',
    body: (
      <>
        The record name is <Code>_adcontrol-challenge.&lt;primary-domain&gt;</Code> and the value is{' '}
        <Code>adcontrol-site-verification=&lt;token&gt;</Code>. Wait for propagation, remove
        conflicting records, and retry. Changing the primary domain resets verification. If DNS
        cannot be changed, an administrator may use the manual fallback.
      </>
    ),
  },
  {
    title: 'A change is not live yet',
    body: (
      <>
        Allow up to roughly one minute: the SDK polls every 30 seconds and a browser or CDN may hold
        the delivery response for up to 30 seconds. Confirm the campaign is active, not paused or
        outside its schedule. In the console, <Code>AdControl.refresh()</Code> forces an immediate
        fetch.
      </>
    ),
  },
  {
    title: 'An affiliate URL is rejected',
    body: (
      <>
        Offers require an absolute <Code>https://</Code> URL with a public hostname, no userinfo, no
        whitespace, and at most 2048 characters. Validation is structural only: the destination is
        not fetched and a reachable URL does not prove the product, price, or commission is live.
      </>
    ),
  },
  {
    title: 'A click returns 410',
    body: (
      <>
        The signed redirect revalidates everything at click time. A paused or expired campaign, an
        inactive offer, an unverified or disabled site, or a disallowed origin returns a plain-text
        410 page instead of redirecting.
      </>
    ),
  },
  {
    title: 'Login is throttled',
    body: (
      <>
        Login attempts are rate-limited. Wait for the window to pass. Rate-limit buckets are held in
        process memory and reset when the API restarts. On internet-facing deployments, add a WAF
        rate limit for login and registration as well.
      </>
    ),
  },
];

export const DocsPage: React.FC = () => {
  useSeo({
    title: 'Platform documentation',
    description:
      'Administrator, publisher, learning, SDK integration, security, and operations documentation for the AdControl platform.',
    path: '/docs',
  });

  return (
    <Container>
      <Breadcrumbs items={[{ label: 'Home', to: '/' }, { label: 'Documentation' }]} />

      <div className="grid gap-8 lg:grid-cols-[1.4fr_0.6fr] lg:items-end">
        <div>
          <div className="lc-eyebrow">Platform documentation</div>
          <h1 className="mt-3 max-w-3xl text-4xl font-bold leading-[1.08] tracking-[-0.03em] text-slate-950 sm:text-5xl">
            The AdControl handbook.
          </h1>
          <p className="mt-4 max-w-2xl text-base leading-7 text-slate-600">
            Everything needed to run the platform end to end: administrator workflows, publisher
            onboarding, the learning area, SDK integration, and the boundaries behind honest
            measurement.
          </p>
          <div className="mt-6 flex flex-wrap gap-3">
            <a href="#admin-guide" className="lc-button lc-button-primary px-5 py-3">
              Start with the admin guide
              <ArrowRight className="h-4 w-4" aria-hidden="true" />
            </a>
            <a href="#sdk" className="lc-button lc-button-secondary px-5 py-3">
              Install the SDK
            </a>
          </div>
        </div>

        <aside className="lc-card p-5">
          <h2 className="text-xs font-bold uppercase tracking-[0.14em] text-slate-500">Quick facts</h2>
          <dl className="mt-3 space-y-3">
            {FACTS.map((fact) => {
              const Icon = fact.icon;
              return (
                <div key={fact.label} className="flex items-start gap-2.5">
                  <Icon className="mt-0.5 h-4 w-4 shrink-0 text-teal-700" aria-hidden="true" />
                  <div>
                    <dt className="text-[11px] font-semibold uppercase tracking-wider text-slate-500">
                      {fact.label}
                    </dt>
                    <dd className="text-sm font-medium text-slate-800">{fact.value}</dd>
                  </div>
                </div>
              );
            })}
          </dl>
        </aside>
      </div>

      <div className="mt-8 grid grid-cols-2 gap-3 lg:grid-cols-4">
        {AUDIENCES.map((audience) => {
          const Icon = audience.icon;
          return (
            <a
              key={audience.href}
              href={audience.href}
              className="lc-card group p-4 transition hover:-translate-y-0.5 hover:border-teal-200 hover:shadow-md"
            >
              <span className="flex h-9 w-9 items-center justify-center rounded-lg bg-teal-50 text-teal-800">
                <Icon className="h-4 w-4" aria-hidden="true" />
              </span>
              <div className="mt-3 flex items-center gap-1.5 text-sm font-bold text-slate-950">
                {audience.title}
                <ArrowRight className="h-3.5 w-3.5 text-teal-700 transition-transform group-hover:translate-x-0.5" aria-hidden="true" />
              </div>
              <p className="mt-1 text-xs leading-5 text-slate-500">{audience.description}</p>
            </a>
          );
        })}
      </div>

      <div className="mt-10 grid gap-8 lg:grid-cols-[15rem_minmax(0,1fr)]">
        <aside className="hidden lg:block">
          <nav aria-label="On this page" className="sticky top-24">
            <div className="text-xs font-bold uppercase tracking-[0.14em] text-slate-500">On this page</div>
            <TOC />
          </nav>
        </aside>

        <div className="min-w-0 space-y-6">
          <details className="lc-card p-4 lg:hidden">
            <summary className="cursor-pointer text-sm font-bold text-slate-900">Jump to a section</summary>
            <TOC />
          </details>

          <Section
            id="how-it-works"
            eyebrow="Overview"
            title="How AdControl works"
            intro={
              <>
                AdControl separates how a page looks from what it shows. Publishers keep their own
                layout and add one empty container per placement plus a single SDK script. The
                dashboard stores the creative, schedule, and destination. The SDK asks the API for
                the current configuration, renders only what is eligible right now, and reports
                impressions and clicks back.
              </>
            }
          >
            <div className="grid gap-4 sm:grid-cols-3">
              {[
                {
                  step: '1',
                  title: 'Install once',
                  body: 'Add the async SDK script and an empty element for each placement key. No redeploy is needed after this.',
                },
                {
                  step: '2',
                  title: 'Manage centrally',
                  body: 'Create sites, placements, campaigns, offers, and schedules in the dashboard, scoped to a publisher tenant.',
                },
                {
                  step: '3',
                  title: 'Measure honestly',
                  body: 'Impressions are qualified by visibility, clicks pass through signed redirects, and observed metrics stay separate from confirmed conversions.',
                },
              ].map((item) => (
                <div key={item.step} className="rounded-xl border border-slate-200 bg-white p-4">
                  <span className="flex h-7 w-7 items-center justify-center rounded-full bg-teal-900 text-xs font-bold text-white">
                    {item.step}
                  </span>
                  <h3 className="mt-3 text-sm font-bold text-slate-950">{item.title}</h3>
                  <p className="mt-1.5 text-xs leading-5 text-slate-600">{item.body}</p>
                </div>
              ))}
            </div>

            <div className="mt-6 rounded-xl border border-slate-200 bg-slate-50 p-5">
              <h3 className="flex items-center gap-2 text-sm font-bold text-slate-950">
                <CheckCircle2 className="h-4 w-4 text-teal-700" aria-hidden="true" />
                The delivery contract
              </h3>
              <Bullets>
                <li>
                  Delivery configuration refreshes every 30 seconds; browsers and CDNs may cache a
                  response for up to 30 seconds, so changes go live within roughly one minute.
                </li>
                <li>
                  An impression counts only after at least 50% of the placement has been visible for
                  one second.
                </li>
                <li>
                  House and direct clicks use <Code>/click/&lt;signed-token&gt;</Code>; affiliate
                  clicks use <Code>/r/&lt;signed-token&gt;</Code>. Both revalidate state server-side
                  and redirect to the stored destination.
                </li>
                <li>
                  Destinations are never written into the primary DOM. The host page can display an
                  ad but cannot read or rewrite where it points.
                </li>
                <li>
                  Delivery is fail-open for the host page: if the SDK or API is unavailable, the page
                  keeps working and containers stay empty or reserve blank space.
                </li>
                <li>
                  Every creative is visibly labeled Ad, Sponsored, Sponsored offer, or Affiliate and
                  exposes an accessible name ending in “(advertisement)”.
                </li>
              </Bullets>
            </div>
          </Section>

          <Section
            id="roles"
            eyebrow="Access model"
            title="Roles and access"
            intro="Dashboard accounts are invite-only. Public registration at /register always creates a learner; administrators seed the first admin account from the deployment environment only when no administrator exists."
          >
            <RoleTable />
            <Note>
              Administrators should change the seeded password in <strong>Settings</strong> after the
              first sign-in. Changing a password signs out every active session for that account,
              including the current one.
            </Note>
          </Section>

          <Section
            id="admin-guide"
            eyebrow="Administrator guide"
            title="Running the operations dashboard"
            intro={
              <>
                Sign in at <a href="/dashboard" className="lc-link">/dashboard</a>. Administrators
                see every area; publishers see the same navigation scoped to their own tenant.
                Analysts can read the views they are allowed to reach but cannot write.
              </>
            }
          >
            <AreaList>
              <Area icon={<LayoutDashboard className="h-4 w-4" aria-hidden="true" />} title="Overview">
                <p>
                  The landing view combines recent delivery totals, active campaign counts, connected
                  sites, a seven-day trend, and the latest audit entries. Shortcut buttons open the
                  create dialogs for a site, placement, or ad.
                </p>
              </Area>

              <Area icon={<Globe2 className="h-4 w-4" aria-hidden="true" />} title="Websites">
                <Bullets>
                  <li>
                    <strong>Register a website</strong> with its name, primary domain,
                    comma-separated allowed origins, fallback behavior, and permitted ad sources. The
                    site key is public; the origin allowlist is the security boundary. Use{' '}
                    <Code>*</Code> for local development only.
                  </li>
                  <li>
                    <strong>Verify ownership.</strong> Publisher-owned sites start unverified and
                    cannot deliver. Create the TXT record{' '}
                    <Code>_adcontrol-challenge.&lt;primary-domain&gt;</Code> with the value{' '}
                    <Code>adcontrol-site-verification=&lt;token&gt;</Code>, wait for propagation, then
                    choose <em>Verify with DNS TXT</em>. Unverified delivery requests return 404.
                  </li>
                  <li>
                    <strong>Manual verification</strong> is an administrator-only fallback when DNS
                    cannot be changed. Publishers cannot self-approve it. Changing the primary domain
                    resets verification.
                  </li>
                  <li>
                    <strong>Rotate Public Key</strong> invalidates the old key immediately; update the
                    snippet on every embedding page. <strong>Kill Switch</strong> pauses all active
                    campaigns for the site at once, and disabling the site stops delivery entirely.
                  </li>
                  <li>
                    <strong>Integration Snippet</strong> generates copy-paste HTML, React, Next.js,
                    and WordPress variants. <strong>Delete Website</strong> removes the inventory
                    after the usual confirmation.
                  </li>
                </Bullets>
              </Area>

              <Area icon={<LayoutGrid className="h-4 w-4" aria-hidden="true" />} title="Placements">
                <Bullets>
                  <li>
                    A placement is a stable slot: name, key, format, allowed source types, max items,
                    disclosure mode, fallback behavior, and notes.
                  </li>
                  <li>
                    Keys use letters, digits, <Code>_</Code>, and <Code>-</Code>, start with a letter
                    or digit, and stay within 64 characters. The key must match{' '}
                    <Code>data-adcontrol-placement</Code> exactly.
                  </li>
                  <li>
                    Formats are responsive banner, image, text link, native card, and product card.
                    Source types are <Code>house</Code>, <Code>direct</Code>, and{' '}
                    <Code>affiliate</Code>; affiliate product cards require the affiliate source on
                    both the site and the placement. <Code>external_network</Code> is reserved and
                    rejected until a reviewed adapter exists.
                  </li>
                  <li>
                    Fallback <strong>hide</strong> leaves the container empty;{' '}
                    <strong>blank</strong> reserves the configured width and height. An inactive or
                    unknown placement key never renders and never reserves space.
                  </li>
                  <li>Use one element per placement key per page.</li>
                </Bullets>
              </Area>

              <Area icon={<Megaphone className="h-4 w-4" aria-hidden="true" />} title="Ads & Campaigns">
                <Bullets>
                  <li>
                    Choose a placement and provide a title, body, optional HTTPS image, alt text, CTA
                    label, and an HTTPS destination. Schedules use UTC start and end times.
                  </li>
                  <li>
                    Statuses are <Code>draft</Code>, <Code>scheduled</Code>, <Code>active</Code>,{' '}
                    <Code>paused</Code>, <Code>expired</Code>, and <Code>archived</Code>. The publish
                    and pause toggle controls delivery without editing the creative.
                  </li>
                  <li>
                    Selection is one deliverable campaign per placement per refresh. Priority gates
                    the tier; eligible campaigns rotate by weight. A running experiment selects a
                    variant first, and audience/daily-budget filters apply before selection.
                  </li>
                  <li>
                    Destinations are validated as safe HTTPS URLs and rechecked server-side on every
                    click. Redirects are not stored in the page.
                  </li>
                  <li>
                    Affiliate campaigns copy their destination from the offer; a campaign request
                    cannot substitute a different URL.
                  </li>
                </Bullets>
              </Area>

              <Area icon={<Ticket className="h-4 w-4" aria-hidden="true" />} title="Affiliate Offers">
                <Bullets>
                  <li>
                    Paste the Shopee affiliate URL exactly as provided, including tracking and
                    campaign parameters. AdControl does not call the Shopee API, generate links, or
                    rewrite the query string.
                  </li>
                  <li>
                    Structural checks require an absolute <Code>https://</Code> URL with a public
                    hostname, no userinfo, no whitespace, and at most 2048 characters. Validation does
                    not fetch the destination.
                  </li>
                  <li>
                    Offers can be <Code>draft</Code>, <Code>active</Code>, or <Code>paused</Code>.
                    Pausing an offer stops affiliate serving immediately.
                  </li>
                  <li>
                    <strong>Place offer</strong> creates a product-card campaign whose destination is
                    copied server-side. The disclosure precedence is campaign override, then offer,
                    then site default, then a built-in affiliate disclosure. It must stay visible
                    outside the clickable link.
                  </li>
                  <li>
                    Clicks go through <Code>/r/&lt;signed-token&gt;</Code>, which revalidates site,
                    placement, campaign, offer, schedule, and origin before returning a 302 to the
                    exact stored URL.
                  </li>
                </Bullets>
              </Area>

              <Area icon={<Users className="h-4 w-4" aria-hidden="true" />} title="Publishers">
                <Bullets>
                  <li>
                    Provision a publisher with its name, owner email, temporary password (12–128
                    characters), contact details, timezone, and initial status. Passwords are never
                    displayed after creation; share them out of band.
                  </li>
                  <li>
                    Statuses are <Code>pending</Code>, <Code>active</Code>, <Code>suspended</Code>,
                    and <Code>rejected</Code>. Suspending an owner stops delivery for their inventory
                    immediately.
                  </li>
                  <li>
                    Publisher dashboards are tenant-isolated: a publisher cannot list or mutate
                    another publisher’s sites, placements, campaigns, or offers.
                  </li>
                  <li>There is no public publisher sign-up and no in-product analyst provisioning.</li>
                </Bullets>
              </Area>

              <Area icon={<Inbox className="h-4 w-4" aria-hidden="true" />} title="Inquiries">
                <Bullets>
                  <li>
                    Applications from <Link to="/become-a-publisher" className="lc-link">/become-a-publisher</Link>{' '}
                    and <Link to="/become-a-client" className="lc-link">/become-a-client</Link> land
                    here after validation, a 16 KiB cap, a per-origin rate limit, and a hidden
                    bot-trap check.
                  </li>
                  <li>
                    Search by name, email, company, or text, and filter by type and status:{' '}
                    <Code>new</Code>, <Code>reviewing</Code>, or <Code>closed</Code>.
                  </li>
                  <li>
                    Reply opens your email client. Status changes are audited. Applications never
                    create a user, publisher, site, campaign, or session.
                  </li>
                </Bullets>
              </Area>

              <Area icon={<BarChart3 className="h-4 w-4" aria-hidden="true" />} title="Analytics">
                <Bullets>
                  <li>
                    Metrics include impressions, clicks, outbound redirects, affiliate clicks, and
                    CTR, with a time series and breakdowns by site, placement, campaign, or source.
                  </li>
                  <li>
                    Filter by publisher (administrators), site, placement, campaign, and date range.
                    Authenticated CSV export mirrors the current filters.
                  </li>
                  <li>
                    <Code>confirmed_conversions</Code> and <Code>confirmed_commission</Code> include
                    only approved or reconciled events from a configured conversion source (browser
                    SDK, pixel, or HMAC-signed webhook). Clicks are observed behavior, not proof of a
                    sale; a redirect is never inferred as revenue.
                  </li>
                  <li>Event retention defaults to 180 days and is configured by the operator.</li>
                </Bullets>
              </Area>

              <Area icon={<History className="h-4 w-4" aria-hidden="true" />} title="Audit Log">
                <p>
                  An administrator-only trail of privileged actions: creates, publishes, pauses,
                  key rotations, password changes, deletes, and archives. Use it to review who
                  changed what, and when, after an incident or a handover.
                </p>
              </Area>

              <Area icon={<FlaskConical className="h-4 w-4" aria-hidden="true" />} title="Live Sandbox">
                <p>
                  A simulated host page that loads the real SDK against your current inventory.
                  Preview placement output, copy the snippet, and validate markup or CSP changes.
                  The sandbox is preview-only and never changes production campaigns.
                </p>
              </Area>

              <Area icon={<Settings className="h-4 w-4" aria-hidden="true" />} title="Settings">
                <Bullets>
                  <li>
                    The deployment panel shows environment, timezone, SDK version, configuration
                    cache, event retention, click-token TTL, the SDK URL, rate limits, and maximum
                    stale age.
                  </li>
                  <li>
                    Change your password here. It requires 12–128 characters and signs out every
                    active admin session.
                  </li>
                  <li>
                    Automatic online backups and retention are described here; secrets,{' '}
                    <Code>SECRET_KEY</Code>, and backup configuration stay in the deployment
                    environment and are never editable in the dashboard.
                  </li>
                </Bullets>
              </Area>
            </AreaList>
          </Section>

          <Section
            id="publisher-guide"
            eyebrow="Publisher guide"
            title="Monetizing a publisher-owned site"
            intro="Publishers are provisioned by an administrator. Once invited, the owner signs in at /dashboard and only ever sees their own tenant."
          >
            <ol className="space-y-4">
              {[
                {
                  title: 'Sign in and create the site',
                  body: 'Add the site, its primary domain, and every origin allowed to load it. The site starts unverified with monetization pending.',
                },
                {
                  title: 'Prove domain ownership',
                  body: 'Create the TXT record shown in the verification panel, then choose Verify with DNS TXT. Publishers cannot self-approve the administrator-only manual fallback.',
                },
                {
                  title: 'Create placements',
                  body: 'Use stable keys such as header-banner or article-inline. For product cards, allow the affiliate source on both the site and the placement.',
                },
                {
                  title: 'Add an affiliate offer',
                  body: 'Paste the exact Shopee affiliate URL. Validation is structural only; it does not confirm the product or commission. Publish the offer when it is ready.',
                },
                {
                  title: 'Place the offer',
                  body: 'Place offer creates a product-card campaign on an affiliate-capable placement. The server copies the destination from the stored offer.',
                },
                {
                  title: 'Monitor and pause',
                  body: 'Use Analytics for AdControl-observed impressions and clicks, and pause a campaign, offer, site, or all site ads whenever needed.',
                },
              ].map((step, index) => (
                <li key={step.title} className="flex gap-3">
                  <span className="flex h-7 w-7 shrink-0 items-center justify-center rounded-full bg-teal-50 text-xs font-bold text-teal-900">
                    {index + 1}
                  </span>
                  <div>
                    <h3 className="text-sm font-bold text-slate-950">{step.title}</h3>
                    <p className="mt-1 text-sm leading-6 text-slate-600">{step.body}</p>
                  </div>
                </li>
              ))}
            </ol>
            <Note tone="warning">
              The affiliate disclosure must remain visible, outside the clickable link. Do not hide,
              remove, wrap, or intercept the SDK’s product link. Affiliate clicks and observed
              outbound redirects do not prove that a sale or commission occurred.
            </Note>
          </Section>

          <Section
            id="learning"
            eyebrow="Learning platform"
            title="Learning and authoring"
            intro="The public learning app lives at / and serves the same deployment as the operations dashboard at /dashboard. Learning accounts are separate from back-office roles."
          >
            <div className="grid gap-4 md:grid-cols-2">
              <div className="rounded-xl border border-slate-200 bg-white p-5">
                <h3 className="flex items-center gap-2 text-sm font-bold text-slate-950">
                  <GraduationCap className="h-4 w-4 text-teal-700" aria-hidden="true" />
                  For learners
                </h3>
                <Bullets>
                  <li>
                    Browse <Link to="/courses" className="lc-link">courses</Link>,{' '}
                    <Link to="/topics" className="lc-link">topics</Link>, and{' '}
                    <Link to="/learning-paths" className="lc-link">learning paths</Link> without an
                    account.
                  </li>
                  <li>
                    Register at <Link to="/register" className="lc-link">/register</Link> to enroll,
                    track completed lessons, resume a course, and take quizzes.
                  </li>
                  <li>
                    Quizzes are graded server-side; answer keys and explanations are withheld until a
                    graded attempt.
                  </li>
                  <li>
                    Download an account export or delete a learner account from{' '}
                    <Link to="/my-learning" className="lc-link">My learning</Link>. Staff accounts
                    cannot self-delete.
                  </li>
                </Bullets>
              </div>

              <div className="rounded-xl border border-slate-200 bg-white p-5">
                <h3 className="flex items-center gap-2 text-sm font-bold text-slate-950">
                  <FileText className="h-4 w-4 text-teal-700" aria-hidden="true" />
                  For instructors and editors
                </h3>
                <Bullets>
                  <li>
                    An administrator creates instructor and editor accounts at{' '}
                    <Link to="/author/team" className="lc-link">/author/team</Link>. Share the initial
                    password out of band.
                  </li>
                  <li>
                    Authors build drafts with modules, lessons, resources, and optional quizzes in
                    safe Markdown. Raw HTML, scripts, unsafe URLs, and arbitrary iframes are rejected;
                    video embeds are limited to YouTube and Vimeo IDs.
                  </li>
                  <li>
                    Submit for review; an editor or administrator approves and publishes. Publishing
                    requires at least one module, an active lesson in every module, and content in
                    every active lesson. It also increments the version and stores an immutable
                    revision snapshot.
                  </li>
                  <li>
                    Freshness review is tracked with a 180-day next-review date. To change published
                    content, move the course back to draft, edit, review, and publish the next
                    version. Author analytics are aggregate-only.
                  </li>
                </Bullets>
              </div>
            </div>

            <div className="mt-4 rounded-xl border border-slate-200 bg-slate-50 p-5 text-sm leading-6 text-slate-600">
              <h3 className="text-sm font-bold text-slate-950">SEO and publication boundaries</h3>
              <p className="mt-2">
                <Code>/sitemap.xml</Code> and <Code>/robots.txt</Code> are generated by the API and
                include only published catalog content; private app and API shells are excluded from
                indexing. Canonical URLs use <Code>PUBLIC_BASE_URL</Code>, which must be an HTTPS
                origin with no path or trailing slash. Route metadata is client-rendered. Courses
                can be free or paid. Paid courses create a manual invoice and unlock after an
                administrator records settlement; there is no card checkout, certificate, cohort,
                or instructor marketplace.
              </p>
            </div>
          </Section>

          <Section
            id="sdk"
            eyebrow="Integration"
            title="Installing the browser SDK"
            intro="A placement needs two things: one script tag and one empty container element per placement key. That is the only change on the host site; everything else is managed centrally."
          >
            <CodeBlock label="One-time snippet">{`<script async
        src="https://marketing.nevatal.id/sdk/v1/adcontrol.js"
        data-site-key="site_pub_your_public_key"></script>`}</CodeBlock>

            <CodeBlock label="Placement markup">{`<div data-adcontrol-placement="header-banner"></div>`}</CodeBlock>

            <div className="mt-6 grid gap-4 md:grid-cols-2">
              <div className="min-w-0">
                <h3 className="text-sm font-bold text-slate-950">Optional attributes</h3>
                <div className="mt-2 overflow-x-auto">
                  <table className="w-full min-w-[26rem] border-collapse text-left text-sm">
                    <thead>
                      <tr className="border-b border-slate-200 text-[11px] uppercase tracking-[0.1em] text-slate-500">
                        <th scope="col" className="py-2 pr-4 font-semibold">Attribute</th>
                        <th scope="col" className="py-2 font-semibold">Meaning</th>
                      </tr>
                    </thead>
                    <tbody className="divide-y divide-slate-100 align-top">
                      <tr>
                        <td className="py-2.5 pr-4 font-mono text-xs text-slate-800">data-site-key</td>
                        <td className="py-2.5 text-slate-600">Required for auto-initialization; the website’s public key.</td>
                      </tr>
                      <tr>
                        <td className="py-2.5 pr-4 font-mono text-xs text-slate-800">data-api-base</td>
                        <td className="py-2.5 text-slate-600">API origin when the assets are hosted elsewhere.</td>
                      </tr>
                      <tr>
                        <td className="py-2.5 pr-4 font-mono text-xs text-slate-800">data-debug</td>
                        <td className="py-2.5 text-slate-600">Console diagnostics. Never enable in production.</td>
                      </tr>
                    </tbody>
                  </table>
                </div>
              </div>

              <div className="min-w-0">
                <h3 className="text-sm font-bold text-slate-950">Manual initialization</h3>
                <CodeBlock>{`AdControl.init({
  siteKey: 'site_pub_your_public_key',
  refreshIntervalMs: 30000,
  maxStaleMs: 300000,
});
AdControl.scan();     // rescan after a router change
AdControl.refresh();  // fetch configuration now
AdControl.destroy();  // remove ads and listeners`}</CodeBlock>
              </div>
            </div>

            <h3 className="mt-6 text-sm font-bold text-slate-950">Formats</h3>
            <div className="mt-2 overflow-x-auto">
              <table className="w-full min-w-[34rem] border-collapse text-left text-sm">
                <thead>
                  <tr className="border-b border-slate-200 text-[11px] uppercase tracking-[0.1em] text-slate-500">
                    <th scope="col" className="py-2 pr-4 font-semibold">Format</th>
                    <th scope="col" className="py-2 font-semibold">Rendering</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-slate-100 align-top">
                  <tr>
                    <td className="py-2.5 pr-4 font-mono text-xs text-slate-800">responsive_banner</td>
                    <td className="py-2.5 text-slate-600">Image banner with a text fallback card; responsive width, capped height.</td>
                  </tr>
                  <tr>
                    <td className="py-2.5 pr-4 font-mono text-xs text-slate-800">image</td>
                    <td className="py-2.5 text-slate-600">Fixed image banner using the same renderer.</td>
                  </tr>
                  <tr>
                    <td className="py-2.5 pr-4 font-mono text-xs text-slate-800">text_link</td>
                    <td className="py-2.5 text-slate-600">Inline labeled text link.</td>
                  </tr>
                  <tr>
                    <td className="py-2.5 pr-4 font-mono text-xs text-slate-800">native_card</td>
                    <td className="py-2.5 text-slate-600">Labeled card with optional image, title, description, and CTA.</td>
                  </tr>
                  <tr>
                    <td className="py-2.5 pr-4 font-mono text-xs text-slate-800">product_card</td>
                    <td className="py-2.5 text-slate-600">Affiliate product card with merchant, price text, CTA, and a visible disclosure outside the link.</td>
                  </tr>
                </tbody>
              </table>
            </div>

            <h3 className="mt-6 text-sm font-bold text-slate-950">Strict CSP</h3>
            <p className="mt-1.5 text-sm leading-6 text-slate-600">
              The SDK uses no <Code>eval</Code>, <Code>innerHTML</Code>, inline scripts, or inline
              style attributes. Allow the AdControl origin explicitly:
            </p>
            <CodeBlock>{`Content-Security-Policy:
  default-src 'self';
  script-src 'self' https://marketing.nevatal.id;
  style-src 'self' https://marketing.nevatal.id;
  img-src 'self' https://marketing.nevatal.id https://img.example.com;
  connect-src 'self' https://marketing.nevatal.id;
  object-src 'none';
  base-uri 'self';
  frame-ancestors 'none';`}</CodeBlock>
            <p className="mt-2 text-xs leading-5 text-slate-500">
              <Code>script-src</Code> loads the SDK, <Code>connect-src</Code> covers configuration and
              impression beacons, and <Code>style-src</Code> loads the SDK stylesheet. Add every host
              used for creative images to <Code>img-src</Code>. Subresource Integrity is optional and
              must be recomputed after each platform upgrade.
            </p>

            <Note>
              The SDK rescans the DOM for added or removed placement elements and on router events,
              so SPAs work without extra wiring. If a framework reuses a container for a different
              key, update the attribute and call <Code>AdControl.scan()</Code>.
            </Note>
          </Section>

          <Section
            id="security"
            eyebrow="Trust model"
            title="Security and measurement boundaries"
            intro="Security is enforced server-side at delivery and click time. The browser is never trusted to choose a destination or prove a conversion."
          >
            <div className="grid gap-4 md:grid-cols-2">
              <div className="rounded-xl border border-slate-200 bg-white p-5">
                <h3 className="flex items-center gap-2 text-sm font-bold text-slate-950">
                  <ShieldCheck className="h-4 w-4 text-teal-700" aria-hidden="true" />
                  What is enforced
                </h3>
                <Bullets>
                  <li>Origin and Referer allowlists on every delivery request.</li>
                  <li>DNS TXT or administrator-attested domain ownership before delivery.</li>
                  <li>Short-lived HMAC-signed redirects; the browser never submits a URL.</li>
                  <li>Schedule, tenant, offer, and destination revalidation on every click.</li>
                  <li>No-store responses for admin data; short-lived caching for delivery.</li>
                  <li>Rate limits for login, registration, delivery, and event reporting.</li>
                  <li>Tenant isolation between publisher dashboards.</li>
                </Bullets>
              </div>

              <div className="rounded-xl border border-slate-200 bg-white p-5">
                <h3 className="flex items-center gap-2 text-sm font-bold text-slate-950">
                  <Eye className="h-4 w-4 text-teal-700" aria-hidden="true" />
                  What the numbers mean
                </h3>
                <Bullets>
                  <li>An impression is qualified by viewport visibility, not just a render.</li>
                  <li>Clicks, outbound redirects, and affiliate clicks are observed events.</li>
                  <li>
                    Confirmed conversions and commission are recorded only through a configured
                    conversion source and become reportable after approval/reconciliation.
                  </li>
                  <li>Never infer a sale or commission from a click or a redirect.</li>
                  <li>
                    Conversion overview, funnel, attribution, and commission reconciliation live in
                    the <Code>Conversions</Code> dashboard area.
                  </li>
                </Bullets>
              </div>
            </div>

            <Note tone="warning">
              Deployment limitations worth knowing: SQLite in WAL mode supports one API instance per
              host, rate-limit buckets live in process memory and reset on restart, and the dashboard
              uses a bearer token in browser local storage. The shipped CSP mitigates script
              injection; an httpOnly cookie session would be stronger if the apps were split across
              origins.
            </Note>
          </Section>

          <Section
            id="troubleshooting"
            eyebrow="Support"
            title="Troubleshooting"
            intro="Fast checks for the issues that come up most often."
          >
            <div className="grid gap-4 md:grid-cols-2">
              {FAQ_ITEMS.map((item) => (
                <div key={item.title} className="rounded-xl border border-slate-200 bg-white p-5">
                  <h3 className="flex items-start gap-2 text-sm font-bold text-slate-950">
                    <LifeBuoy className="mt-0.5 h-4 w-4 shrink-0 text-teal-700" aria-hidden="true" />
                    {item.title}
                  </h3>
                  <p className="mt-2 text-sm leading-6 text-slate-600">{item.body}</p>
                </div>
              ))}
            </div>
            <p className="mt-4 text-sm leading-6 text-slate-600">
              Still stuck? Reproduce in the <strong>Live Sandbox</strong> and capture the exact
              placement key, site key, request URL, and browser console output. The repository’s
              browser suites describe the expected DOM and network behavior in detail.
            </p>
          </Section>

          <Section
            id="operators"
            eyebrow="Self-hosting"
            title="For operators"
            intro="AdControl is deployed as a Compose stack behind nginx with an outbound-only Cloudflare Tunnel. These are the settings most likely to matter after installation."
          >
            <div className="overflow-x-auto">
              <table className="w-full min-w-[36rem] border-collapse text-left text-sm">
                <thead>
                  <tr className="border-b border-slate-200 text-[11px] uppercase tracking-[0.1em] text-slate-500">
                    <th scope="col" className="py-2 pr-4 font-semibold">Variable</th>
                    <th scope="col" className="py-2 font-semibold">Purpose</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-slate-100 align-top">
                  <tr>
                    <td className="py-2.5 pr-4 font-mono text-xs text-slate-800">SECRET_KEY</td>
                    <td className="py-2.5 text-slate-600">Signs click tokens. Changing it invalidates outstanding redirects.</td>
                  </tr>
                  <tr>
                    <td className="py-2.5 pr-4 font-mono text-xs text-slate-800">ADMIN_EMAIL / ADMIN_PASSWORD</td>
                    <td className="py-2.5 text-slate-600">Seeds the first administrator only when the database has none.</td>
                  </tr>
                  <tr>
                    <td className="py-2.5 pr-4 font-mono text-xs text-slate-800">PUBLIC_BASE_URL</td>
                    <td className="py-2.5 text-slate-600">Canonical HTTPS origin for sitemap, robots, and learning metadata.</td>
                  </tr>
                  <tr>
                    <td className="py-2.5 pr-4 font-mono text-xs text-slate-800">EVENT_RETENTION_DAYS</td>
                    <td className="py-2.5 text-slate-600">Analytics retention, default 180 days.</td>
                  </tr>
                  <tr>
                    <td className="py-2.5 pr-4 font-mono text-xs text-slate-800">BACKUP_INTERVAL_HOURS / BACKUP_RETENTION</td>
                    <td className="py-2.5 text-slate-600">Automatic verified online SQLite backups and how many snapshots are kept.</td>
                  </tr>
                  <tr>
                    <td className="py-2.5 pr-4 font-mono text-xs text-slate-800">HTTP_BIND / HTTP_PORT</td>
                    <td className="py-2.5 text-slate-600">Loopback-only HTTP port the tunnel connects to. Do not expose it publicly.</td>
                  </tr>
                </tbody>
              </table>
            </div>

            <div className="mt-6 grid gap-4 md:grid-cols-3">
              <div className="rounded-xl border border-slate-200 bg-white p-4">
                <h3 className="flex items-center gap-2 text-sm font-bold text-slate-950">
                  <Server className="h-4 w-4 text-teal-700" aria-hidden="true" />
                  Health checks
                </h3>
                <p className="mt-2 text-xs leading-5 text-slate-600">
                  <Code>/healthz</Code> reports liveness and <Code>/readyz</Code> reports readiness.
                  Both should be reachable through the public origin after deployment.
                </p>
              </div>
              <div className="rounded-xl border border-slate-200 bg-white p-4">
                <h3 className="flex items-center gap-2 text-sm font-bold text-slate-950">
                  <CheckCircle2 className="h-4 w-4 text-teal-700" aria-hidden="true" />
                  Backups
                </h3>
                <p className="mt-2 text-xs leading-5 text-slate-600">
                  Use <Code>./scripts/backup.sh</Code> for a verified snapshot and{' '}
                  <Code>./scripts/restore.sh &lt;file&gt; --yes</Code> for a guarded restore. Copy
                  backups off the host and encrypt them; RAID is not a backup.
                </p>
              </div>
              <div className="rounded-xl border border-slate-200 bg-white p-4">
                <h3 className="flex items-center gap-2 text-sm font-bold text-slate-950">
                  <FileText className="h-4 w-4 text-teal-700" aria-hidden="true" />
                  Deep references
                </h3>
                <p className="mt-2 text-xs leading-5 text-slate-600">
                  The repository ships fuller references: <Code>docs/architecture.md</Code>,{' '}
                  <Code>docs/api.md</Code>, <Code>docs/security.md</Code>,{' '}
                  <Code>docs/operations.md</Code>, <Code>docs/integration.md</Code>,{' '}
                  <Code>docs/learning.md</Code>, and <Code>docs/backup-restore.md</Code>.
                </p>
              </div>
            </div>

            <Note>
              Run one API instance per SQLite database, keep the dashboard on the loopback side of
              the tunnel, and add edge rate limits for login and registration. The bundled test
              suites cover unit, API, stack, SDK, CSP, and learning smoke checks.
            </Note>
          </Section>
        </div>
      </div>
    </Container>
  );
};
