import React, { FormEvent, ReactNode, useEffect, useMemo, useState } from 'react';
import {
  ArrowRight,
  BarChart3,
  BookOpen,
  CheckCircle2,
  FileText,
  GraduationCap,
  NotebookPen,
  Plus,
  Search,
  ShieldCheck,
  Users,
} from 'lucide-react';
import { learningApi } from '../api';
import {
  AccessDenied,
  Container,
  EmptyState,
  ErrorState,
  FieldHint,
  LoadingState,
  MetricTile,
  NoticeBanner,
  ProgressBar,
  SectionHeading,
  StatusBadge,
} from '../components';
import { Markdown } from '../markdown';
import { Link, useRouter } from '../router';
import { useSeo } from '../seo';
import {
  canAuthor,
  formatDate,
  formatError,
  levelLabel,
  roleLabel,
  slugify,
  useAuth,
  useRemote,
} from '../state';
import { AuthorCourse, LearningTopic } from '../types';
import { SignInPanel } from './learner';

export const StaffGuard: React.FC<{
  children: ReactNode;
  next: string;
  adminOnly?: boolean;
}> = ({ children, next, adminOnly = false }) => {
  const { user, authReady } = useAuth();
  if (!authReady) {
    return <Container><LoadingState label="Checking your account" /></Container>;
  }
  if (!user) return <SignInPanel next={next} title="Sign in to the author studio" />;
  if (!canAuthor(user)) {
    return (
      <Container>
        <AccessDenied
          title="Author access required"
          message="The author studio is for instructors, editors, and administrators. Your learning account can still take courses and track progress."
        >
          <Link to="/courses" className="lc-button lc-button-primary">Browse courses</Link>
        </AccessDenied>
      </Container>
    );
  }
  if (adminOnly && user.role !== 'admin') {
    return (
      <Container>
        <AccessDenied
          title="Administrator access required"
          message="This area is limited to platform administrators. Editors can review and publish courses from the author studio."
        >
          <Link to="/author" className="lc-button lc-button-primary">Back to author studio</Link>
        </AccessDenied>
      </Container>
    );
  }
  return <>{children}</>;
};

export const AuthorCoursesPage: React.FC = () => {
  useSeo({
    title: 'Author studio',
    description: 'Create, review, and publish AdControl Learning courses.',
    noindex: true,
  });
  return (
    <StaffGuard next="/author">
      <AuthorCoursesContent />
    </StaffGuard>
  );
};

