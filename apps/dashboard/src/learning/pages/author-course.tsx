import React, { FormEvent, useEffect, useState } from 'react';
import {
  ArrowLeft,
  BarChart3,
  BookOpen,
  CheckCircle2,
  Clock3,
  ExternalLink,
  Eye,
  FileQuestion,
  Link2,
  Loader2,
  Pencil,
  Plus,
  Save,
  Send,
  ShieldCheck,
  Trash2,
  Undo2,
  Video,
  XCircle,
} from 'lucide-react';
import { learningApi } from '../api';
import {
  ConfirmButton,
  Container,
  EmptyState,
  ErrorState,
  FieldHint,
  LoadingState,
  MetricTile,
  NoticeBanner,
  ProgressBar,
  StatusBadge,
} from '../components';
import { Markdown } from '../markdown';
import { Link } from '../router';
import { useSeo } from '../seo';
import {
  canReview,
  formatDate,
  formatDateTime,
  formatError,
  levelLabel,
  resourceKindLabel,
  slugify,
  statusLabel,
  useAuth,
  useRemote,
} from '../state';
import {
  AuthorCourse,
  AuthoringPayload,
  CourseAnalytics,
  CourseRevisionDetail,
  LearningLesson,
  LearningModule,
  LearningResource,
  LearningTopic,
  LessonQuiz,
} from '../types';
import { StaffGuard } from './author';

export const AuthorCoursePage: React.FC<{ courseId: string }> = ({ courseId }) => {
  useSeo({
    title: 'Edit course',
    description: 'Authoring workspace for an AdControl Learning course.',
    noindex: true,
  });
  return (
    <StaffGuard next={`/author/courses/${courseId}`}>
      <AuthorCourseContent courseId={courseId} />
    </StaffGuard>
  );
};

const AuthorCourseContent: React.FC<{ courseId: string }> = ({ courseId }) => {
  const { user } = useAuth();
  const authoring = useRemote(() => learningApi.authorCourse(courseId), [courseId]);
  const topics = useRemote(() => learningApi.authorTopics(), []);

  if (authoring.loading) {
    return <Container><LoadingState label="Opening the authoring workspace" /></Container>;
  }
  if (authoring.error || !authoring.data) {
    return (
      <Container>
        <ErrorState
          title="Course could not be opened"
          message={authoring.error ? formatError(authoring.error) : 'This course does not exist or you cannot manage it.'}
          onRetry={authoring.reload}
        />
        <Link to="/author" className="lc-button lc-button-secondary">Back to author studio</Link>
      </Container>
    );
  }

  const { course, modules, resources, revisions = [] } = authoring.data;
  const topic = topics.data?.topics.find((item) => item.id === course.topic_id);
  const draft = course.status === 'draft';
  const publicTopic = topic?.status === 'published';

  return (
    <Container className="pb-20">
      <div className="mb-5 flex flex-wrap items-center justify-between gap-3">
        <Link to="/author" className="inline-flex items-center gap-1 text-sm font-medium text-slate-600 hover:text-teal-900">
          <ArrowLeft className="h-4 w-4" aria-hidden="true" /> All courses
        </Link>
        <div className="flex flex-wrap items-center gap-2">
          {course.status === 'published' && publicTopic && (
            <Link to={`/courses/${course.slug}`} className="lc-button lc-button-secondary">
              <Eye className="h-4 w-4" aria-hidden="true" /> Public page
            </Link>
          )}
          <span className="text-xs text-slate-500">Course ID {course.id.slice(0, 8)}</span>
        </div>
      </div>

      <div className="flex flex-col gap-4 lg:flex-row lg:items-end lg:justify-between">
        <div className="min-w-0">
          <div className="flex flex-wrap items-center gap-2">
            <StatusBadge status={course.status} />
            <span className="text-xs font-medium text-slate-500">{topic?.title || course.topic_title}</span>
            <span className="text-xs text-slate-500">Version {course.version}</span>
          </div>
          <h1 className="mt-2 text-3xl font-bold tracking-tight text-slate-950 sm:text-4xl">{course.title}</h1>
          <p className="mt-1 text-sm text-slate-500">/courses/{course.slug}</p>
          <div className="mt-3 flex flex-wrap gap-x-4 gap-y-1 text-xs text-slate-500">
            <span>{levelLabel(course.level)}</span>
            <span>{course.duration_minutes} min</span>
            <span>{modules.length} modules</span>
            <span>{modules.reduce((sum, module) => sum + module.lessons.length, 0)} lessons</span>
            <span>Updated {formatDate(course.updated_at)}</span>
          </div>
        </div>
        <div className="shrink-0 rounded-xl border border-slate-200 bg-white px-4 py-3 text-xs text-slate-600">
          <div className="font-semibold text-slate-800">Author</div>
          <div className="mt-0.5">{course.author_email || user?.email}</div>
          {course.reviewer_email && (
            <>
              <div className="mt-2 font-semibold text-slate-800">Reviewer</div>
              <div className="mt-0.5">{course.reviewer_email}</div>
            </>
          )}
        </div>
      </div>

      <nav
        aria-label="Authoring sections"
        className="sticky top-16 z-20 mt-6 -mx-4 overflow-x-auto border-y border-slate-200 bg-[#f7f6f2]/95 px-4 py-2 backdrop-blur sm:-mx-6 sm:px-6 lg:-mx-8 lg:px-8"
      >
        <div className="flex min-w-max gap-1">
          {[
            ['metadata', 'Course details'],
            ['publication', 'Publication'],
            ['curriculum', 'Curriculum'],
            ['resources', 'Resources'],
            ['history', 'Versions & review'],
            ['analytics', 'Analytics'],
          ].map(([id, label]) => (
            <a
              key={id}
              href={`#${id}`}
              className="rounded-lg px-3 py-2 text-sm font-semibold text-slate-600 hover:bg-white hover:text-slate-950"
            >
              {label}
            </a>
          ))}
        </div>
      </nav>

      {!draft && (
        <div className="mt-6">
          <NoticeBanner tone="info">
            This course is <strong>{statusLabel(course.status)}</strong>. Content becomes read-only outside
            draft. {canReview(user)
              ? 'Use the publication controls to return it to draft before editing.'
              : 'Ask an editor to return it to draft if changes are needed.'}
          </NoticeBanner>
        </div>
      )}

      <section id="metadata" className="scroll-mt-32 pt-10" aria-labelledby="metadata-heading">
        <h2 id="metadata-heading" className="text-2xl font-bold tracking-tight text-slate-950">Course details</h2>
        <p className="mt-1 text-sm text-slate-600">
          The public promise of the course. Changes are saved to the draft and require review before publication.
        </p>
        <div className="mt-5">
          <MetadataEditor
            course={course}
            topics={topics.data?.topics || []}
            editable={draft}
            onSaved={authoring.reload}
          />
        </div>
      </section>

      <section id="publication" className="scroll-mt-32 pt-12" aria-labelledby="publication-heading">
        <h2 id="publication-heading" className="text-2xl font-bold tracking-tight text-slate-950">Publication</h2>
        <p className="mt-1 text-sm text-slate-600">
          Submission, editorial review, and publication are separate steps. Instructors can withdraw a
          submission; editors approve and publish.
        </p>
        <div className="mt-5">
          <PublicationPanel course={course} modules={modules} onChanged={authoring.reload} />
        </div>
      </section>

      <section id="curriculum" className="scroll-mt-32 pt-12" aria-labelledby="curriculum-heading">
        <div className="flex flex-wrap items-end justify-between gap-3">
          <div>
            <h2 id="curriculum-heading" className="text-2xl font-bold tracking-tight text-slate-950">Curriculum</h2>
            <p className="mt-1 text-sm text-slate-600">
              Modules keep lessons in a sensible order. Every active module needs a lesson, and every active
              lesson needs written content, before the course can be published.
            </p>
          </div>
          <span className="text-xs font-semibold text-slate-500">
            {modules.length} modules · {modules.reduce((sum, module) => sum + module.lessons.length, 0)} lessons
          </span>
        </div>
        <div className="mt-5 space-y-5">
          {modules.map((module, index) => (
            <ModuleCard
              key={module.id}
              module={module}
              index={index}
              draft={draft}
              onChanged={authoring.reload}
            />
          ))}
          {modules.length === 0 && (
            <EmptyState title="Start the curriculum with a module">
              <p>A module groups lessons into a coherent chapter. Add one, then add lessons inside it.</p>
            </EmptyState>
          )}
          {draft ? (
            <NewModuleForm
              courseId={course.id}
              nextSort={Math.max(1, modules.length + 1)}
              onCreated={authoring.reload}
            />
          ) : (
            <p className="text-sm text-slate-500">Move the course to draft to add or change curriculum.</p>
          )}
        </div>
      </section>

      <section id="resources" className="scroll-mt-32 pt-12" aria-labelledby="resources-heading">
        <h2 id="resources-heading" className="text-2xl font-bold tracking-tight text-slate-950">Resources</h2>
        <p className="mt-1 text-sm text-slate-600">
          Optional downloads and tools attached to the whole course or a specific lesson. Only HTTPS links
          are accepted.
        </p>
        <div className="mt-5">
          <ResourcesSection
            courseId={course.id}
            resources={resources}
            modules={modules}
            draft={draft}
            onChanged={authoring.reload}
          />
        </div>
      </section>

      <section id="history" className="scroll-mt-32 pt-12" aria-labelledby="history-heading">
        <h2 id="history-heading" className="text-2xl font-bold tracking-tight text-slate-950">Versions and review</h2>
        <p className="mt-1 text-sm text-slate-600">
          Every publication stores an immutable snapshot. Freshness dates help editors decide when a course
          needs review again.
        </p>
        <div className="mt-5 grid gap-5 lg:grid-cols-[minmax(0,1fr)_18rem]">
          <RevisionHistory course={course} revisions={revisions} />
          <aside className="lc-card h-fit p-5">
            <h3 className="font-bold text-slate-950">Freshness</h3>
            <dl className="mt-3 space-y-3 text-sm">
              <div>
                <dt className="text-xs font-medium text-slate-500">First published</dt>
                <dd className="mt-0.5 text-slate-800">{formatDate(course.published_at)}</dd>
              </div>
              <div>
                <dt className="text-xs font-medium text-slate-500">Last reviewed</dt>
                <dd className="mt-0.5 text-slate-800">{formatDate(course.last_reviewed_at)}</dd>
              </div>
              <div>
                <dt className="text-xs font-medium text-slate-500">Next review due</dt>
                <dd className="mt-0.5 text-slate-800">{formatDate(course.next_review_due_at)}</dd>
              </div>
            </dl>
            <p className="mt-4 border-t border-slate-100 pt-3 text-xs leading-5 text-slate-500">
              Review dates are set by the publication workflow; they are not editable directly.
            </p>
          </aside>
        </div>
      </section>

      <section id="analytics" className="scroll-mt-32 pt-12" aria-labelledby="analytics-heading">
        <h2 id="analytics-heading" className="text-2xl font-bold tracking-tight text-slate-950">Course analytics</h2>
        <p className="mt-1 text-sm text-slate-600">
          Aggregate only. No learner names, emails, or individual activity are shown.
        </p>
        <div className="mt-5">
          <CourseAnalyticsPanel courseId={course.id} />
        </div>
      </section>
    </Container>
  );
};

