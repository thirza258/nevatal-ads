import React, { FormEvent, useState } from 'react';
import {
  ArrowRight,
  BarChart3,
  CheckCircle2,
  Globe2,
  Handshake,
  LucideIcon,
  Megaphone,
  Send,
  ShieldCheck,
  Sparkles,
  Target,
  Timer,
} from 'lucide-react';
import { learningApi } from '../api';
import { Breadcrumbs, Container, FieldHint, SectionHeading } from '../components';
import { Link } from '../router';
import { useSeo } from '../seo';
import { formatError } from '../state';
import { PartnerInquiryKind } from '../types';

interface Benefit {
  icon: LucideIcon;
  title: string;
  body: string;
}

const PUBLISHER_BENEFITS: Benefit[] = [
  {
    icon: Globe2,
    title: 'One lightweight script',
    body: 'Add a single SDK tag to your site. No framework, build step, or third-party tag manager required.',
  },
  {
    icon: Target,
    title: 'Placements you control',
    body: 'Choose where each unit can render, what it may contain, and whether an empty slot collapses or stays hidden.',
  },
  {
    icon: Handshake,
    title: 'House, direct, and affiliate demand',
    body: 'Run direct campaigns alongside first-party affiliate offers, with a clear disclosure beside every paid result.',
  },
  {
    icon: BarChart3,
    title: 'Honest, first-party counts',
    body: 'See impressions, clicks, and outbound redirects from your own domain. No cross-site profiles and no data resale.',
  },
  {
    icon: ShieldCheck,
    title: 'Domain allowlist and verification',
    body: 'Your public site key only serves verified domains. A central kill switch pauses everything immediately.',
  },
  {
    icon: Timer,
    title: 'A reviewed onboarding',
    body: 'We check fit and traffic quality before provisioning, so your readers never see an untrusted ad network.',
  },
];

const CLIENT_BENEFITS: Benefit[] = [
  {
    icon: Megaphone,
    title: 'Direct campaigns, not a black box',
    body: 'Provide a title, body, image, CTA, and destination. You always know which site and placement will run it.',
  },
  {
    icon: Timer,
    title: 'Scheduling and central pause',
    body: 'Set start and end dates, then pause a campaign or an entire site instantly when plans change.',
  },
  {
    icon: Globe2,
    title: 'First-party delivery',
    body: 'Units are served from the publisher’s own domain through the AdControl API, with a signed click redirect.',
  },
  {
    icon: BarChart3,
    title: 'Transparent delivery counts',
    body: 'Review impressions, clicks, and click-through rate by campaign, site, and placement — no estimated reach.',
  },
  {
    icon: Handshake,
    title: 'Placement-level fit',
    body: 'Target a specific placement on a specific site rather than buying an anonymous audience segment.',
  },
  {
    icon: ShieldCheck,
    title: 'A reviewed launch',
    body: 'We plan the placement, schedule, and creative with you before anything goes live on a publisher site.',
  },
];

const STEPS: Record<PartnerInquiryKind, Array<{ title: string; body: string }>> = {
  publisher: [
    {
      title: 'Send the application',
      body: 'Tell us about your site, audience, and the placements you have in mind. No account is created automatically.',
    },
    {
      title: 'We review and provision',
      body: 'If it is a fit, we create your publisher workspace, site record, placement keys, and a verified account for you.',
    },
    {
      title: 'Verify and launch',
      body: 'Add the SDK, verify domain ownership, and activate placements. You can pause everything from one switch.',
    },
  ],
  client: [
    {
      title: 'Send the brief',
      body: 'Tell us what you are promoting, the audience you want, timing, and any creative constraints. No account is created automatically.',
    },
    {
      title: 'Plan and provision',
      body: 'We agree on publisher sites and placements, then provision your client access, campaign, and schedule.',
    },
    {
      title: 'Launch and review',
      body: 'The campaign goes live on the agreed placements. Review delivery counts and adjust or pause at any time.',
    },
  ],
};

interface PartnerInquiryFormProps {
  kind: PartnerInquiryKind;
  websiteRequired: boolean;
  messagePlaceholder: string;
}

