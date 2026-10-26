import React, { ReactNode, useState } from 'react';
import {
  ArrowUpRight,
  CheckCircle2,
  Clock3,
  ExternalLink,
  GraduationCap,
  Layers3,
  RotateCw,
  ShieldAlert,
} from 'lucide-react';
import { Link } from './router';
import { formatDuration, levelLabel, statusLabel } from './state';
import { PublicCourse } from './types';

interface ProgressBarProps {
  value: number;
  label?: string;
  showValue?: boolean;
  size?: 'sm' | 'md';
}

export const ProgressBar: React.FC<ProgressBarProps> = ({
  value,
  label = 'Course progress',
  showValue = true,
  size = 'md',
}) => {
  const safeValue = Math.max(0, Math.min(100, Math.round(value)));
  const height = size === 'sm' ? 'h-1.5' : 'h-2.5';
  return (
    <div className="flex items-center gap-3">
      <div
        className={`min-w-0 flex-1 overflow-hidden rounded-full bg-[#cde2fb] ${height}`}
        role="progressbar"
        aria-label={label}
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={safeValue}
        aria-valuetext={`${safeValue}% complete`}
      >
        <div
          className={`${height} rounded-full bg-[#2a78d6] transition-[width] duration-300`}
          style={{ width: `${safeValue}%` }}
        />
      </div>
      {showValue && (
        <span className="shrink-0 text-xs font-semibold tabular-nums text-slate-600">
          {safeValue}%
        </span>
      )}
    </div>
  );
};

interface StatusBadgeProps {
  status: string;
  label?: string;
}

export const StatusBadge: React.FC<StatusBadgeProps> = ({ status, label }) => {
  const tone: Record<string, string> = {
    published: 'border-emerald-200 bg-emerald-50 text-emerald-900',
    approved: 'border-sky-200 bg-sky-50 text-sky-900',
    in_review: 'border-amber-200 bg-amber-50 text-amber-950',
    draft: 'border-slate-200 bg-slate-50 text-slate-700',
    archived: 'border-zinc-200 bg-zinc-100 text-zinc-700',
    completed: 'border-emerald-200 bg-emerald-50 text-emerald-900',
    active: 'border-slate-200 bg-white text-slate-700',
  };
  const showCheck = status === 'published' || status === 'completed';
  return (
    <span
      className={`inline-flex items-center gap-1.5 rounded-full border px-2.5 py-1 text-xs font-semibold ${
        tone[status] || tone.draft
      }`}
    >
      {showCheck && <CheckCircle2 className="h-3.5 w-3.5" aria-hidden="true" />}
      {label || statusLabel(status)}
    </span>
  );
};

export const LevelBadge: React.FC<{ level: string }> = ({ level }) => (
  <span className="inline-flex items-center gap-1.5 text-xs font-medium text-slate-600">
    <GraduationCap className="h-4 w-4 text-teal-700" aria-hidden="true" />
    {levelLabel(level)}
  </span>
);

export const DurationBadge: React.FC<{ minutes: number }> = ({ minutes }) => (
  <span className="inline-flex items-center gap-1.5 text-xs font-medium text-slate-600">
    <Clock3 className="h-4 w-4 text-teal-700" aria-hidden="true" />
    {formatDuration(minutes)}
  </span>
);

interface CourseCardProps {
  course: PublicCourse;
  compact?: boolean;
}

export const CourseCard: React.FC<CourseCardProps> = ({ course, compact = false }) => {
  const headingId = `course-title-${course.slug}`;
  return (
    <article
      className={`lc-card group flex h-full flex-col p-5 transition hover:-translate-y-0.5 hover:border-teal-200 hover:shadow-md sm:p-6 ${
        compact ? 'gap-3' : 'gap-4'
      }`}
      itemScope
      itemType="https://schema.org/Course"
      aria-labelledby={headingId}
    >
      <meta itemProp="courseCode" content={course.slug} />
      <meta itemProp="educationalLevel" content={levelLabel(course.level)} />
      <meta itemProp="isAccessibleForFree" content="true" />
      <meta itemProp="description" content={course.summary} />
      <span className="hidden" itemProp="provider" itemScope itemType="https://schema.org/Organization">
        <meta itemProp="name" content="AdControl Learning" />
      </span>

      <div className="flex flex-wrap items-center gap-2">
        <Link
          to={`/topics/${course.topic_slug}`}
          className="text-xs font-bold uppercase tracking-[0.14em] text-teal-800 hover:text-teal-950"
        >
          {course.topic_title}
        </Link>
        <span className="text-slate-300" aria-hidden="true">/</span>
        <span className="text-xs font-medium text-slate-500">Free</span>
      </div>

      <h3 id={headingId} className="text-lg font-bold leading-snug tracking-tight text-slate-950 sm:text-xl">
        <Link to={`/courses/${course.slug}`} itemProp="url" className="hover:text-teal-900">
          <span itemProp="name">{course.title}</span>
        </Link>
      </h3>

      {!compact && <p className="line-clamp-3 text-sm leading-6 text-slate-600">{course.summary}</p>}

      <div className="mt-auto flex flex-wrap items-center gap-x-4 gap-y-2 border-t border-slate-100 pt-4">
        <LevelBadge level={course.level} />
        <DurationBadge minutes={course.duration_minutes} />
        <span className="ml-auto inline-flex items-center gap-1 text-xs font-semibold text-teal-800">
          View course
          <ArrowUpRight className="h-3.5 w-3.5 transition-transform group-hover:-translate-y-0.5 group-hover:translate-x-0.5" aria-hidden="true" />
        </span>
      </div>
    </article>
  );
};