const MetadataEditor: React.FC<{
  course: AuthorCourse;
  topics: LearningTopic[];
  editable: boolean;
  onSaved: () => void;
}> = ({ course, topics, editable, onSaved }) => {
  const [title, setTitle] = useState(course.title);
  const [slug, setSlug] = useState(course.slug);
  const [summary, setSummary] = useState(course.summary);
  const [description, setDescription] = useState(course.description_md);
  const [level, setLevel] = useState(course.level);
  const [duration, setDuration] = useState(course.duration_minutes);
  const [topicId, setTopicId] = useState(course.topic_id);
  const [outcomes, setOutcomes] = useState(course.outcomes.join('\n'));
  const [preview, setPreview] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);

  useEffect(() => {
    setTitle(course.title);
    setSlug(course.slug);
    setSummary(course.summary);
    setDescription(course.description_md);
    setLevel(course.level);
    setDuration(course.duration_minutes);
    setTopicId(course.topic_id);
    setOutcomes(course.outcomes.join('\n'));
  }, [course]);

  const save = async (event: FormEvent) => {
    event.preventDefault();
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      await learningApi.updateCourse(course.id, {
        title,
        slug,
        summary,
        description_md: description,
        level,
        duration_minutes: Number(duration),
        topic_id: topicId,
        outcomes: outcomes.split('\n').map((item) => item.trim()).filter(Boolean),
      });
      setNotice('Course details saved.');
      onSaved();
    } catch (err) {
      setError(formatError(err));
    } finally {
      setBusy(false);
    }
  };

  return (
    <form onSubmit={save} className="lc-card space-y-5 p-5 sm:p-6">
      {error && <NoticeBanner tone="error">{error}</NoticeBanner>}
      {notice && <NoticeBanner tone="success" onDismiss={() => setNotice(null)}>{notice}</NoticeBanner>}
      {!editable && (
        <NoticeBanner tone="info">
          Details are read-only while the course is outside draft.
        </NoticeBanner>
      )}

      <div className="grid gap-5 sm:grid-cols-2">
        <div>
          <label htmlFor="edit-course-title" className="lc-label">Title</label>
          <input
            id="edit-course-title"
            value={title}
            onChange={(event) => setTitle(event.target.value)}
            disabled={!editable}
            required
            minLength={3}
            maxLength={160}
            className="lc-input disabled:bg-slate-50 disabled:text-slate-600"
          />
        </div>
        <div>
          <label htmlFor="edit-course-slug" className="lc-label">Slug</label>
          <input
            id="edit-course-slug"
            value={slug}
            onChange={(event) => setSlug(slugify(event.target.value))}
            disabled={!editable}
            required
            minLength={2}
            maxLength={120}
            pattern="[a-z0-9]+(?:-[a-z0-9]+)*"
            className="lc-input font-mono text-xs disabled:bg-slate-50 disabled:text-slate-600"
          />
        </div>
      </div>

      <div className="grid gap-5 sm:grid-cols-2">
        <div>
          <label htmlFor="edit-course-topic" className="lc-label">Topic</label>
          <select
            id="edit-course-topic"
            value={topicId}
            onChange={(event) => setTopicId(event.target.value)}
            disabled={!editable}
            className="lc-input disabled:bg-slate-50 disabled:text-slate-600"
          >
            {topics.map((topic) => (
              <option key={topic.id} value={topic.id}>{topic.title}</option>
            ))}
          </select>
          {topics.find((topic) => topic.id === topicId)?.status !== 'published' && (
            <FieldHint>This topic is not published, so the course will remain out of the public catalog even when published.</FieldHint>
          )}
        </div>
        <div>
          <label htmlFor="edit-course-level" className="lc-label">Level</label>
          <select
            id="edit-course-level"
            value={level}
            onChange={(event) => setLevel(event.target.value)}
            disabled={!editable}
            className="lc-input disabled:bg-slate-50 disabled:text-slate-600"
          >
            <option value="beginner">Beginner</option>
            <option value="intermediate">Intermediate</option>
            <option value="advanced">Advanced</option>
          </select>
        </div>
      </div>

      <div>
        <label htmlFor="edit-course-duration" className="lc-label">Estimated duration (minutes)</label>
        <input
          id="edit-course-duration"
          type="number"
          min={1}
          max={100000}
          value={duration}
          onChange={(event) => setDuration(Number(event.target.value))}
          disabled={!editable}
          className="lc-input sm:max-w-[12rem] disabled:bg-slate-50 disabled:text-slate-600"
        />
      </div>

      <div>
        <label htmlFor="edit-course-summary" className="lc-label">Short summary</label>
        <textarea
          id="edit-course-summary"
          rows={3}
          value={summary}
          onChange={(event) => setSummary(event.target.value)}
          disabled={!editable}
          required
          minLength={10}
          maxLength={500}
          className="lc-input disabled:bg-slate-50 disabled:text-slate-600"
        />
      </div>

      <div>
        <div className="flex items-center justify-between gap-3">
          <label htmlFor="edit-course-description" className="lc-label mb-0">Course description</label>
          <button type="button" onClick={() => setPreview((value) => !value)} className="text-xs font-semibold text-teal-800 hover:text-teal-950">
            {preview ? 'Edit Markdown' : 'Preview'}
          </button>
        </div>
        {preview ? (
          <div className="mt-2 rounded-lg border border-slate-200 bg-white p-4">
            <Markdown source={description} emptyMessage="Nothing to preview yet." />
          </div>
        ) : (
          <textarea
            id="edit-course-description"
            rows={12}
            value={description}
            onChange={(event) => setDescription(event.target.value)}
            disabled={!editable}
            className="lc-input mt-2 font-mono text-xs leading-6 disabled:bg-slate-50 disabled:text-slate-600"
          />
        )}
        <FieldHint>
          Safe Markdown. Raw HTML, scripts, iframes, and unsafe URLs are rejected by the server and rendered
          as literal text.
        </FieldHint>
      </div>

      <div>
        <label htmlFor="edit-course-outcomes" className="lc-label">Learning outcomes</label>
        <textarea
          id="edit-course-outcomes"
          rows={5}
          value={outcomes}
          onChange={(event) => setOutcomes(event.target.value)}
          disabled={!editable}
          className="lc-input disabled:bg-slate-50 disabled:text-slate-600"
        />
        <FieldHint>One plain-text outcome per line, up to 20.</FieldHint>
      </div>

      {editable && (
        <div className="flex items-center gap-3 border-t border-slate-100 pt-5">
          <button type="submit" disabled={busy} className="lc-button lc-button-primary">
            {busy ? <Loader2 className="h-4 w-4 animate-spin" aria-hidden="true" /> : <Save className="h-4 w-4" aria-hidden="true" />}
            {busy ? 'Saving…' : 'Save details'}
          </button>
          <span className="text-xs text-slate-500">Saving does not publish the course.</span>
        </div>
      )}
    </form>
  );
};