const PartnerInquiryForm: React.FC<PartnerInquiryFormProps> = ({
  kind,
  websiteRequired,
  messagePlaceholder,
}) => {
  const [values, setValues] = useState({
    name: '',
    email: '',
    company: '',
    website: '',
    message: '',
    fax: '',
  });
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState('');
  const [submitted, setSubmitted] = useState(false);

  const update = (field: keyof typeof values) => (
    event: React.ChangeEvent<HTMLInputElement | HTMLTextAreaElement>,
  ) => setValues((current) => ({ ...current, [field]: event.target.value }));

  const handleSubmit = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    if (submitting) return;
    setError('');
    setSubmitting(true);
    try {
      await learningApi.submitPartnerInquiry({ kind, ...values });
      setSubmitted(true);
    } catch (err) {
      setError(formatError(err));
    } finally {
      setSubmitting(false);
    }
  };

  if (submitted) {
    return (
      <div
        className="lc-card border-teal-200 bg-teal-50/70 p-6 sm:p-8"
        role="status"
        aria-live="polite"
      >
        <div className="flex items-start gap-3">
          <CheckCircle2 className="mt-0.5 h-6 w-6 shrink-0 text-teal-700" aria-hidden="true" />
          <div>
            <h3 className="text-lg font-bold text-teal-950">Application received</h3>
            <p className="mt-2 text-sm leading-6 text-teal-900">
              Thanks, {values.name || 'there'}. We will review your details and reply to{' '}
              <span className="font-semibold">{values.email}</span>.
            </p>
            <p className="mt-2 text-sm leading-6 text-teal-900">
              Nothing has been provisioned yet. If your application is a fit, we will follow up with the
              next steps and account setup.
            </p>
            <Link to="/" className="lc-button lc-button-secondary mt-5">
              Back to learning
              <ArrowRight className="h-4 w-4" aria-hidden="true" />
            </Link>
          </div>
        </div>
      </div>
    );
  }

  return (
    <form className="space-y-5" onSubmit={handleSubmit}>
      <div className="grid gap-5 sm:grid-cols-2">
        <div>
          <label htmlFor={`${kind}-name`} className="lc-label">Your name</label>
          <input
            id={`${kind}-name`}
            name="name"
            type="text"
            required
            minLength={2}
            maxLength={120}
            autoComplete="name"
            className="lc-input"
            value={values.name}
            onChange={update('name')}
          />
        </div>
        <div>
          <label htmlFor={`${kind}-email`} className="lc-label">Email</label>
          <input
            id={`${kind}-email`}
            name="email"
            type="email"
            required
            maxLength={254}
            autoComplete="email"
            className="lc-input"
            value={values.email}
            onChange={update('email')}
          />
          <FieldHint>We use this only to reply to your application.</FieldHint>
        </div>
      </div>

      <div className="grid gap-5 sm:grid-cols-2">
        <div>
          <label htmlFor={`${kind}-company`} className="lc-label">
            {kind === 'publisher' ? 'Company / site name' : 'Company / brand'}
          </label>
          <input
            id={`${kind}-company`}
            name="company"
            type="text"
            maxLength={160}
            autoComplete="organization"
            className="lc-input"
            value={values.company}
            onChange={update('company')}
          />
        </div>
        <div>
          <label htmlFor={`${kind}-website`} className="lc-label">
            Website {websiteRequired ? '' : <span className="font-normal text-slate-500">(optional)</span>}
          </label>
          <input
            id={`${kind}-website`}
            name="website"
            type="url"
            required={websiteRequired}
            maxLength={2048}
            placeholder="https://example.com"
            autoComplete="url"
            className="lc-input"
            value={values.website}
            onChange={update('website')}
          />
        </div>
      </div>

      <div>
        <label htmlFor={`${kind}-message`} className="lc-label">
          {kind === 'publisher' ? 'Tell us about your site' : 'Tell us about your campaign'}
        </label>
        <textarea
          id={`${kind}-message`}
          name="message"
          required
          minLength={20}
          maxLength={4000}
          rows={6}
          placeholder={messagePlaceholder}
          className="lc-input resize-y"
          value={values.message}
          onChange={update('message')}
        />
        <FieldHint>
          {kind === 'publisher'
            ? 'Include your audience, monthly traffic, and where you would like the units to appear.'
            : 'Include what you are promoting, the audience you want, timing, and a budget range if you have one.'}
        </FieldHint>
      </div>

      {/* Hidden anti-bot field. People never see or fill this. */}
      <div className="hidden" aria-hidden="true">
        <label htmlFor={`${kind}-fax`}>Fax</label>
        <input
          id={`${kind}-fax`}
          name="fax"
          type="text"
          tabIndex={-1}
          autoComplete="off"
          value={values.fax}
          onChange={update('fax')}
        />
      </div>

      {error && (
        <div className="rounded-lg border border-rose-200 bg-rose-50 p-4 text-sm text-rose-900" role="alert">
          {error}
        </div>
      )}

      <div className="flex flex-col gap-3 sm:flex-row sm:items-center sm:justify-between">
        <p className="max-w-md text-xs leading-5 text-slate-500">
          By applying, you agree that we may store these details to review your application. See the{' '}
          <Link to="/privacy" className="lc-link">privacy page</Link>. No account is created until we
          contact you.
        </p>
        <button type="submit" className="lc-button lc-button-primary shrink-0" disabled={submitting}>
          <Send className="h-4 w-4" aria-hidden="true" />
          {submitting ? 'Sending…' : 'Send application'}
        </button>
      </div>
    </form>
  );
};