const AuthorCoursesContent: React.FC = () => {
  const { user } = useAuth();
  const [statusFilter, setStatusFilter] = useState('all');
  const [query, setQuery] = useState('');
  const studio = useRemote(async () => {
    const [courseResponse, topicResponse] = await Promise.all([
      learningApi.authorCourses(),
      learningApi.authorTopics(),
    ]);
    return { courses: courseResponse.courses, topics: topicResponse.topics };
  }, []);

  const courses = studio.data?.courses || [];
  const filtered = useMemo(() => {
    const term = query.trim().toLowerCase();
    return courses.filter((course) => {
      if (statusFilter !== 'all' && course.status !== statusFilter) return false;
      if (!term) return true;
      return course.title.toLowerCase().includes(term) || course.slug.toLowerCase().includes(term);
    });
  }, [courses, query, statusFilter]);

  const counts = useMemo(() => ({
    draft: courses.filter((course) => course.status === 'draft').length,
    in_review: courses.filter((course) => course.status === 'in_review').length,
    published: courses.filter((course) => course.status === 'published').length,
  }), [courses]);

  return (
    <Container>
      <div className="flex flex-col gap-5 sm:flex-row sm:items-end sm:justify-between">
        <div>
          <div className="lc-eyebrow">Author studio</div>
          <h1 className="mt-2 text-3xl font-bold tracking-tight text-slate-950 sm:text-4xl">Your courses</h1>
          <p className="mt-2 max-w-2xl text-sm leading-6 text-slate-600">
            Draft, review, and publish structured courses. Every publication creates a version snapshot;
            published courses return to draft before editing.
          </p>
        </div>
        <div className="flex flex-wrap gap-2">
          {user?.role === 'admin' && (
            <>
              <Link to="/author/team" className="lc-button lc-button-secondary">
                <Users className="h-4 w-4" aria-hidden="true" /> Team
              </Link>
              <Link to="/author/analytics" className="lc-button lc-button-secondary">
                <BarChart3 className="h-4 w-4" aria-hidden="true" /> Analytics
              </Link>
            </>
          )}
          <Link to="/author/courses/new" className="lc-button lc-button-primary">
            <Plus className="h-4 w-4" aria-hidden="true" /> New course
          </Link>
        </div>
      </div>

      <div className="mt-8 grid gap-4 sm:grid-cols-3">
        <MetricTile label="Drafts" value={counts.draft} detail="Work in progress or returned for changes." icon={<NotebookPen className="h-5 w-5" />} />
        <MetricTile label="In review" value={counts.in_review} detail="Waiting for an editor decision." icon={<ShieldCheck className="h-5 w-5" />} />
        <MetricTile label="Published" value={counts.published} detail="Live in the public catalog." icon={<BookOpen className="h-5 w-5" />} />
      </div>

      <div className="mt-8 flex flex-col gap-3 rounded-xl border border-slate-200 bg-white p-4 sm:flex-row sm:items-end">
        <div className="flex-1">
          <label htmlFor="author-course-search" className="lc-label">Search your courses</label>
          <div className="relative">
            <Search className="pointer-events-none absolute left-3 top-1/2 h-4 w-4 -translate-y-1/2 text-slate-400" aria-hidden="true" />
            <input
              id="author-course-search"
              type="search"
              value={query}
              onChange={(event) => setQuery(event.target.value)}
              className="lc-input pl-9"
              placeholder="Title or slug"
            />
          </div>
        </div>
        <div className="sm:w-52">
          <label htmlFor="author-status-filter" className="lc-label">Status</label>
          <select
            id="author-status-filter"
            value={statusFilter}
            onChange={(event) => setStatusFilter(event.target.value)}
            className="lc-input"
          >
            <option value="all">All statuses</option>
            <option value="draft">Draft</option>
            <option value="in_review">In review</option>
            <option value="approved">Approved</option>
            <option value="published">Published</option>
            <option value="archived">Archived</option>
          </select>
        </div>
      </div>

      <div className="mt-6" aria-live="polite">
        {studio.error ? (
          <ErrorState message={formatError(studio.error)} onRetry={studio.reload} />
        ) : studio.loading ? (
          <LoadingState label="Loading your courses" />
        ) : filtered.length === 0 ? (
          <EmptyState
            icon={<NotebookPen className="h-6 w-6" aria-hidden="true" />}
            title={courses.length === 0 ? 'No courses yet' : 'No courses match those filters'}
          >
            {courses.length === 0 ? (
              <p>Start with one focused course. You can add modules, lessons, resources, and a knowledge check before submitting it for review.</p>
            ) : (
              <p>Clear the filters to see every course you can manage.</p>
            )}
            <Link to="/author/courses/new" className="lc-button lc-button-primary mt-4">Create a course</Link>
          </EmptyState>
        ) : (
          <div className="space-y-3">
            <p className="text-xs text-slate-500">{filtered.length} of {courses.length} courses</p>
            {filtered.map((course) => (
              <AuthorCourseRow key={course.id} course={course} topics={studio.data?.topics || []} />
            ))}
          </div>
        )}
      </div>
    </Container>
  );
};