const PublicationPanel: React.FC<{
  course: AuthorCourse;
  modules: LearningModule[];
  onChanged: () => void;
}> = ({ course, modules, onChanged }) => {
  const { user } = useAuth();
  const [note, setNote] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const editor = canReview(user);

  const totalLessons = modules.reduce((sum, module) => sum + module.lessons.length, 0);
  const emptyLessons = modules.reduce(
    (sum, module) => sum + module.lessons.filter((lesson) => !lesson.content_md.trim()).length,
    0,
  );
  const emptyModules = modules.filter((module) => module.lessons.length === 0).length;
  const checks = [
    { ok: modules.length > 0, label: 'At least one module' },
    { ok: totalLessons > 0, label: 'At least one active lesson' },
    { ok: emptyModules === 0, label: 'Every module has a lesson' },
    { ok: emptyLessons === 0, label: 'Every lesson has written content' },
  ];
  const ready = checks.every((check) => check.ok);

  const change = async (target: string) => {
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      await learningApi.setCourseStatus(course.id, target, note);
      setNotice(`Course status changed to ${statusLabel(target)}.`);
      setNote('');
      onChanged();
    } catch (err) {
      setError(formatError(err));
    } finally {
      setBusy(false);
    }
  };

  const primaryAction = (() => {
    if (course.status === 'draft') return { target: 'in_review', label: 'Submit for review', icon: Send };
    if (course.status === 'in_review' && editor) return { target: 'approved', label: 'Approve for publication', icon: ShieldCheck };
    if (course.status === 'approved' && editor) return { target: 'published', label: 'Publish', icon: Send };
    return null;
  })();

  const secondary: Array<{ target: string; label: string; icon?: typeof Undo2 }> = [];
  if (course.status === 'in_review') secondary.push({ target: 'draft', label: 'Return to draft', icon: Undo2 });
  if (course.status === 'approved' && editor) secondary.push({ target: 'draft', label: 'Return to draft', icon: Undo2 });
  if (course.status === 'published' && editor) secondary.push({ target: 'draft', label: 'Unpublish to draft', icon: Undo2 });
  if (course.status === 'archived' && editor) secondary.push({ target: 'draft', label: 'Restore to draft', icon: Undo2 });

  const canArchive = course.status === 'draft' || (editor && course.status !== 'archived');

  return (
    <div className="grid gap-5 lg:grid-cols-[minmax(0,1fr)_20rem]">
      <div className="lc-card p-5 sm:p-6">
        {error && <div className="mb-4"><NoticeBanner tone="error">{error}</NoticeBanner></div>}
        {notice && <div className="mb-4"><NoticeBanner tone="success" onDismiss={() => setNotice(null)}>{notice}</NoticeBanner></div>}

        <div>
          <label htmlFor="review-note" className="lc-label">Review note (optional)</label>
          <textarea
            id="review-note"
            rows={3}
            maxLength={500}
            value={note}
            onChange={(event) => setNote(event.target.value)}
            disabled={busy || course.status === 'published'}
            className="lc-input disabled:bg-slate-50 disabled:text-slate-600"
            placeholder={
              course.status === 'draft'
                ? 'What should the editor focus on?'
                : course.status === 'in_review'
                  ? 'Decision or change-request note'
                  : 'Publication note (stored as the version note)'
            }
          />
          <FieldHint>Plain text, up to 500 characters. Included in the audit trail and, on publish, the version note.</FieldHint>
        </div>

        <div className="mt-5 flex flex-wrap items-center gap-2 border-t border-slate-100 pt-5">
          {primaryAction && (
            <button
              type="button"
              disabled={busy || !ready}
              onClick={() => change(primaryAction.target)}
              className="lc-button lc-button-primary"
            >
              <primaryAction.icon className="h-4 w-4" aria-hidden="true" />
              {busy ? 'Working…' : primaryAction.label}
            </button>
          )}
          {course.status === 'draft' && !ready && (
            <span className="text-xs text-amber-800">Complete the readiness checks before submitting for review.</span>
          )}
          {secondary.map((action) => (
            <button
              key={action.target}
              type="button"
              disabled={busy}
              onClick={() => change(action.target)}
              className="lc-button lc-button-secondary"
            >
              {action.icon && <action.icon className="h-4 w-4" aria-hidden="true" />}
              {action.label}
            </button>
          ))}
          {canArchive && (
            <ConfirmButton
              label={course.status === 'draft' ? 'Archive draft' : 'Archive course'}
              confirmLabel="Confirm archive"
              disabled={busy}
              onConfirm={() => change('archived')}
            />
          )}
        </div>

        {course.status !== 'draft' && course.status !== 'in_review' && !editor && (
          <p className="mt-4 text-xs leading-5 text-slate-500">
            This course is outside draft. An editor can unpublish it if changes are required.
          </p>
        )}
      </div>

      <aside className="lc-card h-fit p-5">
        <h3 className="font-bold text-slate-950">Readiness</h3>
        <ul className="mt-3 space-y-2.5">
          {checks.map((check) => (
            <li key={check.label} className="flex items-start gap-2 text-sm">
              {check.ok ? (
                <CheckCircle2 className="mt-0.5 h-4 w-4 shrink-0 text-emerald-700" aria-hidden="true" />
              ) : (
                <XCircle className="mt-0.5 h-4 w-4 shrink-0 text-rose-700" aria-hidden="true" />
              )}
              <span className={check.ok ? 'text-slate-700' : 'font-medium text-rose-900'}>{check.label}</span>
            </li>
          ))}
        </ul>
        <p className="mt-4 border-t border-slate-100 pt-3 text-xs leading-5 text-slate-500">
          Publication also requires the assigned topic to be published for the course to appear publicly.
        </p>
      </aside>
    </div>
  );
};