interface PartnerPageProps {
  kind: PartnerInquiryKind;
  eyebrow: string;
  title: string;
  intro: string;
  benefits: Benefit[];
  formTitle: string;
  formDescription: string;
  websiteRequired: boolean;
  messagePlaceholder: string;
}

const PartnerPage: React.FC<PartnerPageProps> = ({
  kind,
  eyebrow,
  title,
  intro,
  benefits,
  formTitle,
  formDescription,
  websiteRequired,
  messagePlaceholder,
}) => {
  const other = kind === 'publisher'
    ? { to: '/become-a-client', label: 'Looking to advertise instead? Become a client' }
    : { to: '/become-a-publisher', label: 'Have a site? Become a publisher' };

  return (
    <Container>
      <Breadcrumbs items={[{ label: 'Home', to: '/' }, { label: eyebrow }]} />

      <section className="overflow-hidden rounded-3xl border border-slate-200 bg-white shadow-sm">
        <div className="grid gap-10 p-6 sm:p-10 lg:grid-cols-[1.15fr_0.85fr] lg:p-12">
          <div>
            <div className="lc-eyebrow">{eyebrow}</div>
            <h1 className="mt-3 max-w-2xl text-4xl font-bold leading-tight tracking-tight text-slate-950 sm:text-5xl">
              {title}
            </h1>
            <p className="mt-5 max-w-2xl text-base leading-8 text-slate-600">{intro}</p>
            <div className="mt-8 flex flex-col gap-3 sm:flex-row">
              <a href="#apply" className="lc-button lc-button-primary">
                Apply now
                <ArrowRight className="h-4 w-4" aria-hidden="true" />
              </a>
              <Link to={other.to} className="lc-button lc-button-secondary">
                {other.label}
              </Link>
            </div>
            <p className="mt-5 flex items-center gap-2 text-xs font-medium text-slate-500">
              <Sparkles className="h-4 w-4 text-teal-700" aria-hidden="true" />
              Applications are reviewed by a person. Expect a reply within a few business days.
            </p>
          </div>

          <div className="rounded-2xl border border-teal-100 bg-teal-50/60 p-6">
            <h2 className="text-sm font-bold uppercase tracking-[0.16em] text-teal-900">What you get</h2>
            <ul className="mt-4 space-y-3">
              {benefits.slice(0, 4).map((benefit) => {
                const Icon = benefit.icon;
                return (
                  <li key={benefit.title} className="flex gap-3 text-sm leading-6 text-teal-950">
                    <Icon className="mt-0.5 h-5 w-5 shrink-0 text-teal-700" aria-hidden="true" />
                    <span><span className="font-semibold">{benefit.title}.</span> {benefit.body}</span>
                  </li>
                );
              })}
            </ul>
          </div>
        </div>
      </section>

      <section aria-labelledby="benefits-heading" className="mt-12">
        <SectionHeading
          eyebrow="Why AdControl"
          title="Built for a direct relationship"
          description="No hidden exchanges, no resold audience profiles, and no automatic enrollment. You choose the placement; we keep the counts honest."
        />
        <h2 id="benefits-heading" className="sr-only">Benefits</h2>
        <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-3">
          {benefits.map((benefit) => {
            const Icon = benefit.icon;
            return (
              <article key={benefit.title} className="lc-card p-5">
                <span className="flex h-10 w-10 items-center justify-center rounded-xl bg-teal-50 text-teal-800">
                  <Icon className="h-5 w-5" aria-hidden="true" />
                </span>
                <h3 className="mt-4 font-bold text-slate-950">{benefit.title}</h3>
                <p className="mt-2 text-sm leading-6 text-slate-600">{benefit.body}</p>
              </article>
            );
          })}
        </div>
      </section>

      <section aria-labelledby="steps-heading" className="mt-12">
        <SectionHeading
          eyebrow="Process"
          title="What happens after you apply"
          description="A short, reviewed path. Nothing goes live on a site until both sides agree."
        />
        <ol className="grid gap-4 md:grid-cols-3">
          {STEPS[kind].map((step, index) => (
            <li key={step.title} className="lc-card p-5">
              <span className="flex h-9 w-9 items-center justify-center rounded-full bg-teal-900 text-sm font-bold text-white">
                {index + 1}
              </span>
              <h2 className="mt-4 font-bold text-slate-950">{step.title}</h2>
              <p className="mt-2 text-sm leading-6 text-slate-600">{step.body}</p>
            </li>
          ))}
        </ol>
      </section>

      <section id="apply" aria-labelledby="apply-heading" className="mt-12 scroll-mt-24">
        <div className="lc-card mx-auto max-w-3xl p-6 sm:p-8">
          <div className="lc-eyebrow">{kind === 'publisher' ? 'Publisher application' : 'Client brief'}</div>
          <h2 id="apply-heading" className="mt-2 text-2xl font-bold tracking-tight text-slate-950">
            {formTitle}
          </h2>
          <p className="mt-2 text-sm leading-6 text-slate-600">{formDescription}</p>
          <div className="mt-6">
            <PartnerInquiryForm
              kind={kind}
              websiteRequired={websiteRequired}
              messagePlaceholder={messagePlaceholder}
            />
          </div>
        </div>
      </section>
    </Container>
  );
};