const AuthorCourseRow: React.FC<{ course: AuthorCourse; topics: LearningTopic[] }> = ({
  course,
  topics,
}) => {
  const topic = topics.find((item) => item.id === course.topic_id);
  return (
    <article className="lc-card flex flex-col gap-4 p-5 sm:flex-row sm:items-center sm:justify-between">
      <div className="min-w-0">
        <div className="flex flex-wrap items-center gap-2">
          <StatusBadge status={course.status} />
          <span className="text-xs font-medium text-slate-500">
            {topic?.title || course.topic_title || 'Topic'}
          </span>
          {course.version > 0 && (
            <span className="text-xs text-slate-500">v{course.version}</span>
          )}
        </div>
        <h2 className="mt-2 truncate text-lg font-bold text-slate-950">
          <Link to={`/author/courses/${course.id}`} className="hover:text-teal-900">{course.title}</Link>
        </h2>
        <p className="mt-1 line-clamp-1 text-sm text-slate-600">{course.summary}</p>
        <div className="mt-2 flex flex-wrap gap-x-4 gap-y-1 text-xs text-slate-500">
          <span>{levelLabel(course.level)}</span>
          <span>{course.duration_minutes} min</span>
          <span>Updated {formatDate(course.updated_at)}</span>
          {course.author_email && <span>Author {course.author_email}</span>}
        </div>
      </div>
      <div className="flex shrink-0 flex-wrap gap-2">
        {course.status === 'published' && (
          <Link to={`/courses/${course.slug}`} className="lc-button lc-button-secondary">Public page</Link>
        )}
        <Link to={`/author/courses/${course.id}`} className="lc-button lc-button-primary">
          Edit course <ArrowRight className="h-4 w-4" aria-hidden="true" />
        </Link>
      </div>
    </article>
  );
};

export const NewCoursePage: React.FC = () => {
  useSeo({
    title: 'Create a course',
    description: 'Create a new AdControl Learning course draft.',
    noindex: true,
  });
  return (
    <StaffGuard next="/author/courses/new">
      <NewCourseContent />
    </StaffGuard>
  );
};