const ModuleCard: React.FC<{
  module: LearningModule;
  index: number;
  draft: boolean;
  onChanged: () => void;
}> = ({ module, index, draft, onChanged }) => {
  const [editing, setEditing] = useState(false);
  const [adding, setAdding] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const archive = async () => {
    setBusy(true);
    setError(null);
    try {
      await learningApi.archiveModule(module.id);
      onChanged();
    } catch (err) {
      setError(formatError(err));
      setBusy(false);
    }
  };

  return (
    <section className="lc-card overflow-hidden" aria-labelledby={`module-heading-${module.id}`}>
      <div className="flex flex-wrap items-start justify-between gap-3 border-b border-slate-100 bg-slate-50/70 px-5 py-4">
        <div className="min-w-0">
          <div className="text-xs font-semibold uppercase tracking-wide text-slate-500">Module {index + 1}</div>
          <h3 id={`module-heading-${module.id}`} className="mt-0.5 text-lg font-bold text-slate-950">{module.title}</h3>
          {module.summary && <p className="mt-0.5 text-sm text-slate-600">{module.summary}</p>}
          <p className="mt-1 text-xs text-slate-500">{module.lessons.length} lessons · sort {module.sort_order}</p>
        </div>
        {draft && (
          <div className="flex flex-wrap gap-2">
            <button type="button" className="lc-button lc-button-secondary" onClick={() => setEditing((value) => !value)}>
              <Pencil className="h-4 w-4" aria-hidden="true" /> {editing ? 'Close' : 'Edit'}
            </button>
            <ConfirmButton
              label="Archive"
              confirmLabel="Confirm archive"
              disabled={busy}
              onConfirm={archive}
            />
          </div>
        )}
      </div>

      {editing && draft && (
        <ModuleEditForm
          module={module}
          onCancel={() => setEditing(false)}
          onSaved={() => {
            setEditing(false);
            onChanged();
          }}
        />
      )}

      {error && <div className="px-5 pt-4"><NoticeBanner tone="error">{error}</NoticeBanner></div>}

      <div className="divide-y divide-slate-100">
        {module.lessons.map((lesson) => (
          <LessonRow key={lesson.id} lesson={lesson} draft={draft} onChanged={onChanged} />
        ))}
        {module.lessons.length === 0 && (
          <p className="px-5 py-4 text-sm text-slate-500">No active lessons in this module yet.</p>
        )}
      </div>

      {draft && (
        <div className="border-t border-slate-100 p-4">
          {adding ? (
            <NewLessonForm
              courseId={module.course_id}
              moduleId={module.id}
              nextSort={Math.max(1, module.lessons.length + 1)}
              onCancel={() => setAdding(false)}
              onCreated={() => {
                setAdding(false);
                onChanged();
              }}
            />
          ) : (
            <button type="button" className="lc-button lc-button-secondary" onClick={() => setAdding(true)}>
              <Plus className="h-4 w-4" aria-hidden="true" /> Add lesson
            </button>
          )}
        </div>
      )}
    </section>
  );
};

const ModuleEditForm: React.FC<{
  module: LearningModule;
  onCancel: () => void;
  onSaved: () => void;
}> = ({ module, onCancel, onSaved }) => {
  const [title, setTitle] = useState(module.title);
  const [summary, setSummary] = useState(module.summary);
  const [sortOrder, setSortOrder] = useState(module.sort_order);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const save = async (event: FormEvent) => {
    event.preventDefault();
    setBusy(true);
    setError(null);
    try {
      await learningApi.updateModule(module.id, {
        title,
        summary,
        sort_order: Number(sortOrder),
      });
      onSaved();
    } catch (err) {
      setError(formatError(err));
      setBusy(false);
    }
  };

  return (
    <form onSubmit={save} className="border-b border-slate-100 bg-white p-5">
      {error && <div className="mb-4"><NoticeBanner tone="error">{error}</NoticeBanner></div>}
      <div className="grid gap-4 sm:grid-cols-[minmax(0,1fr)_8rem]">
        <div>
          <label htmlFor={`module-title-${module.id}`} className="lc-label">Module title</label>
          <input id={`module-title-${module.id}`} value={title} onChange={(event) => setTitle(event.target.value)} required minLength={3} maxLength={160} className="lc-input" />
        </div>
        <div>
          <label htmlFor={`module-sort-${module.id}`} className="lc-label">Sort order</label>
          <input id={`module-sort-${module.id}`} type="number" min={0} max={100000} value={sortOrder} onChange={(event) => setSortOrder(Number(event.target.value))} className="lc-input" />
        </div>
      </div>
      <div className="mt-4">
        <label htmlFor={`module-summary-${module.id}`} className="lc-label">Summary</label>
        <textarea id={`module-summary-${module.id}`} rows={2} maxLength={500} value={summary} onChange={(event) => setSummary(event.target.value)} className="lc-input" />
      </div>
      <div className="mt-4 flex gap-2">
        <button type="submit" disabled={busy} className="lc-button lc-button-primary">{busy ? 'Saving…' : 'Save module'}</button>
        <button type="button" onClick={onCancel} className="lc-button lc-button-secondary">Cancel</button>
      </div>
    </form>
  );
};

const NewModuleForm: React.FC<{
  courseId: string;
  nextSort: number;
  onCreated: () => void;
}> = ({ courseId, nextSort, onCreated }) => {
  const [open, setOpen] = useState(false);
  const [title, setTitle] = useState('');
  const [summary, setSummary] = useState('');
  const [sortOrder, setSortOrder] = useState(nextSort);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const create = async (event: FormEvent) => {
    event.preventDefault();
    setBusy(true);
    setError(null);
    try {
      await learningApi.createModule(courseId, { title, summary, sort_order: Number(sortOrder) });
      setTitle('');
      setSummary('');
      setOpen(false);
      onCreated();
    } catch (err) {
      setError(formatError(err));
    } finally {
      setBusy(false);
    }
  };

  if (!open) {
    return (
      <button type="button" onClick={() => setOpen(true)} className="lc-button lc-button-primary">
        <Plus className="h-4 w-4" aria-hidden="true" /> Add module
      </button>
    );
  }

  return (
    <form onSubmit={create} className="lc-card p-5">
      <h3 className="font-bold text-slate-950">New module</h3>
      {error && <div className="mt-3"><NoticeBanner tone="error">{error}</NoticeBanner></div>}
      <div className="mt-4 grid gap-4 sm:grid-cols-[minmax(0,1fr)_8rem]">
        <div>
          <label htmlFor="new-module-title" className="lc-label">Module title</label>
          <input id="new-module-title" value={title} onChange={(event) => setTitle(event.target.value)} required minLength={3} maxLength={160} className="lc-input" placeholder="e.g. Foundations of search intent" />
        </div>
        <div>
          <label htmlFor="new-module-sort" className="lc-label">Sort order</label>
          <input id="new-module-sort" type="number" min={0} max={100000} value={sortOrder} onChange={(event) => setSortOrder(Number(event.target.value))} className="lc-input" />
        </div>
      </div>
      <div className="mt-4">
        <label htmlFor="new-module-summary" className="lc-label">Summary</label>
        <textarea id="new-module-summary" rows={2} maxLength={500} value={summary} onChange={(event) => setSummary(event.target.value)} className="lc-input" />
      </div>
      <div className="mt-4 flex gap-2">
        <button type="submit" disabled={busy} className="lc-button lc-button-primary">{busy ? 'Creating…' : 'Create module'}</button>
        <button type="button" onClick={() => setOpen(false)} className="lc-button lc-button-secondary">Cancel</button>
      </div>
    </form>
  );
};

const LessonRow: React.FC<{
  lesson: LearningLesson;
  draft: boolean;
  onChanged: () => void;
}> = ({ lesson, draft, onChanged }) => {
  const [editing, setEditing] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const archive = async () => {
    setBusy(true);
    setError(null);
    try {
      await learningApi.archiveLesson(lesson.id);
      onChanged();
    } catch (err) {
      setError(formatError(err));
      setBusy(false);
    }
  };

  return (
    <div className="p-5">
      <div className="flex flex-wrap items-start justify-between gap-3">
        <div className="min-w-0">
          <h4 className="font-semibold text-slate-950">{lesson.title}</h4>
          <div className="mt-1 flex flex-wrap gap-x-3 gap-y-1 text-xs text-slate-500">
            <span className="inline-flex items-center gap-1"><Clock3 className="h-3.5 w-3.5" aria-hidden="true" />{lesson.estimated_minutes} min</span>
            <span>sort {lesson.sort_order}</span>
            {lesson.is_preview && <span className="font-semibold text-teal-800">Public preview</span>}
            {lesson.video_provider && <span className="inline-flex items-center gap-1"><Video className="h-3.5 w-3.5" aria-hidden="true" />{lesson.video_provider}</span>}
            {lesson.quiz && <span className="inline-flex items-center gap-1"><FileQuestion className="h-3.5 w-3.5" aria-hidden="true" />Quiz</span>}
            {!lesson.content_md.trim() && <span className="font-semibold text-amber-800">No written content</span>}
          </div>
        </div>
        {draft && (
          <div className="flex flex-wrap gap-2">
            <button type="button" className="lc-button lc-button-secondary" onClick={() => setEditing((value) => !value)} aria-expanded={editing}>
              <Pencil className="h-4 w-4" aria-hidden="true" /> {editing ? 'Close editor' : 'Edit lesson'}
            </button>
            <ConfirmButton
              label="Archive"
              confirmLabel="Confirm archive"
              disabled={busy}
              onConfirm={archive}
            />
          </div>
        )}
      </div>

      {error && <div className="mt-3"><NoticeBanner tone="error">{error}</NoticeBanner></div>}

      {editing && draft && (
        <div className="mt-5 space-y-5 border-t border-slate-100 pt-5">
          <LessonEditor lesson={lesson} onSaved={onChanged} />
          <QuizEditor key={lesson.quiz?.id || 'new'} lessonId={lesson.id} quiz={lesson.quiz || null} onSaved={onChanged} />
        </div>
      )}
    </div>
  );
};