interface VideoEmbedProps {
  provider: string;
  videoId: string;
  title: string;
}

export const VideoEmbed: React.FC<VideoEmbedProps> = ({ provider, videoId, title }) => {
  if (!/^[A-Za-z0-9_-]{6,64}$/.test(videoId)) return null;
  const normalizedProvider = provider.toLowerCase();
  let src = '';
  let watchUrl = '';
  if (normalizedProvider === 'youtube') {
    src = `https://www.youtube-nocookie.com/embed/${encodeURIComponent(videoId)}?rel=0`;
    watchUrl = `https://www.youtube.com/watch?v=${encodeURIComponent(videoId)}`;
  } else if (normalizedProvider === 'vimeo') {
    src = `https://player.vimeo.com/video/${encodeURIComponent(videoId)}`;
    watchUrl = `https://vimeo.com/${encodeURIComponent(videoId)}`;
  } else {
    return null;
  }

  return (
    <figure className="my-8">
      <div className="aspect-video overflow-hidden rounded-2xl border border-slate-200 bg-slate-950 shadow-sm">
        <iframe
          src={src}
          title={`Video lesson: ${title}`}
          className="h-full w-full"
          loading="lazy"
          referrerPolicy="strict-origin-when-cross-origin"
          allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; fullscreen"
          allowFullScreen
        />
      </div>
      <figcaption className="mt-2 text-right text-xs text-slate-500">
        <a
          href={watchUrl}
          target="_blank"
          rel="noopener noreferrer"
          className="inline-flex items-center gap-1 font-medium text-slate-600 hover:text-teal-900"
        >
          Open video on {normalizedProvider === 'youtube' ? 'YouTube' : 'Vimeo'}
          <ExternalLink className="h-3 w-3" aria-hidden="true" />
        </a>
      </figcaption>
    </figure>
  );
};

export const LoadingState: React.FC<{ label?: string }> = ({ label = 'Loading learning content' }) => (
  <div className="flex min-h-[16rem] flex-col items-center justify-center gap-3 py-16 text-slate-600" role="status" aria-live="polite">
    <span className="h-8 w-8 animate-spin rounded-full border-2 border-slate-200 border-t-teal-700" aria-hidden="true" />
    <span className="text-sm font-medium">{label}…</span>
  </div>
);

interface ErrorStateProps {
  title?: string;
  message: string;
  onRetry?: () => void;
}

export const ErrorState: React.FC<ErrorStateProps> = ({
  title = 'We could not load this page',
  message,
  onRetry,
}) => (
  <div className="lc-card my-8 border-rose-200 bg-rose-50/70 p-6" role="alert">
    <div className="flex items-start gap-3">
      <ShieldAlert className="mt-0.5 h-5 w-5 shrink-0 text-rose-800" aria-hidden="true" />
      <div className="min-w-0">
        <h2 className="font-bold text-rose-950">{title}</h2>
        <p className="mt-1 text-sm leading-6 text-rose-900">{message}</p>
        {onRetry && (
          <button type="button" onClick={onRetry} className="lc-button lc-button-secondary mt-4">
            <RotateCw className="h-4 w-4" aria-hidden="true" />
            Try again
          </button>
        )}
      </div>
    </div>
  </div>
);

export const EmptyState: React.FC<{
  icon?: ReactNode;
  title: string;
  children?: ReactNode;
}> = ({ icon, title, children }) => (
  <div className="lc-card flex flex-col items-center px-6 py-12 text-center">
    <div className="mb-4 flex h-12 w-12 items-center justify-center rounded-2xl bg-teal-50 text-teal-800">
      {icon || <Layers3 className="h-6 w-6" aria-hidden="true" />}
    </div>
    <h2 className="text-lg font-bold text-slate-950">{title}</h2>
    {children && <div className="mt-2 max-w-lg text-sm leading-6 text-slate-600">{children}</div>}
  </div>
);

export const SectionHeading: React.FC<{
  eyebrow?: string;
  title: string;
  description?: string;
  action?: ReactNode;
}> = ({ eyebrow, title, description, action }) => (
  <div className="mb-6 flex flex-col gap-4 sm:flex-row sm:items-end sm:justify-between">
    <div className="max-w-2xl">
      {eyebrow && <div className="lc-eyebrow mb-2">{eyebrow}</div>}
      <h2 className="text-2xl font-bold tracking-tight text-slate-950 sm:text-3xl">{title}</h2>
      {description && <p className="mt-2 text-sm leading-6 text-slate-600 sm:text-base">{description}</p>}
    </div>
    {action && <div className="shrink-0">{action}</div>}
  </div>
);