const NewCourseContent: React.FC = () => {
  const { navigate } = useRouter();
  const topics = useRemote(() => learningApi.authorTopics(), []);
  const [title, setTitle] = useState('');
  const [slug, setSlug] = useState('');
  const [slugEdited, setSlugEdited] = useState(false);
  const [topicId, setTopicId] = useState('');
  const [summary, setSummary] = useState('');
  const [description, setDescription] = useState('');
  const [level, setLevel] = useState('beginner');
  const [duration, setDuration] = useState(60);
  const [outcomes, setOutcomes] = useState('');
  const [preview, setPreview] = useState(false);
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (!topicId && topics.data?.topics[0]) setTopicId(topics.data.topics[0].id);
  }, [topicId, topics.data]);

  const handleTitle = (value: string) => {
    setTitle(value);
    if (!slugEdited) setSlug(slugify(value));
  };

  const handleSubmit = async (event: FormEvent) => {
    event.preventDefault();
    setError(null);
    setSubmitting(true);
    try {
      const payload = await learningApi.createCourse({
        title,
        slug,
        topic_id: topicId,
        summary,
        description_md: description,
        level,
        duration_minutes: Number(duration),
        outcomes: outcomes.split('\n').map((item) => item.trim()).filter(Boolean),
      });
      navigate(`/author/courses/${payload.course.id}`);
    } catch (err) {
      setError(formatError(err));
      setSubmitting(false);
    }
  };

  return (
    <Container>
      <div className="mb-6">
        <Link to="/author" className="text-sm font-medium text-slate-600 hover:text-teal-900">← Author studio</Link>
      </div>
      <div className="mx-auto max-w-3xl">
        <div className="lc-eyebrow">New draft</div>
        <h1 className="mt-2 text-3xl font-bold tracking-tight text-slate-950">Create a course</h1>
        <p className="mt-2 text-sm leading-6 text-slate-600">
          Start with the promise of the course. You can add the curriculum after the draft is created.
          Courses are saved as drafts and are not public until reviewed and published.
        </p>

        {topics.error ? <div className="mt-6"><ErrorState message={formatError(topics.error)} onRetry={topics.reload} /></div> : null}
        {error && <div className="mt-6"><NoticeBanner tone="error">{error}</NoticeBanner></div>}

        <form onSubmit={handleSubmit} className="lc-card mt-6 space-y-6 p-6 sm:p-8">
          <div>
            <label htmlFor="new-course-title" className="lc-label">Course title</label>
            <input
              id="new-course-title"
              value={title}
              onChange={(event) => handleTitle(event.target.value)}
              required
              minLength={3}
              maxLength={160}
              className="lc-input"
              placeholder="e.g. Paid search fundamentals"
            />
            <FieldHint>A clear promise in plain language. Avoid channel jargon in the title.</FieldHint>
          </div>

          <div>
            <label htmlFor="new-course-slug" className="lc-label">URL slug</label>
            <div className="flex items-center rounded-lg border border-slate-300 bg-white focus-within:ring-2 focus-within:ring-teal-700/20">
              <span className="pl-3 text-sm text-slate-400">/courses/</span>
              <input
                id="new-course-slug"
                value={slug}
                onChange={(event) => {
                  setSlugEdited(true);
                  setSlug(slugify(event.target.value));
                }}
                required
                minLength={2}
                maxLength={120}
                pattern="[a-z0-9]+(?:-[a-z0-9]+)*"
                className="w-full rounded-lg border-0 bg-transparent px-1 py-2.5 pr-3 text-sm text-slate-900 focus:outline-none"
                placeholder="paid-search-fundamentals"
              />
            </div>
            <FieldHint>Lowercase letters, digits, and single hyphens. Choose carefully; this is the public URL.</FieldHint>
          </div>

          <div className="grid gap-5 sm:grid-cols-2">
            <div>
              <label htmlFor="new-course-topic" className="lc-label">Topic</label>
              <select
                id="new-course-topic"
                value={topicId}
                onChange={(event) => setTopicId(event.target.value)}
                required
                className="lc-input"
              >
                <option value="">Choose a topic</option>
                {(topics.data?.topics || []).map((topic) => (
                  <option key={topic.id} value={topic.id}>{topic.title}</option>
                ))}
              </select>
            </div>
            <div>
              <label htmlFor="new-course-level" className="lc-label">Level</label>
              <select id="new-course-level" value={level} onChange={(event) => setLevel(event.target.value)} className="lc-input">
                <option value="beginner">Beginner</option>
                <option value="intermediate">Intermediate</option>
                <option value="advanced">Advanced</option>
              </select>
            </div>
          </div>

          <div>
            <label htmlFor="new-course-duration" className="lc-label">Estimated duration (minutes)</label>
            <input
              id="new-course-duration"
              type="number"
              min={1}
              max={100000}
              value={duration}
              onChange={(event) => setDuration(Number(event.target.value))}
              className="lc-input sm:max-w-[12rem]"
            />
          </div>

          <div>
            <label htmlFor="new-course-summary" className="lc-label">Short summary</label>
            <textarea
              id="new-course-summary"
              rows={3}
              required
              minLength={10}
              maxLength={500}
              value={summary}
              onChange={(event) => setSummary(event.target.value)}
              className="lc-input"
              placeholder="Who this is for, what they will be able to do, and why it matters."
            />
            <FieldHint>Used on catalog cards and search results. 10–500 characters.</FieldHint>
          </div>

          <div>
            <div className="flex items-center justify-between gap-3">
              <label htmlFor="new-course-description" className="lc-label mb-0">Course description</label>
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
                id="new-course-description"
                rows={12}
                required
                value={description}
                onChange={(event) => setDescription(event.target.value)}
                className="lc-input mt-2 font-mono text-xs leading-6"
                placeholder={'## What this course covers\n\nWrite in Markdown. Headings, lists, links, and code are supported.'}
              />
            )}
            <FieldHint>
              Markdown only. Raw HTML, scripts, iframes, and unsafe URLs are rejected by the server.
            </FieldHint>
          </div>

          <div>
            <label htmlFor="new-course-outcomes" className="lc-label">Learning outcomes</label>
            <textarea
              id="new-course-outcomes"
              rows={5}
              value={outcomes}
              onChange={(event) => setOutcomes(event.target.value)}
              className="lc-input"
              placeholder={'Read a search performance report\nSet a defensible campaign budget'}
            />
            <FieldHint>One outcome per line, up to 20. Plain text shown on the course page.</FieldHint>
          </div>

          <div className="flex flex-wrap items-center gap-3 border-t border-slate-100 pt-5">
            <button type="submit" disabled={submitting || !topicId} className="lc-button lc-button-primary">
              {submitting ? 'Creating draft…' : 'Create draft'}
            </button>
            <Link to="/author" className="lc-button lc-button-secondary">Cancel</Link>
          </div>
        </form>
      </div>
    </Container>
  );
};