const LessonEditor: React.FC<{ lesson: LearningLesson; onSaved: () => void }> = ({ lesson, onSaved }) => {
  const [title, setTitle] = useState(lesson.title);
  const [slug, setSlug] = useState(lesson.slug);
  const [content, setContent] = useState(lesson.content_md);
  const [minutes, setMinutes] = useState(lesson.estimated_minutes);
  const [sortOrder, setSortOrder] = useState(lesson.sort_order);
  const [isPreview, setIsPreview] = useState(lesson.is_preview);
  const [provider, setProvider] = useState(lesson.video_provider || '');
  const [videoId, setVideoId] = useState(lesson.video_id || '');
  const [preview, setPreview] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);

  const save = async (event: FormEvent) => {
    event.preventDefault();
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      await learningApi.updateLesson(lesson.id, {
        title,
        slug,
        content_md: content,
        estimated_minutes: Number(minutes),
        sort_order: Number(sortOrder),
        is_preview: isPreview,
        video_provider: provider,
        video_id: videoId,
      });
      setNotice('Lesson saved.');
      onSaved();
    } catch (err) {
      setError(formatError(err));
    } finally {
      setBusy(false);
    }
  };

  return (
    <form onSubmit={save} className="rounded-xl border border-slate-200 bg-slate-50/50 p-4 sm:p-5">
      <h5 className="text-sm font-bold uppercase tracking-wide text-slate-600">Lesson content</h5>
      {error && <div className="mt-3"><NoticeBanner tone="error">{error}</NoticeBanner></div>}
      {notice && <div className="mt-3"><NoticeBanner tone="success" onDismiss={() => setNotice(null)}>{notice}</NoticeBanner></div>}
      <div className="mt-4 grid gap-4 sm:grid-cols-2">
        <div>
          <label htmlFor={`lesson-title-${lesson.id}`} className="lc-label">Title</label>
          <input id={`lesson-title-${lesson.id}`} value={title} onChange={(event) => setTitle(event.target.value)} required minLength={3} maxLength={160} className="lc-input" />
        </div>
        <div>
          <label htmlFor={`lesson-slug-${lesson.id}`} className="lc-label">Slug</label>
          <input id={`lesson-slug-${lesson.id}`} value={slug} onChange={(event) => setSlug(slugify(event.target.value))} required minLength={2} maxLength={120} pattern="[a-z0-9]+(?:-[a-z0-9]+)*" className="lc-input font-mono text-xs" />
        </div>
      </div>
      <div className="mt-4 grid gap-4 sm:grid-cols-3">
        <div>
          <label htmlFor={`lesson-minutes-${lesson.id}`} className="lc-label">Minutes</label>
          <input id={`lesson-minutes-${lesson.id}`} type="number" min={1} max={100000} value={minutes} onChange={(event) => setMinutes(Number(event.target.value))} className="lc-input" />
        </div>
        <div>
          <label htmlFor={`lesson-sort-${lesson.id}`} className="lc-label">Sort order</label>
          <input id={`lesson-sort-${lesson.id}`} type="number" min={0} max={100000} value={sortOrder} onChange={(event) => setSortOrder(Number(event.target.value))} className="lc-input" />
        </div>
        <div className="flex items-end pb-2">
          <label className="flex items-center gap-2 text-sm text-slate-700">
            <input type="checkbox" checked={isPreview} onChange={(event) => setIsPreview(event.target.checked)} className="h-4 w-4 rounded border-slate-300 text-teal-700 focus:ring-teal-700" />
            Public preview lesson
          </label>
        </div>
      </div>
      <div className="mt-4 grid gap-4 sm:grid-cols-2">
        <div>
          <label htmlFor={`lesson-provider-${lesson.id}`} className="lc-label">Video provider</label>
          <select id={`lesson-provider-${lesson.id}`} value={provider} onChange={(event) => setProvider(event.target.value)} className="lc-input">
            <option value="">No video</option>
            <option value="youtube">YouTube (privacy-enhanced)</option>
            <option value="vimeo">Vimeo</option>
          </select>
        </div>
        <div>
          <label htmlFor={`lesson-video-${lesson.id}`} className="lc-label">Video ID</label>
          <input id={`lesson-video-${lesson.id}`} value={videoId} onChange={(event) => setVideoId(event.target.value)} disabled={!provider} maxLength={64} className="lc-input font-mono text-xs disabled:bg-slate-100" placeholder="e.g. dQw4w9WgXcQ" />
          <FieldHint>Only the provider ID, not a full URL. Approved hosts only.</FieldHint>
        </div>
      </div>
      <div className="mt-4">
        <div className="flex items-center justify-between gap-3">
          <label htmlFor={`lesson-content-${lesson.id}`} className="lc-label mb-0">Lesson content</label>
          <button type="button" onClick={() => setPreview((value) => !value)} className="text-xs font-semibold text-teal-800 hover:text-teal-950">
            {preview ? 'Edit Markdown' : 'Preview'}
          </button>
        </div>
        {preview ? (
          <div className="mt-2 rounded-lg border border-slate-200 bg-white p-4">
            <Markdown source={content} emptyMessage="Nothing to preview yet." />
          </div>
        ) : (
          <textarea
            id={`lesson-content-${lesson.id}`}
            rows={14}
            value={content}
            onChange={(event) => setContent(event.target.value)}
            className="lc-input mt-2 font-mono text-xs leading-6"
            placeholder="Write the lesson in Markdown. Include a practical exercise and a note about what should not be changed automatically."
          />
        )}
        <FieldHint>
          Safe Markdown only. Raw HTML and scripts are rejected. Keep exercises non-destructive: never instruct
          learners to make automatic changes to a production campaign.
        </FieldHint>
      </div>
      <div className="mt-4">
        <button type="submit" disabled={busy} className="lc-button lc-button-primary">
          {busy ? <Loader2 className="h-4 w-4 animate-spin" aria-hidden="true" /> : <Save className="h-4 w-4" aria-hidden="true" />}
          {busy ? 'Saving…' : 'Save lesson'}
        </button>
      </div>
    </form>
  );
};

interface OptionDraft {
  key: string;
  text: string;
  is_correct: boolean;
}

interface QuestionDraft {
  key: string;
  prompt: string;
  explanation: string;
  options: OptionDraft[];
}

let draftCounter = 0;
const draftKey = (prefix: string) => `${prefix}-${Date.now()}-${draftCounter++}`;

function draftOptions(): OptionDraft[] {
  return [
    { key: draftKey('o'), text: '', is_correct: true },
    { key: draftKey('o'), text: '', is_correct: false },
  ];
}

function fromQuiz(quiz: LessonQuiz | null): QuestionDraft[] {
  if (!quiz || quiz.questions.length === 0) {
    return [{ key: draftKey('q'), prompt: '', explanation: '', options: draftOptions() }];
  }
  return quiz.questions.map((question) => ({
    key: question.id,
    prompt: question.prompt,
    explanation: question.explanation || '',
    options: question.options.map((option) => ({
      key: option.id,
      text: option.text,
      is_correct: !!option.is_correct,
    })),
  }));
}