export const FieldHint: React.FC<{ children: ReactNode }> = ({ children }) => (
  <p className="mt-1.5 text-xs leading-5 text-slate-500">{children}</p>
);

export const Container: React.FC<{ children: ReactNode; className?: string }> = ({
  children,
  className = '',
}) => (
  <div className={`mx-auto w-full max-w-7xl px-4 py-10 sm:px-6 sm:py-12 lg:px-8 ${className}`.trim()}>
    {children}
  </div>
);

export interface Crumb {
  label: string;
  to?: string;
}

export const Breadcrumbs: React.FC<{ items: Crumb[] }> = ({ items }) => (
  <nav aria-label="Breadcrumb" className="mb-6">
    <ol className="flex flex-wrap items-center gap-1.5 text-xs text-slate-500">
      {items.map((item, index) => (
        <li key={`${item.label}-${index}`} className="flex items-center gap-1.5">
          {index > 0 && <span aria-hidden="true">/</span>}
          {item.to ? (
            <Link to={item.to} className="font-medium text-slate-600 hover:text-teal-900">
              {item.label}
            </Link>
          ) : (
            <span className="font-medium text-slate-700" aria-current="page">{item.label}</span>
          )}
        </li>
      ))}
    </ol>
  </nav>
);

interface ConfirmButtonProps {
  onConfirm: () => void;
  label: string;
  confirmLabel?: string;
  className?: string;
  disabled?: boolean;
}

export const ConfirmButton: React.FC<ConfirmButtonProps> = ({
  onConfirm,
  label,
  confirmLabel = 'Confirm',
  className = 'lc-button lc-button-danger',
  disabled = false,
}) => {
  const [confirming, setConfirming] = useState(false);
  if (!confirming) {
    return (
      <button type="button" className={className} disabled={disabled} onClick={() => setConfirming(true)}>
        {label}
      </button>
    );
  }
  return (
    <span className="inline-flex flex-wrap items-center gap-2">
      <button
        type="button"
        className="lc-button lc-button-danger"
        onClick={() => {
          setConfirming(false);
          onConfirm();
        }}
      >
        {confirmLabel}
      </button>
      <button type="button" className="lc-button lc-button-secondary" onClick={() => setConfirming(false)}>
        Cancel
      </button>
    </span>
  );
};

export const AccessDenied: React.FC<{
  title: string;
  message: string;
  children?: ReactNode;
}> = ({ title, message, children }) => (
  <div className="lc-card mx-auto my-16 max-w-xl p-8 text-center">
    <div className="mx-auto mb-4 flex h-12 w-12 items-center justify-center rounded-2xl bg-amber-50 text-amber-900">
      <ShieldAlert className="h-6 w-6" aria-hidden="true" />
    </div>
    <h1 className="text-2xl font-bold tracking-tight text-slate-950">{title}</h1>
    <p className="mt-3 text-sm leading-6 text-slate-600">{message}</p>
    {children && <div className="mt-6">{children}</div>}
  </div>
);

export const MetricTile: React.FC<{
  label: string;
  value: number | string;
  detail?: string;
  icon?: ReactNode;
}> = ({ label, value, detail, icon }) => (
  <div className="lc-card p-5">
    <div className="flex items-start justify-between gap-3">
      <div className="text-xs font-semibold uppercase tracking-[0.1em] text-slate-500">{label}</div>
      {icon && <span className="text-teal-800" aria-hidden="true">{icon}</span>}
    </div>
    <div className="mt-3 text-3xl font-bold tabular-nums tracking-tight text-slate-950">{value}</div>
    {detail && <p className="mt-1 text-xs leading-5 text-slate-500">{detail}</p>}
  </div>
);

type NoticeTone = 'info' | 'success' | 'error';

export const NoticeBanner: React.FC<{ tone?: NoticeTone; children: ReactNode; onDismiss?: () => void }> = ({
  tone = 'info',
  children,
  onDismiss,
}) => {
  const styles: Record<NoticeTone, string> = {
    info: 'border-sky-200 bg-sky-50 text-sky-950',
    success: 'border-emerald-200 bg-emerald-50 text-emerald-950',
    error: 'border-rose-200 bg-rose-50 text-rose-950',
  };
  return (
    <div className={`rounded-lg border p-3 text-sm ${styles[tone]}`} role={tone === 'error' ? 'alert' : 'status'}>
      <div className="flex items-start justify-between gap-3">
        <div className="leading-6">{children}</div>
        {onDismiss && (
          <button type="button" onClick={onDismiss} className="shrink-0 text-xs font-semibold underline underline-offset-2 opacity-80 hover:opacity-100">
            Dismiss
          </button>
        )}
      </div>
    </div>
  );
};