export const TeamPage: React.FC = () => {
  useSeo({
    title: 'Learning team',
    description: 'Manage learning staff accounts.',
    noindex: true,
  });
  return (
    <StaffGuard next="/author/team" adminOnly>
      <TeamContent />
    </StaffGuard>
  );
};

const TeamContent: React.FC = () => {
  const users = useRemote(() => learningApi.adminUsers(), []);
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [role, setRole] = useState('instructor');
  const [query, setQuery] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);

  const filtered = useMemo(() => {
    const term = query.trim().toLowerCase();
    const list = users.data?.users || [];
    if (!term) return list;
    return list.filter((user) => user.email.toLowerCase().includes(term) || user.role.includes(term));
  }, [query, users.data]);

  const handleCreate = async (event: FormEvent) => {
    event.preventDefault();
    setError(null);
    setNotice(null);
    setSubmitting(true);
    try {
      await learningApi.createStaff({ email: email.trim(), password, role });
      setNotice(`Created ${role} account for ${email.trim()}. Share the password through a secure channel.`);
      setEmail('');
      setPassword('');
      users.reload();
    } catch (err) {
      setError(formatError(err));
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <Container>
      <div className="mb-6">
        <Link to="/author" className="text-sm font-medium text-slate-600 hover:text-teal-900">← Author studio</Link>
      </div>
      <div className="flex flex-col gap-4 sm:flex-row sm:items-end sm:justify-between">
        <div>
          <div className="lc-eyebrow">Administration</div>
          <h1 className="mt-2 text-3xl font-bold tracking-tight text-slate-950">Learning team</h1>
          <p className="mt-2 max-w-2xl text-sm leading-6 text-slate-600">
            Instructors author their own courses. Editors review and publish any course. Learners only see
            published content. Accounts cannot choose their own role during registration.
          </p>
        </div>
        <Link to="/author/analytics" className="lc-button lc-button-secondary">
          <BarChart3 className="h-4 w-4" aria-hidden="true" /> Aggregate analytics
        </Link>
      </div>

      <div className="mt-8 grid gap-6 lg:grid-cols-[minmax(0,1fr)_22rem]">
        <section aria-labelledby="team-list">
          <div className="flex flex-wrap items-end justify-between gap-3">
            <h2 id="team-list" className="text-xl font-bold text-slate-950">Learning accounts</h2>
            <div className="w-full sm:w-64">
              <label htmlFor="team-search" className="sr-only">Search accounts</label>
              <input
                id="team-search"
                type="search"
                value={query}
                onChange={(event) => setQuery(event.target.value)}
                placeholder="Search email or role"
                className="lc-input"
              />
            </div>
          </div>
          <div className="mt-4">
            {users.error ? (
              <ErrorState message={formatError(users.error)} onRetry={users.reload} />
            ) : users.loading ? (
              <LoadingState label="Loading accounts" />
            ) : filtered.length === 0 ? (
              <EmptyState icon={<Users className="h-6 w-6" aria-hidden="true" />} title="No accounts match">
                <p>Try a different search term.</p>
              </EmptyState>
            ) : (
              <div className="overflow-x-auto rounded-xl border border-slate-200 bg-white">
                <table className="min-w-full text-left text-sm">
                  <caption className="sr-only">Learning accounts with role and creation date</caption>
                  <thead className="border-b border-slate-200 bg-slate-50 text-xs uppercase tracking-wide text-slate-500">
                    <tr>
                      <th scope="col" className="px-4 py-3 font-semibold">Email</th>
                      <th scope="col" className="px-4 py-3 font-semibold">Role</th>
                      <th scope="col" className="px-4 py-3 font-semibold">Created</th>
                    </tr>
                  </thead>
                  <tbody className="divide-y divide-slate-100">
                    {filtered.map((user) => (
                      <tr key={user.id}>
                        <td className="px-4 py-3 font-medium text-slate-900">{user.email}</td>
                        <td className="px-4 py-3 text-slate-600">{roleLabel(user.role)}</td>
                        <td className="px-4 py-3 text-slate-500">{formatDate(user.created_at)}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            )}
          </div>
        </section>

        <aside>
          <div className="lc-card p-6">
            <h2 className="font-bold text-slate-950">Add a staff account</h2>
            <p className="mt-1 text-xs leading-5 text-slate-500">
              New staff receive a one-time password you set here. They can sign in immediately.
            </p>
            {error && <div className="mt-4"><NoticeBanner tone="error">{error}</NoticeBanner></div>}
            {notice && <div className="mt-4"><NoticeBanner tone="success" onDismiss={() => setNotice(null)}>{notice}</NoticeBanner></div>}
            <form onSubmit={handleCreate} className="mt-4 space-y-4">
              <div>
                <label htmlFor="staff-email" className="lc-label">Email</label>
                <input id="staff-email" type="email" required autoComplete="off" value={email} onChange={(event) => setEmail(event.target.value)} className="lc-input" />
              </div>
              <div>
                <label htmlFor="staff-password" className="lc-label">Temporary password</label>
                <input id="staff-password" type="password" required minLength={12} maxLength={128} autoComplete="new-password" value={password} onChange={(event) => setPassword(event.target.value)} className="lc-input" />
                <FieldHint>At least 12 characters. Share it securely; it is never shown again.</FieldHint>
              </div>
              <div>
                <label htmlFor="staff-role" className="lc-label">Role</label>
                <select id="staff-role" value={role} onChange={(event) => setRole(event.target.value)} className="lc-input">
                  <option value="instructor">Instructor — author own courses</option>
                  <option value="editor">Editor — review and publish any course</option>
                </select>
              </div>
              <button type="submit" disabled={submitting} className="lc-button lc-button-primary w-full">
                {submitting ? 'Creating…' : 'Create staff account'}
              </button>
            </form>
          </div>
        </aside>
      </div>
    </Container>
  );
};

export const AdminAnalyticsPage: React.FC = () => {
  useSeo({
    title: 'Learning analytics',
    description: 'Aggregate AdControl Learning enrollment, completion, and quiz metrics.',
    noindex: true,
  });
  return (
    <StaffGuard next="/author/analytics" adminOnly>
      <AdminAnalyticsContent />
    </StaffGuard>
  );
};

const AdminAnalyticsContent: React.FC = () => {
  const analytics = useRemote(() => learningApi.adminAnalytics(), []);
  const data = analytics.data;
  const totalUsers = data
    ? data.users_by_role.learners + data.users_by_role.instructors + data.users_by_role.editors
    : 0;

  return (
    <Container>
      <div className="mb-6 flex flex-wrap items-center justify-between gap-3">
        <Link to="/author" className="text-sm font-medium text-slate-600 hover:text-teal-900">← Author studio</Link>
        <Link to="/author/team" className="lc-button lc-button-secondary">Manage team</Link>
      </div>
      <SectionHeading
        eyebrow="Administration"
        title="Learning analytics"
        description="Aggregate enrollment, completion, and quiz activity. Named learner activity is deliberately not exposed."
      />
      {analytics.error ? (
        <ErrorState message={formatError(analytics.error)} onRetry={analytics.reload} />
      ) : analytics.loading || !data ? (
        <LoadingState label="Loading aggregate analytics" />
      ) : (
        <>
          <div className="grid gap-4 sm:grid-cols-2 xl:grid-cols-4">
            <MetricTile label="Learners" value={data.users_by_role.learners} detail={`${totalUsers} learning and staff accounts`} icon={<GraduationCap className="h-5 w-5" />} />
            <MetricTile label="Enrollments" value={data.totals.enrollments} detail="All time, all courses" icon={<BookOpen className="h-5 w-5" />} />
            <MetricTile label="Completions" value={data.totals.completions} detail="Courses fully completed" icon={<CheckCircle2 className="h-5 w-5" />} />
            <MetricTile label="Quiz attempts" value={data.totals.quiz_attempts} detail="Graded submissions" icon={<FileText className="h-5 w-5" />} />
          </div>

          <div className="mt-4 grid gap-4 sm:grid-cols-2">
            <MetricTile label="Instructors" value={data.users_by_role.instructors} detail="Can author their own courses" icon={<NotebookPen className="h-5 w-5" />} />
            <MetricTile label="Editors" value={data.users_by_role.editors} detail="Can review and publish any course" icon={<ShieldCheck className="h-5 w-5" />} />
          </div>

          <section className="mt-10" aria-labelledby="course-metrics">
            <h2 id="course-metrics" className="text-xl font-bold tracking-tight text-slate-950">Course performance</h2>
            <p className="mt-1 text-sm text-slate-600">{data.note}</p>
            <div className="mt-4 overflow-x-auto rounded-xl border border-slate-200 bg-white">
              <table className="min-w-full text-left text-sm">
                <caption className="sr-only">Aggregate course enrollment, completion, and quiz metrics</caption>
                <thead className="border-b border-slate-200 bg-slate-50 text-xs uppercase tracking-wide text-slate-500">
                  <tr>
                    <th scope="col" className="px-4 py-3 font-semibold">Course</th>
                    <th scope="col" className="px-4 py-3 font-semibold">Status</th>
                    <th scope="col" className="px-4 py-3 text-right font-semibold">Enrollments</th>
                    <th scope="col" className="px-4 py-3 text-right font-semibold">Completions</th>
                    <th scope="col" className="px-4 py-3 font-semibold">Average progress</th>
                    <th scope="col" className="px-4 py-3 text-right font-semibold">Quiz avg</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-slate-100">
                  {data.courses.map((row) => (
                    <tr key={row.course_id}>
                      <td className="px-4 py-3">
                        <span className="block font-semibold text-slate-900">{row.title}</span>
                        <span className="block text-xs text-slate-500">{row.author_email || 'No author'}</span>
                      </td>
                      <td className="px-4 py-3"><StatusBadge status={row.status} /></td>
                      <td className="px-4 py-3 text-right tabular-nums text-slate-700">{row.analytics.enrollments}</td>
                      <td className="px-4 py-3 text-right tabular-nums text-slate-700">{row.analytics.completions}</td>
                      <td className="min-w-[10rem] px-4 py-3">
                        <ProgressBar value={row.analytics.average_progress_percent} label={`Average progress for ${row.title}`} />
                      </td>
                      <td className="px-4 py-3 text-right tabular-nums text-slate-700">
                        {row.analytics.quiz_attempts > 0 ? `${row.analytics.average_quiz_score}%` : '—'}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            {data.courses.length === 0 && (
              <p className="mt-4 text-sm text-slate-500">No courses exist yet.</p>
            )}
          </section>
        </>
      )}
    </Container>
  );
};