const QuizEditor: React.FC<{
  lessonId: string;
  quiz: LessonQuiz | null;
  onSaved: () => void;
}> = ({ lessonId, quiz, onSaved }) => {
  const [title, setTitle] = useState(quiz?.title || 'Knowledge check');
  const [passingScore, setPassingScore] = useState(quiz?.passing_score ?? 70);
  const [questions, setQuestions] = useState<QuestionDraft[]>(() => fromQuiz(quiz));
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);

  const updateQuestion = (questionKey: string, patch: Partial<QuestionDraft>) => {
    setQuestions((current) => current.map((question) => (
      question.key === questionKey ? { ...question, ...patch } : question
    )));
  };

  const updateOption = (questionKey: string, optionKey: string, patch: Partial<OptionDraft>) => {
    setQuestions((current) => current.map((question) => {
      if (question.key !== questionKey) return question;
      return {
        ...question,
        options: question.options.map((option) => (
          option.key === optionKey ? { ...option, ...patch } : option
        )),
      };
    }));
  };

  const setCorrect = (questionKey: string, optionKey: string) => {
    setQuestions((current) => current.map((question) => {
      if (question.key !== questionKey) return question;
      return {
        ...question,
        options: question.options.map((option) => ({
          ...option,
          is_correct: option.key === optionKey,
        })),
      };
    }));
  };

  const addQuestion = () => {
    setQuestions((current) => [
      ...current,
      { key: draftKey('q'), prompt: '', explanation: '', options: draftOptions() },
    ]);
  };

  const removeQuestion = (questionKey: string) => {
    setQuestions((current) => current.filter((question) => question.key !== questionKey));
  };

  const addOption = (questionKey: string) => {
    setQuestions((current) => current.map((question) => (
      question.key === questionKey
        ? { ...question, options: [...question.options, { key: draftKey('o'), text: '', is_correct: false }] }
        : question
    )));
  };

  const removeOption = (questionKey: string, optionKey: string) => {
    setQuestions((current) => current.map((question) => {
      if (question.key !== questionKey || question.options.length <= 2) return question;
      const remaining = question.options.filter((option) => option.key !== optionKey);
      if (!remaining.some((option) => option.is_correct) && remaining[0]) {
        remaining[0] = { ...remaining[0], is_correct: true };
      }
      return { ...question, options: remaining };
    }));
  };

  const validate = (): string | null => {
    if (title.trim().length < 3) return 'Quiz title must be at least 3 characters.';
    if (passingScore < 0 || passingScore > 100) return 'Passing score must be between 0 and 100.';
    if (questions.length === 0) return 'Add at least one question.';
    if (questions.length > 50) return 'A quiz can have at most 50 questions.';
    for (let index = 0; index < questions.length; index += 1) {
      const question = questions[index];
      if (question.prompt.trim().length < 3) return `Question ${index + 1} needs a prompt.`;
      if (question.options.length < 2 || question.options.length > 10) {
        return `Question ${index + 1} needs 2–10 options.`;
      }
      if (question.options.some((option) => !option.text.trim())) {
        return `Question ${index + 1} has an empty option.`;
      }
      if (question.options.filter((option) => option.is_correct).length !== 1) {
        return `Question ${index + 1} must have exactly one correct option.`;
      }
    }
    return null;
  };

  const save = async (event: FormEvent) => {
    event.preventDefault();
    const problem = validate();
    if (problem) {
      setError(problem);
      return;
    }
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      await learningApi.saveQuiz(lessonId, {
        title,
        passing_score: Number(passingScore),
        questions: questions.map((question) => ({
          prompt: question.prompt,
          explanation: question.explanation,
          options: question.options.map((option) => ({
            text: option.text,
            is_correct: option.is_correct,
          })),
        })),
      });
      setNotice('Knowledge check saved.');
      onSaved();
    } catch (err) {
      setError(formatError(err));
    } finally {
      setBusy(false);
    }
  };

  const remove = async () => {
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      await learningApi.deleteQuiz(lessonId);
      setTitle('Knowledge check');
      setPassingScore(70);
      setQuestions(fromQuiz(null));
      setNotice('Knowledge check deleted.');
      onSaved();
    } catch (err) {
      setError(formatError(err));
    } finally {
      setBusy(false);
    }
  };

  return (
    <form onSubmit={save} className="rounded-xl border border-slate-200 bg-slate-50/50 p-4 sm:p-5">
      <div className="flex flex-wrap items-center justify-between gap-2">
        <h5 className="text-sm font-bold uppercase tracking-wide text-slate-600">Knowledge check</h5>
        <span className="text-xs text-slate-500">{quiz ? 'Saved quiz' : 'Not yet saved'}</span>
      </div>
      {error && <div className="mt-3"><NoticeBanner tone="error">{error}</NoticeBanner></div>}
      {notice && <div className="mt-3"><NoticeBanner tone="success" onDismiss={() => setNotice(null)}>{notice}</NoticeBanner></div>}

      <div className="mt-4 grid gap-4 sm:grid-cols-[minmax(0,1fr)_10rem]">
        <div>
          <label htmlFor={`quiz-title-${lessonId}`} className="lc-label">Quiz title</label>
          <input id={`quiz-title-${lessonId}`} value={title} onChange={(event) => setTitle(event.target.value)} required minLength={3} maxLength={160} className="lc-input" />
        </div>
        <div>
          <label htmlFor={`quiz-pass-${lessonId}`} className="lc-label">Passing score (%)</label>
          <input id={`quiz-pass-${lessonId}`} type="number" min={0} max={100} value={passingScore} onChange={(event) => setPassingScore(Number(event.target.value))} className="lc-input" />
        </div>
      </div>

      <div className="mt-5 space-y-5">
        {questions.map((question, questionIndex) => (
          <fieldset key={question.key} className="rounded-xl border border-slate-200 bg-white p-4">
            <legend className="px-1 text-xs font-bold uppercase tracking-wide text-slate-500">
              Question {questionIndex + 1}
            </legend>
            <div className="mt-2">
              <label htmlFor={`q-prompt-${question.key}`} className="lc-label">Prompt</label>
              <textarea
                id={`q-prompt-${question.key}`}
                rows={2}
                required
                maxLength={1000}
                value={question.prompt}
                onChange={(event) => updateQuestion(question.key, { prompt: event.target.value })}
                className="lc-input"
              />
            </div>
            <div className="mt-3">
              <label htmlFor={`q-explanation-${question.key}`} className="lc-label">Explanation (shown after grading)</label>
              <textarea
                id={`q-explanation-${question.key}`}
                rows={2}
                maxLength={2000}
                value={question.explanation}
                onChange={(event) => updateQuestion(question.key, { explanation: event.target.value })}
                className="lc-input"
              />
            </div>
            <div className="mt-3">
              <span className="lc-label">Options</span>
              <div className="space-y-2">
                {question.options.map((option, optionIndex) => (
                  <div key={option.key} className="flex items-center gap-2">
                    <label className="flex items-center gap-2 text-xs text-slate-600">
                      <input
                        type="radio"
                        name={`correct-${question.key}`}
                        checked={option.is_correct}
                        onChange={() => setCorrect(question.key, option.key)}
                        aria-label={`Option ${optionIndex + 1} is correct`}
                        className="h-4 w-4 border-slate-300 text-teal-700 focus:ring-teal-700"
                      />
                      Correct
                    </label>
                    <label htmlFor={`option-${option.key}`} className="sr-only">Option {optionIndex + 1}</label>
                    <input
                      id={`option-${option.key}`}
                      value={option.text}
                      onChange={(event) => updateOption(question.key, option.key, { text: event.target.value })}
                      maxLength={500}
                      required
                      className="lc-input flex-1"
                      placeholder={`Option ${optionIndex + 1}`}
                    />
                    <button
                      type="button"
                      onClick={() => removeOption(question.key, option.key)}
                      disabled={question.options.length <= 2}
                      className="rounded-lg p-2 text-slate-400 hover:bg-rose-50 hover:text-rose-700 disabled:opacity-30"
                      aria-label={`Remove option ${optionIndex + 1}`}
                    >
                      <Trash2 className="h-4 w-4" aria-hidden="true" />
                    </button>
                  </div>
                ))}
              </div>
              <button
                type="button"
                onClick={() => addOption(question.key)}
                disabled={question.options.length >= 10}
                className="mt-2 text-xs font-semibold text-teal-800 hover:text-teal-950 disabled:opacity-40"
              >
                + Add option
              </button>
            </div>
            <div className="mt-3 flex justify-end border-t border-slate-100 pt-3">
              <button
                type="button"
                onClick={() => removeQuestion(question.key)}
                disabled={questions.length <= 1}
                className="text-xs font-semibold text-rose-800 hover:text-rose-950 disabled:opacity-40"
              >
                Remove question
              </button>
            </div>
          </fieldset>
        ))}
      </div>

      <div className="mt-4 flex flex-wrap items-center gap-2">
        <button type="button" onClick={addQuestion} disabled={questions.length >= 50} className="lc-button lc-button-secondary">
          <Plus className="h-4 w-4" aria-hidden="true" /> Add question
        </button>
        <button type="submit" disabled={busy} className="lc-button lc-button-primary">
          {busy ? <Loader2 className="h-4 w-4 animate-spin" aria-hidden="true" /> : <Save className="h-4 w-4" aria-hidden="true" />}
          {busy ? 'Saving…' : quiz ? 'Replace quiz' : 'Save quiz'}
        </button>
        {quiz && (
          <ConfirmButton label="Delete quiz" confirmLabel="Confirm delete" disabled={busy} onConfirm={remove} />
        )}
      </div>
      <p className="mt-3 text-xs leading-5 text-slate-500">
        Correct answers and explanations are stripped from learner API responses until a quiz attempt is graded.
        Saving replaces the whole quiz.
      </p>
    </form>
  );
};