export const BecomePublisherPage: React.FC = () => {
  useSeo({
    title: 'Become a publisher',
    description:
      'Apply to run AdControl placements on your website: one lightweight SDK, placements you control, direct and affiliate demand, and first-party counts.',
    path: '/become-a-publisher',
  });

  return (
    <PartnerPage
      kind="publisher"
      eyebrow="Become a publisher"
      title="Your audience. Your placements. Your revenue."
      intro="AdControl is a self-hosted ad platform for independent publishers. Apply to have your site reviewed and provisioned, then run direct campaigns and affiliate offers without handing your audience to a black-box exchange."
      benefits={PUBLISHER_BENEFITS}
      formTitle="Apply to publish with AdControl"
      formDescription="Tell us about your site and the placements you have in mind. We review every application before creating an account."
      websiteRequired
      messagePlaceholder="Example: A weekly newsletter and blog about home coffee. About 40,000 visits/month, mostly US and UK. I would like one unit below the article and one in the sidebar."
    />
  );
};

export const BecomeClientPage: React.FC = () => {
  useSeo({
    title: 'Become a client',
    description:
      'Apply to advertise with AdControl: direct campaigns on reviewed publisher sites, placement-level targeting, scheduling, central pause, and transparent delivery counts.',
    path: '/become-a-client',
  });

  return (
    <PartnerPage
      kind="client"
      eyebrow="Become a client"
      title="Put your campaign where the audience already is."
      intro="AdControl places direct campaigns on reviewed publisher sites. Apply with a short brief and we will plan the placement, schedule, and creative with you — no programmatic auction and no anonymous audience segments."
      benefits={CLIENT_BENEFITS}
      formTitle="Send your advertising brief"
      formDescription="Tell us what you are promoting and where you would like it to run. We review every brief before provisioning access."
      websiteRequired={false}
      messagePlaceholder="Example: We sell a small-batch coffee subscription and want to reach home coffee readers for six weeks. One article unit and one sidebar unit, starting next month."
    />
  );
};