const NewLessonForm: React.FC<{
  courseId: string;
  moduleId: string;
  nextSort: number;
  onCancel: () => void;
  onCreated: () => void;
}> = ({ courseId, moduleId, nextSort, onCancel, onCreated }) => {
  const [title, setTitle] = useState('');
  const [slug, setSlug] = useState('');
  const [slugEdited, setSlugEdited] = useState(false);
  const [content, setContent] = useState('');
  const [minutes, setMinutes] = useState(10);
  const [sortOrder, setSortOrder] = useState(nextSort);
  const [isPreview, setIsPreview] = useState(false);
  const [provider, setProvider] = useState('');
  const [videoId, setVideoId] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const handleTitle = (value: string) => {
    setTitle(value);
    if (!slugEdited) setSlug(slugify(value));
  };

  const create = async (event: FormEvent) => {
    event.preventDefault();
    setBusy(true);
    setError(null);
    try {
      await learningApi.createLesson(courseId, {
        module_id: moduleId,
        title,
        slug,
        content_md: content,
        estimated_minutes: Number(minutes),
        sort_order: Number(sortOrder),
        is_preview: isPreview,
        video_provider: provider,
        video_id: videoId,
      });
      onCreated();
    } catch (err) {
      setError(formatError(err));
    } finally {
      setBusy(false);
    }
  };

  return (
    <form onSubmit={create} className="rounded-xl border border-teal-200 bg-teal-50/40 p-4 sm:p-5">
      <h5 className="text-sm font-bold uppercase tracking-wide text-teal-900">New lesson</h5>
      {error && <div className="mt-3"><NoticeBanner tone="error">{error}</NoticeBanner></div>}
      <div className="mt-4 grid gap-4 sm:grid-cols-2">
        <div>
          <label htmlFor="new-lesson-title" className="lc-label">Title</label>
          <input id="new-lesson-title" value={title} onChange={(event) => handleTitle(event.target.value)} required minLength={3} maxLength={160} className="lc-input" />
        </div>
        <div>
          <label htmlFor="new-lesson-slug" className="lc-label">Slug</label>
          <input
            id="new-lesson-slug"
            value={slug}
            onChange={(event) => {
              setSlugEdited(true);
              setSlug(slugify(event.target.value));
            }}
            required
            minLength={2}
            maxLength={120}
            pattern="[a-z0-9]+(?:-[a-z0-9]+)*"
            className="lc-input font-mono text-xs"
          />
        </div>
      </div>
      <div className="mt-4 grid gap-4 sm:grid-cols-3">
        <div>
          <label htmlFor="new-lesson-minutes" className="lc-label">Minutes</label>
          <input id="new-lesson-minutes" type="number" min={1} max={100000} value={minutes} onChange={(event) => setMinutes(Number(event.target.value))} className="lc-input" />
        </div>
        <div>
          <label htmlFor="new-lesson-sort" className="lc-label">Sort order</label>
          <input id="new-lesson-sort" type="number" min={0} max={100000} value={sortOrder} onChange={(event) => setSortOrder(Number(event.target.value))} className="lc-input" />
        </div>
        <div className="flex items-end pb-2">
          <label className="flex items-center gap-2 text-sm text-slate-700">
            <input type="checkbox" checked={isPreview} onChange={(event) => setIsPreview(event.target.checked)} className="h-4 w-4 rounded border-slate-300 text-teal-700 focus:ring-teal-700" />
            Public preview
          </label>
        </div>
      </div>
      <div className="mt-4 grid gap-4 sm:grid-cols-2">
        <div>
          <label htmlFor="new-lesson-provider" className="lc-label">Video provider</label>
          <select id="new-lesson-provider" value={provider} onChange={(event) => setProvider(event.target.value)} className="lc-input">
            <option value="">No video</option>
            <option value="youtube">YouTube</option>
            <option value="vimeo">Vimeo</option>
          </select>
        </div>
        <div>
          <label htmlFor="new-lesson-video" className="lc-label">Video ID</label>
          <input id="new-lesson-video" value={videoId} onChange={(event) => setVideoId(event.target.value)} disabled={!provider} maxLength={64} className="lc-input font-mono text-xs disabled:bg-slate-100" />
        </div>
      </div>
      <div className="mt-4">
        <label htmlFor="new-lesson-content" className="lc-label">Content (Markdown)</label>
        <textarea id="new-lesson-content" rows={8} value={content} onChange={(event) => setContent(event.target.value)} className="lc-input font-mono text-xs leading-6" placeholder="Safe Markdown. You can refine the lesson and add a quiz after creating it." />
      </div>
      <div className="mt-4 flex gap-2">
        <button type="submit" disabled={busy} className="lc-button lc-button-primary">{busy ? 'Creating…' : 'Create lesson'}</button>
        <button type="button" onClick={onCancel} className="lc-button lc-button-secondary">Cancel</button>
      </div>
    </form>
  );
};

const ResourcesSection: React.FC<{
  courseId: string;
  resources: LearningResource[];
  modules: LearningModule[];
  draft: boolean;
  onChanged: () => void;
}> = ({ courseId, resources, modules, draft, onChanged }) => {
  const lessons = modules.flatMap((module) => module.lessons);
  const [title, setTitle] = useState('');
  const [description, setDescription] = useState('');
  const [url, setUrl] = useState('');
  const [kind, setKind] = useState('link');
  const [lessonId, setLessonId] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [deleteError, setDeleteError] = useState<string | null>(null);

  const add = async (event: FormEvent) => {
    event.preventDefault();
    let parsed: URL;
    try {
      parsed = new URL(url);
    } catch (_) {
      setError('Enter a complete, valid HTTPS URL.');
      return;
    }
    if (parsed.protocol !== 'https:' || parsed.username || parsed.password) {
      setError('Resource URL must be an HTTPS URL without embedded credentials.');
      return;
    }
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      await learningApi.createResource(courseId, {
        title,
        description,
        url,
        kind,
        lesson_id: lessonId,
        sort_order: resources.length + 1,
      });
      setTitle('');
      setDescription('');
      setUrl('');
      setKind('link');
      setLessonId('');
      setNotice('Resource added.');
      onChanged();
    } catch (err) {
      setError(formatError(err));
    } finally {
      setBusy(false);
    }
  };

  const remove = async (resourceId: string) => {
    setDeleteError(null);
    try {
      await learningApi.deleteResource(resourceId);
      onChanged();
    } catch (err) {
      setDeleteError(formatError(err));
    }
  };

  return (
    <div className="grid gap-5 lg:grid-cols-[minmax(0,1fr)_22rem]">
      <div className="lc-card p-5">
        <h3 className="font-bold text-slate-950">Attached resources</h3>
        {deleteError && <div className="mt-3"><NoticeBanner tone="error">{deleteError}</NoticeBanner></div>}
        {resources.length === 0 ? (
          <p className="mt-3 text-sm text-slate-500">No resources attached yet.</p>
        ) : (
          <ul className="mt-3 divide-y divide-slate-100">
            {resources.map((resource) => {
              const lesson = lessons.find((item) => item.id === resource.lesson_id);
              return (
                <li key={resource.id} className="flex flex-wrap items-start justify-between gap-3 py-3">
                  <div className="min-w-0">
                    <a href={resource.url} target="_blank" rel="noopener noreferrer" className="inline-flex items-center gap-1.5 font-semibold text-slate-900 hover:text-teal-900">
                      <Link2 className="h-4 w-4 shrink-0 text-slate-400" aria-hidden="true" />
                      {resource.title}
                      <ExternalLink className="h-3 w-3 text-slate-400" aria-hidden="true" />
                    </a>
                    <div className="mt-0.5 text-xs text-slate-500">
                      {resourceKindLabel(resource.kind)} · {lesson ? lesson.title : 'Whole course'}
                    </div>
                    {resource.description && <p className="mt-1 max-w-xl text-xs leading-5 text-slate-500">{resource.description}</p>}
                  </div>
                  {draft && (
                    <ConfirmButton
                      label="Remove"
                      confirmLabel="Confirm remove"
                      onConfirm={() => remove(resource.id)}
                    />
                  )}
                </li>
              );
            })}
          </ul>
        )}
      </div>

      <div className="lc-card p-5">
        <h3 className="font-bold text-slate-950">Add a resource</h3>
        {!draft ? (
          <p className="mt-2 text-sm leading-6 text-slate-500">Resources are editable only while the course is a draft.</p>
        ) : (
          <form onSubmit={add} className="mt-3 space-y-4">
            {error && <NoticeBanner tone="error">{error}</NoticeBanner>}
            {notice && <NoticeBanner tone="success" onDismiss={() => setNotice(null)}>{notice}</NoticeBanner>}
            <div>
              <label htmlFor="resource-title" className="lc-label">Title</label>
              <input id="resource-title" value={title} onChange={(event) => setTitle(event.target.value)} required minLength={3} maxLength={200} className="lc-input" />
            </div>
            <div>
              <label htmlFor="resource-url" className="lc-label">HTTPS URL</label>
              <input id="resource-url" type="url" value={url} onChange={(event) => setUrl(event.target.value)} required placeholder="https://example.com/template" className="lc-input" />
              <FieldHint>Only HTTPS destinations are accepted. The server checks the URL before saving.</FieldHint>
            </div>
            <div>
              <label htmlFor="resource-description" className="lc-label">Description</label>
              <textarea id="resource-description" rows={2} maxLength={1000} value={description} onChange={(event) => setDescription(event.target.value)} className="lc-input" />
            </div>
            <div className="grid gap-4 sm:grid-cols-2">
              <div>
                <label htmlFor="resource-kind" className="lc-label">Kind</label>
                <select id="resource-kind" value={kind} onChange={(event) => setKind(event.target.value)} className="lc-input">
                  <option value="link">Link</option>
                  <option value="download">Download</option>
                  <option value="tool">Tool</option>
                </select>
              </div>
              <div>
                <label htmlFor="resource-lesson" className="lc-label">Attach to</label>
                <select id="resource-lesson" value={lessonId} onChange={(event) => setLessonId(event.target.value)} className="lc-input">
                  <option value="">Whole course</option>
                  {lessons.map((lesson) => (
                    <option key={lesson.id} value={lesson.id}>{lesson.title}</option>
                  ))}
                </select>
              </div>
            </div>
            <button type="submit" disabled={busy} className="lc-button lc-button-primary w-full">
              {busy ? 'Adding…' : 'Add resource'}
            </button>
          </form>
        )}
      </div>
    </div>
  );
};

const RevisionHistory: React.FC<{
  course: AuthorCourse;
  revisions: NonNullable<AuthoringPayload['revisions']>;
}> = ({ course, revisions }) => {
  const [detail, setDetail] = useState<CourseRevisionDetail | null>(null);
  const [loadingId, setLoadingId] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  const open = async (revisionId: string) => {
    setLoadingId(revisionId);
    setError(null);
    try {
      const response = await learningApi.revision(course.id, revisionId);
      setDetail(response);
    } catch (err) {
      setError(formatError(err));
    } finally {
      setLoadingId(null);
    }
  };

  return (
    <div className="lc-card p-5">
      <h3 className="font-bold text-slate-950">Published versions</h3>
      {error && <div className="mt-3"><NoticeBanner tone="error">{error}</NoticeBanner></div>}
      {revisions.length === 0 ? (
        <p className="mt-3 text-sm text-slate-500">No published versions yet. Publishing creates the first snapshot.</p>
      ) : (
        <ol className="mt-3 divide-y divide-slate-100">
          {revisions.map((revision) => (
            <li key={revision.id} className="flex flex-wrap items-start justify-between gap-3 py-3">
              <div>
                <div className="flex items-center gap-2">
                  <span className="rounded-full bg-slate-100 px-2 py-0.5 text-xs font-bold text-slate-700">v{revision.version}</span>
                  <span className="text-sm font-semibold text-slate-900">{revision.note || 'Published version'}</span>
                </div>
                <div className="mt-1 text-xs text-slate-500">
                  {formatDateTime(revision.created_at)} · {revision.created_by_email || 'Unknown author'}
                </div>
              </div>
              <button
                type="button"
                onClick={() => open(revision.id)}
                disabled={loadingId === revision.id}
                className="lc-button lc-button-secondary"
              >
                {loadingId === revision.id ? <Loader2 className="h-4 w-4 animate-spin" aria-hidden="true" /> : <Eye className="h-4 w-4" aria-hidden="true" />}
                View snapshot
              </button>
            </li>
          ))}
        </ol>
      )}

      {detail && (
        <div className="mt-5 rounded-xl border border-slate-200 bg-slate-50 p-4">
          <div className="flex items-start justify-between gap-3">
            <div>
              <h4 className="font-bold text-slate-950">Version {detail.version} snapshot</h4>
              <p className="text-xs text-slate-500">{formatDateTime(detail.created_at)}</p>
            </div>
            <button type="button" onClick={() => setDetail(null)} className="text-xs font-semibold text-slate-500 hover:text-slate-900">Close</button>
          </div>
          {detail.snapshot.course && (
            <div className="mt-3 text-sm">
              <p className="font-semibold text-slate-900">{detail.snapshot.course.title}</p>
              <p className="mt-1 text-xs text-slate-500">{detail.snapshot.course.summary}</p>
            </div>
          )}
          <div className="mt-3 grid grid-cols-2 gap-3 text-xs">
            <div className="rounded-lg bg-white p-3">
              <div className="text-slate-500">Modules</div>
              <div className="mt-1 text-lg font-bold text-slate-900">{detail.snapshot.modules?.length ?? 0}</div>
            </div>
            <div className="rounded-lg bg-white p-3">
              <div className="text-slate-500">Resources</div>
              <div className="mt-1 text-lg font-bold text-slate-900">{detail.snapshot.resources?.length ?? 0}</div>
            </div>
          </div>
          <p className="mt-3 text-xs leading-5 text-slate-500">
            Snapshots are read-only historical records. Restoring an earlier snapshot is planned for a later phase.
          </p>
        </div>
      )}
    </div>
  );
};

const CourseAnalyticsPanel: React.FC<{ courseId: string }> = ({ courseId }) => {
  const analytics = useRemote(() => learningApi.courseAnalytics(courseId), [courseId]);
  const data: CourseAnalytics | undefined = analytics.data;

  if (analytics.error) {
    return <ErrorState message={formatError(analytics.error)} onRetry={analytics.reload} />;
  }
  if (analytics.loading || !data) {
    return <LoadingState label="Loading course analytics" />;
  }

  const completionRate = data.enrollments > 0
    ? Math.round((data.completions * 100) / data.enrollments)
    : 0;

  return (
    <div className="space-y-4">
      <div className="grid gap-4 sm:grid-cols-2 xl:grid-cols-4">
        <MetricTile label="Enrollments" value={data.enrollments} detail={`${data.active_enrollments} still active`} icon={<BookOpen className="h-5 w-5" />} />
        <MetricTile label="Completions" value={data.completions} detail={`${completionRate}% of enrolled learners`} icon={<CheckCircle2 className="h-5 w-5" />} />
        <MetricTile label="Quiz attempts" value={data.quiz_attempts} detail={data.quiz_attempts > 0 ? `Average score ${data.average_quiz_score}%` : 'No attempts yet'} icon={<FileQuestion className="h-5 w-5" />} />
        <MetricTile label="Average progress" value={`${data.average_progress_percent}%`} detail="Across all enrollments" icon={<BarChart3 className="h-5 w-5" />} />
      </div>
      <div className="lc-card p-5">
        <div className="flex flex-wrap items-center justify-between gap-2">
          <h3 className="font-bold text-slate-950">Average lesson progress</h3>
          <span className="text-xs text-slate-500">Aggregate, not individual</span>
        </div>
        <div className="mt-3">
          <ProgressBar value={data.average_progress_percent} label="Average course progress" />
        </div>
        <p className="mt-4 border-t border-slate-100 pt-3 text-xs leading-5 text-slate-500">{data.note}</p>
      </div>
    </div>
  );
};
