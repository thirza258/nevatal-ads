import React, { FormEvent, useEffect, useState } from 'react';
import {
  ArrowRight,
  BookOpen,
  CheckCircle2,
  Clock3,
  Compass,
  GraduationCap,
  Layers3,
  LockKeyhole,
  PlayCircle,
  Search,
  Sparkles,
  Target,
} from 'lucide-react';
import { learningApi } from '../api';
import {
  Breadcrumbs,
  Container,
  CourseCard,
  DurationBadge,
  EmptyState,
  ErrorState,
  LevelBadge,
  LoadingState,
  SectionHeading,
  VideoEmbed,
} from '../components';
import { Markdown } from '../markdown';
import { Link, useRouter } from '../router';
import { useSeo } from '../seo';
import { formatDate, formatError, formatMoney, isLearningUser, levelLabel, useAuth, useRemote } from '../state';
import { LearningPath, LearningTopic } from '../types';

const isReviewOverdue = (value?: string): boolean => {
  if (!value) return false;
  const date = new Date(value);
  return !Number.isNaN(date.getTime()) && date.getTime() < Date.now();
};

export const HomePage: React.FC = () => {
  useSeo({
    title: 'AdControl Learning — practical marketing skills',
    description:
      'Free, practical courses on digital marketing foundations, SEO, GEO, paid search, affiliate publishing, content, and honest measurement.',
    path: '/',
  });

  const home = useRemote(async () => {
    const [courseResponse, topicResponse, pathResponse] = await Promise.all([
      learningApi.getCourses({ limit: 50 }),
      learningApi.getTopics(),
      learningApi.getPaths(),
    ]);
    return {
      courses: courseResponse.courses,
      topics: topicResponse.topics,
      paths: pathResponse.learning_paths,
    };
  }, []);

  const courses = home.data?.courses || [];
  const topics = home.data?.topics || [];
  const paths = home.data?.paths || [];
  const firstPath = paths[0];

  return (
    <>
      <section className="border-b border-slate-200/80 bg-white">
        <Container className="grid items-center gap-10 py-12 sm:py-16 lg:grid-cols-[1.25fr_0.75fr] lg:py-20">
          <div>
            <div className="lc-eyebrow">Open course catalog</div>
            <h1 className="mt-3 max-w-3xl text-4xl font-bold leading-[1.08] tracking-[-0.03em] text-slate-950 sm:text-5xl lg:text-6xl">
              Marketing skills you can put to work today.
            </h1>
            <p className="mt-5 max-w-2xl text-base leading-7 text-slate-600 sm:text-lg">
              Learn the foundations, the platforms, and the measurement habits behind responsible
              advertising. Short lessons, practical exercises, and a catalog that clearly marks any
              paid course.
            </p>
            <div className="mt-8 flex flex-wrap gap-3">
              <Link to="/courses" className="lc-button lc-button-primary px-5 py-3">
                Browse courses
                <ArrowRight className="h-4 w-4" aria-hidden="true" />
              </Link>
              <Link to="/learning-paths" className="lc-button lc-button-secondary px-5 py-3">
                Follow a learning path
              </Link>
            </div>
            <div className="mt-8 flex flex-wrap items-center gap-x-6 gap-y-2 text-sm text-slate-600">
              <span className="inline-flex items-center gap-2">
                <CheckCircle2 className="h-4 w-4 text-teal-700" aria-hidden="true" />
                Free core courses
              </span>
              <span className="inline-flex items-center gap-2">
                <CheckCircle2 className="h-4 w-4 text-teal-700" aria-hidden="true" />
                Self-paced
              </span>
              <span className="inline-flex items-center gap-2">
                <CheckCircle2 className="h-4 w-4 text-teal-700" aria-hidden="true" />
                Progress saved
              </span>
            </div>
          </div>

          <aside className="lc-card overflow-hidden">
            <div className="border-b border-slate-100 bg-gradient-to-br from-teal-900 via-teal-950 to-slate-950 p-6 text-white sm:p-8">
              <span className="inline-flex items-center gap-2 rounded-full bg-white/10 px-3 py-1 text-xs font-semibold">
                <Sparkles className="h-3.5 w-3.5" aria-hidden="true" />
                Start here
              </span>
              <h2 className="mt-4 text-2xl font-bold tracking-tight">
                {firstPath ? firstPath.title : 'Build a marketing foundation'}
              </h2>
              <p className="mt-2 text-sm leading-6 text-teal-100">
                {firstPath
                  ? firstPath.description
                  : 'A guided sequence of courses that takes you from the fundamentals to measurement you can trust.'}
              </p>
              {firstPath && (
                <Link
                  to={`/learning-paths/${firstPath.slug}`}
                  className="mt-5 inline-flex items-center gap-2 text-sm font-semibold text-white underline decoration-teal-400 underline-offset-4 hover:text-teal-100"
                >
                  View path
                  <ArrowRight className="h-4 w-4" aria-hidden="true" />
                </Link>
              )}
            </div>
            <dl className="grid grid-cols-3 divide-x divide-slate-100 bg-white">
              <div className="px-4 py-5 text-center sm:px-5">
                <dt className="text-xs font-medium text-slate-500">Courses</dt>
                <dd className="mt-1 text-2xl font-bold tabular-nums text-slate-950">
                  {home.data ? courses.length : '—'}
                </dd>
              </div>
              <div className="px-4 py-5 text-center sm:px-5">
                <dt className="text-xs font-medium text-slate-500">Topics</dt>
                <dd className="mt-1 text-2xl font-bold tabular-nums text-slate-950">
                  {home.data ? topics.length : '—'}
                </dd>
              </div>
              <div className="px-4 py-5 text-center sm:px-5">
                <dt className="text-xs font-medium text-slate-500">Paths</dt>
                <dd className="mt-1 text-2xl font-bold tabular-nums text-slate-950">
                  {home.data ? paths.length : '—'}
                </dd>
              </div>
            </dl>
          </aside>
        </Container>
      </section>

      <Container>
        {home.error ? (
          <ErrorState message={formatError(home.error)} onRetry={home.reload} />
        ) : home.loading ? (
          <LoadingState label="Loading the catalog" />
        ) : (
          <>
            <section aria-labelledby="featured-courses">
              <SectionHeading
                eyebrow="Catalog"
                title="Courses to start with"
                description="Each course is a focused sequence of short lessons, with practical exercises and a knowledge check."
                action={
                  <Link to="/courses" className="lc-button lc-button-secondary">
                    All courses <ArrowRight className="h-4 w-4" aria-hidden="true" />
                  </Link>
                }
              />
              <h2 id="featured-courses" className="sr-only">Featured courses</h2>
              {courses.length === 0 ? (
                <EmptyState title="The launch catalog is being prepared">
                  Courses will appear here as soon as they are published.
                </EmptyState>
              ) : (
                <div className="grid gap-5 md:grid-cols-2 xl:grid-cols-3">
                  {courses.slice(0, 3).map((course) => (
                    <CourseCard key={course.id} course={course} />
                  ))}
                </div>
              )}
            </section>

            <section className="mt-16" aria-labelledby="topic-map">
              <SectionHeading
                eyebrow="Explore"
                title="Find your next topic"
                description="From first principles to channel-specific craft. Pick the area you need right now."
              />
              <h2 id="topic-map" className="sr-only">Topics</h2>
              <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-4">
                {topics.map((topic) => (
                  <Link
                    key={topic.id}
                    to={`/topics/${topic.slug}`}
                    className="lc-card group flex flex-col p-5 transition hover:-translate-y-0.5 hover:border-teal-200 hover:shadow-md"
                  >
                    <div className="flex h-10 w-10 items-center justify-center rounded-xl bg-teal-50 text-teal-800">
                      <Compass className="h-5 w-5" aria-hidden="true" />
                    </div>
                    <h3 className="mt-4 font-bold text-slate-950 group-hover:text-teal-900">{topic.title}</h3>
                    <p className="mt-2 line-clamp-2 text-sm leading-6 text-slate-600">{topic.description}</p>
                    <span className="mt-4 text-xs font-semibold text-teal-800">
                      {topic.course_count ?? 0} {(topic.course_count ?? 0) === 1 ? 'course' : 'courses'}
                    </span>
                  </Link>
                ))}
              </div>
            </section>

            <section className="mt-16" aria-labelledby="how-learning-works">
              <SectionHeading eyebrow="How it works" title="A simple, honest way to learn" />
              <h2 id="how-learning-works" className="sr-only">How learning works</h2>
              <div className="grid gap-5 md:grid-cols-3">
                {[
                  {
                    icon: BookOpen,
                    title: '1. Choose a course or path',
                    body: 'Browse by topic or follow a sequenced path. Every course lists its level and time commitment up front.',
                  },
                  {
                    icon: Target,
                    title: '2. Learn and practise',
                    body: 'Read or watch short lessons, try the practical exercise, and check your understanding with a quiz.',
                  },
                  {
                    icon: CheckCircle2,
                    title: '3. Keep your progress',
                    body: 'Your lesson progress is saved to your free account. Resume exactly where you left off.',
                  },
                ].map((step) => {
                  const Icon = step.icon;
                  return (
                    <div key={step.title} className="lc-card p-6">
                      <Icon className="h-6 w-6 text-teal-800" aria-hidden="true" />
                      <h3 className="mt-4 font-bold text-slate-950">{step.title}</h3>
                      <p className="mt-2 text-sm leading-6 text-slate-600">{step.body}</p>
                    </div>
                  );
                })}
              </div>
            </section>

            {paths.length > 0 && (
              <section className="mt-16" aria-labelledby="learning-paths">
                <SectionHeading
                  eyebrow="Guided"
                  title="Learning paths"
                  description="A path sequences courses into a coherent curriculum, so you always know what comes next."
                  action={
                    <Link to="/learning-paths" className="lc-button lc-button-secondary">
                      All paths <ArrowRight className="h-4 w-4" aria-hidden="true" />
                    </Link>
                  }
                />
                <h2 id="learning-paths" className="sr-only">Learning paths</h2>
                <div className="grid gap-5 lg:grid-cols-2">
                  {paths.slice(0, 2).map((path) => (
                    <PathSummary key={path.id} path={path} />
                  ))}
                </div>
              </section>
            )}
          </>
        )}
      </Container>
    </>
  );
};

const PathSummary: React.FC<{ path: LearningPath }> = ({ path }) => {
  const courses = path.courses || [];
  const totalMinutes = courses.reduce((sum, course) => sum + course.duration_minutes, 0);
  return (
    <article className="lc-card flex flex-col p-6">
      <div className="flex flex-wrap items-center gap-2">
        <span className="rounded-full bg-teal-50 px-2.5 py-1 text-xs font-bold text-teal-900">
          {levelLabel(path.level)} path
        </span>
        <DurationBadge minutes={totalMinutes} />
      </div>
      <h3 className="mt-3 text-xl font-bold tracking-tight text-slate-950">
        <Link to={`/learning-paths/${path.slug}`} className="hover:text-teal-900">{path.title}</Link>
      </h3>
      <p className="mt-2 text-sm leading-6 text-slate-600">{path.description}</p>
      <ol className="mt-4 space-y-2 border-t border-slate-100 pt-4">
        {courses.slice(0, 4).map((course, index) => (
          <li key={course.id} className="flex items-start gap-3 text-sm text-slate-700">
            <span className="mt-0.5 flex h-5 w-5 shrink-0 items-center justify-center rounded-full bg-slate-100 text-[11px] font-bold text-slate-600">
              {index + 1}
            </span>
            <span>{course.title}</span>
          </li>
        ))}
      </ol>
      <Link to={`/learning-paths/${path.slug}`} className="mt-5 inline-flex items-center gap-1 text-sm font-semibold text-teal-800 hover:text-teal-950">
        Explore this path <ArrowRight className="h-4 w-4" aria-hidden="true" />
      </Link>
    </article>
  );
};

export const CoursesPage: React.FC = () => {
  const { search, navigate } = useRouter();
  const params = new URLSearchParams(search);
  const q = params.get('q') || '';
  const topic = params.get('topic') || '';
  const [draftQuery, setDraftQuery] = useState(q);

  useEffect(() => setDraftQuery(q), [q]);

  useSeo({
    title: 'All courses',
    description: 'Browse every AdControl Learning course across marketing foundations, SEO, GEO, paid search, affiliate publishing, content, and analytics.',
    path: '/courses',
  });

  const catalog = useRemote(async () => {
    const [courseResponse, topicResponse] = await Promise.all([
      learningApi.getCourses({ q, topic }),
      learningApi.getTopics(),
    ]);
    return { courses: courseResponse.courses, topics: topicResponse.topics };
  }, [q, topic]);

  const updateFilters = (nextQuery: string, nextTopic: string) => {
    const next = new URLSearchParams();
    if (nextQuery.trim()) next.set('q', nextQuery.trim());
    if (nextTopic) next.set('topic', nextTopic);
    navigate(`/courses${next.toString() ? `?${next.toString()}` : ''}`);
  };

  const handleSubmit = (event: FormEvent) => {
    event.preventDefault();
    updateFilters(draftQuery, topic);
  };

  const activeTopic = catalog.data?.topics.find((item) => item.slug === topic);
  const courses = catalog.data?.courses || [];

  return (
    <Container>
      <Breadcrumbs items={[{ label: 'Home', to: '/' }, { label: 'Courses' }]} />
      <div className="max-w-3xl">
        <div className="lc-eyebrow">Catalog</div>
        <h1 className="mt-2 text-3xl font-bold tracking-tight text-slate-950 sm:text-4xl">All courses</h1>
        <p className="mt-3 text-base leading-7 text-slate-600">
          Focused courses for marketers who want practical skills and measurement they can defend.
        </p>
      </div>

      <form onSubmit={handleSubmit} className="mt-8 flex flex-col gap-3 sm:flex-row" role="search">
        <div className="relative flex-1">
          <label htmlFor="course-search" className="sr-only">Search courses</label>
          <Search className="pointer-events-none absolute left-3 top-1/2 h-4 w-4 -translate-y-1/2 text-slate-400" aria-hidden="true" />
          <input
            id="course-search"
            type="search"
            value={draftQuery}
            onChange={(event) => setDraftQuery(event.target.value)}
            placeholder="Search by title, summary, or topic"
            className="lc-input pl-9"
            maxLength={100}
          />
        </div>
        <div className="sm:w-64">
          <label htmlFor="course-topic" className="sr-only">Filter by topic</label>
          <select
            id="course-topic"
            className="lc-input"
            value={topic}
            onChange={(event) => updateFilters(draftQuery, event.target.value)}
          >
            <option value="">All topics</option>
            {(catalog.data?.topics || []).map((item) => (
              <option key={item.id} value={item.slug}>{item.title}</option>
            ))}
          </select>
        </div>
        <button type="submit" className="lc-button lc-button-primary sm:px-6">Search</button>
        {(q || topic) && (
          <button
            type="button"
            className="lc-button lc-button-secondary"
            onClick={() => {
              setDraftQuery('');
              updateFilters('', '');
            }}
          >
            Clear
          </button>
        )}
      </form>

      <div className="mt-5 flex flex-wrap items-center gap-2 text-sm text-slate-600" aria-live="polite">
        <span>
          {catalog.loading
            ? 'Loading courses…'
            : `${courses.length} ${courses.length === 1 ? 'course' : 'courses'}${activeTopic ? ` in ${activeTopic.title}` : ''}${q ? ` matching “${q}”` : ''}`}
        </span>
        {topic && (
          <Link to="/courses" className="rounded-full border border-slate-300 bg-white px-2.5 py-1 text-xs font-semibold text-slate-700 hover:border-slate-400">
            Topic: {activeTopic?.title || topic} ×
          </Link>
        )}
      </div>

      <div className="mt-8">
        {catalog.error ? (
          <ErrorState message={formatError(catalog.error)} onRetry={catalog.reload} />
        ) : catalog.loading ? (
          <LoadingState label="Loading courses" />
        ) : courses.length === 0 ? (
          <EmptyState
            icon={<Search className="h-6 w-6" aria-hidden="true" />}
            title="No courses match that search"
          >
            <p>Try a broader term or clear the topic filter. New courses are added as they are reviewed and published.</p>
            {(q || topic) && (
              <button type="button" onClick={() => updateFilters('', '')} className="lc-button lc-button-secondary mt-4">
                Clear filters
              </button>
            )}
          </EmptyState>
        ) : (
          <div className="grid gap-5 md:grid-cols-2 xl:grid-cols-3">
            {courses.map((course) => (
              <CourseCard key={course.id} course={course} />
            ))}
          </div>
        )}
      </div>
    </Container>
  );
};

export const CourseDetailPage: React.FC<{ slug: string }> = ({ slug }) => {
  const { navigate } = useRouter();
  const { user } = useAuth();
  const [enrolling, setEnrolling] = useState(false);
  const [enrollError, setEnrollError] = useState<string | null>(null);
  const [purchaseInvoice, setPurchaseInvoice] = useState<import('../types').BillingInvoice | null>(null);
  const [previewLessonId, setPreviewLessonId] = useState<string | null>(null);
  const detail = useRemote(() => learningApi.getCourse(slug), [slug]);
  const course = detail.data?.course;

  useSeo({
    title: course?.title || 'Course',
    description: course?.summary || 'A course from AdControl Learning.',
    path: `/courses/${slug}`,
    type: 'article',
  });

  const handleEnroll = async () => {
    if (!user) {
      navigate(`/login?next=${encodeURIComponent(`/courses/${slug}`)}`);
      return;
    }
    if (!isLearningUser(user)) {
      return;
    }
    setEnrolling(true);
    setEnrollError(null);
    setPurchaseInvoice(null);
    try {
      await learningApi.enroll(slug);
      navigate(`/learn/${slug}`);
    } catch (error) {
      const status = (error as { status?: number }).status;
      if (course?.access_type === 'paid' && status === 402) {
        try {
          const purchase = await learningApi.purchaseCourse(slug);
          setPurchaseInvoice(purchase.invoice);
        } catch (purchaseError) {
          const purchaseStatus = (purchaseError as { status?: number }).status;
          if (purchaseStatus === 409) {
            // Access was granted after the page loaded; enrollment can now succeed.
            try {
              await learningApi.enroll(slug);
              navigate(`/learn/${slug}`);
              return;
            } catch (retryError) {
              setEnrollError(formatError(retryError));
            }
          } else {
            setEnrollError(formatError(purchaseError));
          }
        }
      } else {
        setEnrollError(formatError(error));
      }
    } finally {
      setEnrolling(false);
    }
  };

  if (detail.loading) {
    return <Container><LoadingState label="Loading course" /></Container>;
  }
  if (detail.error || !course || !detail.data) {
    return (
      <Container>
        <Breadcrumbs items={[{ label: 'Home', to: '/' }, { label: 'Courses', to: '/courses' }, { label: 'Not found' }]} />
        <ErrorState
          title="Course not found"
          message={detail.error ? formatError(detail.error) : 'This course may have been unpublished or moved.'}
          onRetry={detail.reload}
        />
        <Link to="/courses" className="lc-button lc-button-secondary">Back to all courses</Link>
      </Container>
    );
  }

  const curriculum = detail.data.curriculum;
  const previewLesson = previewLessonId
    ? curriculum.modules.flatMap((module) => module.lessons).find((lesson) => lesson.id === previewLessonId)
    : undefined;
  const learningUser = isLearningUser(user);
  const backOfficeOnly = !!user && !learningUser;

  return (
    <Container>
      <Breadcrumbs
        items={[
          { label: 'Home', to: '/' },
          { label: 'Courses', to: '/courses' },
          { label: detail.data.course.topic_title, to: `/topics/${detail.data.course.topic_slug}` },
          { label: detail.data.course.title },
        ]}
      />

      <div
        itemScope
        itemType="https://schema.org/Course"
        className="grid gap-10 lg:grid-cols-[minmax(0,1fr)_22rem]"
      >
        <div className="min-w-0">
          <div className="flex flex-wrap items-center gap-3">
            <Link
              to={`/topics/${course.topic_slug}`}
              className="rounded-full bg-teal-50 px-3 py-1 text-xs font-bold uppercase tracking-[0.12em] text-teal-900 hover:bg-teal-100"
            >
              {course.topic_title}
            </Link>
            <span className="rounded-full border border-slate-200 bg-white px-3 py-1 text-xs font-semibold text-slate-700">
              {course.access_type === 'paid' ? formatMoney(course.price_cents, course.currency) : 'Free'}
            </span>
            {isReviewOverdue(course.next_review_due_at) && (
              <span className="rounded-full border border-amber-200 bg-amber-50 px-3 py-1 text-xs font-semibold text-amber-950">
                Review due
              </span>
            )}
          </div>

          <h1 itemProp="name" className="mt-4 text-3xl font-bold leading-tight tracking-[-0.02em] text-slate-950 sm:text-4xl">
            {course.title}
          </h1>
          <meta itemProp="description" content={course.summary} />
          <meta itemProp="courseCode" content={course.slug} />
          <meta itemProp="educationalLevel" content={levelLabel(course.level)} />
          <meta itemProp="isAccessibleForFree" content={course.access_type === 'paid' ? 'false' : 'true'} />
          <span className="hidden" itemProp="provider" itemScope itemType="https://schema.org/Organization">
            <meta itemProp="name" content="AdControl Learning" />
          </span>
          <p className="mt-4 max-w-3xl text-base leading-7 text-slate-600 sm:text-lg">{course.summary}</p>

          <div className="mt-5 flex flex-wrap items-center gap-x-5 gap-y-2">
            <LevelBadge level={course.level} />
            <DurationBadge minutes={course.duration_minutes} />
            <span className="inline-flex items-center gap-1.5 text-xs font-medium text-slate-600">
              <Layers3 className="h-4 w-4 text-teal-700" aria-hidden="true" />
              {curriculum.module_count} modules · {curriculum.lesson_count} lessons
            </span>
          </div>

          <div className="mt-10 grid gap-8 lg:grid-cols-[minmax(0,1fr)_16rem]">
            <div className="min-w-0">
              <h2 className="text-xl font-bold tracking-tight text-slate-950">About this course</h2>
              <div className="mt-3">
                <Markdown source={course.description_md} emptyMessage="The course description is being prepared." />
              </div>

              <section className="mt-10" aria-labelledby="course-outline">
                <h2 id="course-outline" className="text-xl font-bold tracking-tight text-slate-950">Course outline</h2>
                <p className="mt-1 text-sm text-slate-600">
                  Locked lessons open after you enroll. Lessons marked preview can be read now.
                </p>
                <div className="mt-4 space-y-4">
                  {curriculum.modules.map((module, moduleIndex) => (
                    <section key={module.id} className="lc-card overflow-hidden" aria-labelledby={`module-${module.id}`}>
                      <div className="border-b border-slate-100 bg-slate-50/70 px-5 py-3">
                        <h3 id={`module-${module.id}`} className="text-sm font-bold text-slate-900">
                          Module {moduleIndex + 1}: {module.title}
                        </h3>
                        {module.summary && <p className="mt-0.5 text-xs text-slate-500">{module.summary}</p>}
                      </div>
                      <ul className="divide-y divide-slate-100">
                        {module.lessons.map((lesson) => {
                          const isPreview = lesson.is_preview;
                          return (
                            <li key={lesson.id} className="flex items-center justify-between gap-3 px-5 py-3">
                              <div className="flex min-w-0 items-center gap-3">
                                {isPreview ? (
                                  <PlayCircle className="h-4 w-4 shrink-0 text-teal-700" aria-hidden="true" />
                                ) : (
                                  <LockKeyhole className="h-4 w-4 shrink-0 text-slate-400" aria-hidden="true" />
                                )}
                                <div className="min-w-0">
                                  <span className="block truncate text-sm font-medium text-slate-800">{lesson.title}</span>
                                  <span className="block text-xs text-slate-500">{lesson.estimated_minutes} min</span>
                                </div>
                              </div>
                              {isPreview ? (
                                <button
                                  type="button"
                                  className="shrink-0 text-xs font-semibold text-teal-800 hover:text-teal-950"
                                  aria-expanded={previewLessonId === lesson.id}
                                  aria-controls="preview-panel"
                                  onClick={() => setPreviewLessonId(previewLessonId === lesson.id ? null : lesson.id)}
                                >
                                  {previewLessonId === lesson.id ? 'Hide preview' : 'Preview'}
                                </button>
                              ) : (
                                <span className="shrink-0 text-xs font-medium text-slate-500">Enroll to open</span>
                              )}
                            </li>
                          );
                        })}
                      </ul>
                    </section>
                  ))}
                </div>

                {previewLesson && (
                  <section id="preview-panel" className="lc-card mt-6 border-teal-200 p-5 sm:p-6" aria-labelledby="preview-title">
                    <div className="flex items-start justify-between gap-4">
                      <div>
                        <div className="lc-eyebrow">Free preview</div>
                        <h3 id="preview-title" className="mt-1 text-lg font-bold text-slate-950">{previewLesson.title}</h3>
                      </div>
                      <button
                        type="button"
                        className="text-xs font-semibold text-slate-500 hover:text-slate-900"
                        onClick={() => setPreviewLessonId(null)}
                      >
                        Close preview
                      </button>
                    </div>
                    {previewLesson.video_provider && previewLesson.video_id && (
                      <VideoEmbed
                        provider={previewLesson.video_provider}
                        videoId={previewLesson.video_id}
                        title={previewLesson.title}
                      />
                    )}
                    <Markdown source={previewLesson.content_md} emptyMessage="This preview lesson has no written content yet." />
                  </section>
                )}
              </section>
            </div>

            <aside className="space-y-5 lg:sticky lg:top-24 lg:self-start">
              <div className="lc-card p-5">
                <div className="text-sm font-semibold text-slate-900">
                  {course.access_type === 'paid'
                    ? `Purchase for ${formatMoney(course.price_cents, course.currency)}`
                    : 'Enroll for free'}
                </div>
                <p className="mt-1 text-xs leading-5 text-slate-500">
                  {course.access_type === 'paid'
                    ? 'A manual invoice is created for this course. Access unlocks after an administrator confirms the offline payment.'
                    : 'Create or use a free learning account to unlock every lesson, save progress, and take quizzes.'}
                </p>
                {purchaseInvoice && (
                  <div className="mt-3 rounded-lg border border-amber-200 bg-amber-50 p-3 text-xs leading-5 text-amber-950" role="status">
                    <div className="font-semibold">Invoice created · {formatMoney(purchaseInvoice.total_cents, purchaseInvoice.currency)}</div>
                    <div className="mt-1 font-mono text-[10px] text-amber-800">{purchaseInvoice.id}</div>
                    <div className="mt-1">
                      Status: {purchaseInvoice.status.replace('_', ' ')}. An administrator will confirm payment; then open this course again to enroll.
                    </div>
                  </div>
                )}
                {enrollError && (
                  <p className="mt-3 rounded-lg bg-rose-50 p-3 text-xs font-medium text-rose-900" role="alert">
                    {enrollError}
                  </p>
                )}
                {backOfficeOnly ? (
                  <div className="mt-4">
                    <p className="text-xs leading-5 text-slate-600">
                      You are signed in with an operations account. Learning enrollment uses a learner,
                      instructor, editor, or administrator account.
                    </p>
                    <a href="/dashboard" className="lc-button lc-button-secondary mt-3 w-full">Open operations</a>
                  </div>
                ) : (
                  <button
                    type="button"
                    onClick={handleEnroll}
                    disabled={enrolling}
                    className="lc-button lc-button-primary mt-4 w-full"
                  >
                    {enrolling
                      ? 'Opening course…'
                      : user
                        ? course.access_type === 'paid'
                          ? `Purchase for ${formatMoney(course.price_cents, course.currency)}`
                          : 'Start this course'
                        : course.access_type === 'paid'
                          ? 'Sign in to purchase'
                          : 'Sign in to enroll'}
                    <ArrowRight className="h-4 w-4" aria-hidden="true" />
                  </button>
                )}
                {!user && (
                  <Link to={`/register?next=${encodeURIComponent(`/courses/${slug}`)}`} className="lc-button lc-button-secondary mt-2 w-full">
                    Create a free account
                  </Link>
                )}
                <ul className="mt-4 space-y-2 border-t border-slate-100 pt-4 text-xs text-slate-600">
                  <li className="flex items-center gap-2"><CheckCircle2 className="h-3.5 w-3.5 text-teal-700" aria-hidden="true" />Every lesson included</li>
                  <li className="flex items-center gap-2"><CheckCircle2 className="h-3.5 w-3.5 text-teal-700" aria-hidden="true" />Progress saved automatically</li>
                  <li className="flex items-center gap-2">
                    <CheckCircle2 className="h-3.5 w-3.5 text-teal-700" aria-hidden="true" />
                    {course.access_type === 'paid' ? 'Manual invoice; access after payment' : 'No payment details'}
                  </li>
                </ul>
              </div>

              {course.outcomes.length > 0 && (
                <div className="lc-card p-5">
                  <h2 className="text-sm font-bold text-slate-950">What you will learn</h2>
                  <ul className="mt-3 space-y-2.5">
                    {course.outcomes.map((outcome) => (
                      <li key={outcome} className="flex items-start gap-2 text-xs leading-5 text-slate-600">
                        <CheckCircle2 className="mt-0.5 h-3.5 w-3.5 shrink-0 text-teal-700" aria-hidden="true" />
                        <span>{outcome}</span>
                      </li>
                    ))}
                  </ul>
                </div>
              )}

              <div className="rounded-xl border border-slate-200 bg-white/70 p-4 text-xs leading-5 text-slate-500">
                <div className="flex items-center gap-2 font-semibold text-slate-700">
                  <Clock3 className="h-4 w-4 text-teal-700" aria-hidden="true" />
                  Content freshness
                </div>
                <p className="mt-2">Last reviewed {formatDate(course.last_reviewed_at)}.</p>
                <p className="mt-1">Version {course.version}. Every published revision is retained for review.</p>
              </div>
            </aside>
          </div>
        </div>
      </div>
    </Container>
  );
};

export const TopicsPage: React.FC = () => {
  useSeo({
    title: 'Topics',
    description: 'Explore marketing topics from foundations and strategy to SEO, GEO, paid search, affiliate publishing, content, and analytics.',
    path: '/topics',
  });
  const topics = useRemote(() => learningApi.getTopics(), []);

  return (
    <Container>
      <Breadcrumbs items={[{ label: 'Home', to: '/' }, { label: 'Topics' }]} />
      <div className="max-w-3xl">
        <div className="lc-eyebrow">Browse by subject</div>
        <h1 className="mt-2 text-3xl font-bold tracking-tight text-slate-950 sm:text-4xl">Topics</h1>
        <p className="mt-3 text-base leading-7 text-slate-600">
          Start with the area where you need to make better decisions. Each topic groups courses at every level.
        </p>
      </div>

      <div className="mt-8">
        {topics.error ? (
          <ErrorState message={formatError(topics.error)} onRetry={topics.reload} />
        ) : topics.loading ? (
          <LoadingState label="Loading topics" />
        ) : (topics.data?.topics.length || 0) === 0 ? (
          <EmptyState title="No topics are published yet" />
        ) : (
          <div className="grid gap-5 sm:grid-cols-2 lg:grid-cols-3">
            {topics.data?.topics.map((topic) => (
              <TopicCard key={topic.id} topic={topic} />
            ))}
          </div>
        )}
      </div>
    </Container>
  );
};

const TopicCard: React.FC<{ topic: LearningTopic }> = ({ topic }) => (
  <Link
    to={`/topics/${topic.slug}`}
    className="lc-card group flex h-full flex-col p-6 transition hover:-translate-y-0.5 hover:border-teal-200 hover:shadow-md"
  >
    <div className="flex h-11 w-11 items-center justify-center rounded-xl bg-teal-50 text-teal-800">
      <Compass className="h-5 w-5" aria-hidden="true" />
    </div>
    <h2 className="mt-4 text-lg font-bold text-slate-950 group-hover:text-teal-900">{topic.title}</h2>
    <p className="mt-2 flex-1 text-sm leading-6 text-slate-600">{topic.description}</p>
    <div className="mt-5 flex items-center justify-between border-t border-slate-100 pt-4">
      <span className="text-xs font-semibold text-slate-600">
        {topic.course_count ?? 0} {(topic.course_count ?? 0) === 1 ? 'course' : 'courses'}
      </span>
      <ArrowRight className="h-4 w-4 text-teal-800 transition-transform group-hover:translate-x-0.5" aria-hidden="true" />
    </div>
  </Link>
);

export const TopicPage: React.FC<{ slug: string }> = ({ slug }) => {
  const topic = useRemote(() => learningApi.getTopic(slug), [slug]);
  useSeo({
    title: topic.data?.topic.title || 'Topic',
    description: topic.data?.topic.description || 'Explore courses in this topic.',
    path: `/topics/${slug}`,
  });

  if (topic.loading) return <Container><LoadingState label="Loading topic" /></Container>;
  if (topic.error || !topic.data) {
    return (
      <Container>
        <Breadcrumbs items={[{ label: 'Home', to: '/' }, { label: 'Topics', to: '/topics' }, { label: 'Not found' }]} />
        <ErrorState
          title="Topic not found"
          message={topic.error ? formatError(topic.error) : 'This topic may have been unpublished or moved.'}
          onRetry={topic.reload}
        />
        <Link to="/topics" className="lc-button lc-button-secondary">Back to topics</Link>
      </Container>
    );
  }

  return (
    <Container>
      <Breadcrumbs items={[{ label: 'Home', to: '/' }, { label: 'Topics', to: '/topics' }, { label: topic.data.topic.title }]} />
      <div className="max-w-3xl">
        <div className="lc-eyebrow">Topic</div>
        <h1 className="mt-2 text-3xl font-bold tracking-tight text-slate-950 sm:text-4xl">{topic.data.topic.title}</h1>
        <p className="mt-3 text-base leading-7 text-slate-600">{topic.data.topic.description}</p>
      </div>
      <div className="mt-8">
        {topic.data.courses.length === 0 ? (
          <EmptyState title="No published courses in this topic yet">
            <Link to="/courses" className="lc-link">Browse all courses</Link>
          </EmptyState>
        ) : (
          <div className="grid gap-5 md:grid-cols-2 xl:grid-cols-3">
            {topic.data.courses.map((course) => (
              <CourseCard key={course.id} course={course} />
            ))}
          </div>
        )}
      </div>
    </Container>
  );
};

export const PathsPage: React.FC = () => {
  useSeo({
    title: 'Learning paths',
    description: 'Follow a sequenced set of free marketing courses, from foundations to channel craft and measurement.',
    path: '/learning-paths',
  });
  const paths = useRemote(() => learningApi.getPaths(), []);
  return (
    <Container>
      <Breadcrumbs items={[{ label: 'Home', to: '/' }, { label: 'Learning paths' }]} />
      <div className="max-w-3xl">
        <div className="lc-eyebrow">Guided sequences</div>
        <h1 className="mt-2 text-3xl font-bold tracking-tight text-slate-950 sm:text-4xl">Learning paths</h1>
        <p className="mt-3 text-base leading-7 text-slate-600">
          Let the curriculum do the sequencing. Each path orders courses so each one builds on the last.
        </p>
      </div>
      <div className="mt-8">
        {paths.error ? (
          <ErrorState message={formatError(paths.error)} onRetry={paths.reload} />
        ) : paths.loading ? (
          <LoadingState label="Loading learning paths" />
        ) : (paths.data?.learning_paths.length || 0) === 0 ? (
          <EmptyState title="No learning paths are published yet" />
        ) : (
          <div className="space-y-6">
            {paths.data?.learning_paths.map((path) => (
              <LearningPathCard key={path.id} path={path} />
            ))}
          </div>
        )}
      </div>
    </Container>
  );
};

const LearningPathCard: React.FC<{ path: LearningPath }> = ({ path }) => {
  const courses = path.courses || [];
  const totalMinutes = courses.reduce((sum, course) => sum + course.duration_minutes, 0);
  return (
    <article className="lc-card overflow-hidden">
      <div className="grid gap-6 p-6 sm:p-8 lg:grid-cols-[minmax(0,1fr)_20rem]">
        <div>
          <div className="flex flex-wrap items-center gap-2">
            <span className="rounded-full bg-teal-50 px-2.5 py-1 text-xs font-bold text-teal-900">
              {levelLabel(path.level)} path
            </span>
            <DurationBadge minutes={totalMinutes} />
            <span className="inline-flex items-center gap-1 text-xs font-medium text-slate-600">
              <GraduationCap className="h-4 w-4 text-teal-700" aria-hidden="true" />
              {courses.length} courses
            </span>
          </div>
          <h2 className="mt-3 text-2xl font-bold tracking-tight text-slate-950">
            <Link to={`/learning-paths/${path.slug}`} className="hover:text-teal-900">{path.title}</Link>
          </h2>
          <p className="mt-2 max-w-2xl text-sm leading-6 text-slate-600">{path.description}</p>
          <Link to={`/learning-paths/${path.slug}`} className="lc-button lc-button-primary mt-5">
            View path <ArrowRight className="h-4 w-4" aria-hidden="true" />
          </Link>
        </div>
        <ol className="space-y-3">
          {courses.slice(0, 5).map((course, index) => (
            <li key={course.id} className="flex items-start gap-3">
              <span className="mt-0.5 flex h-6 w-6 shrink-0 items-center justify-center rounded-full bg-slate-100 text-xs font-bold text-slate-600">
                {index + 1}
              </span>
              <div className="min-w-0">
                <Link to={`/courses/${course.slug}`} className="block text-sm font-semibold text-slate-800 hover:text-teal-900">
                  {course.title}
                </Link>
                <span className="text-xs text-slate-500">{course.duration_minutes} min</span>
              </div>
            </li>
          ))}
          {courses.length > 5 && (
            <li className="pl-9 text-xs text-slate-500">+ {courses.length - 5} more</li>
          )}
        </ol>
      </div>
    </article>
  );
};

export const PathPage: React.FC<{ slug: string }> = ({ slug }) => {
  const path = useRemote(() => learningApi.getPath(slug), [slug]);
  useSeo({
    title: path.data?.learning_path.title || 'Learning path',
    description: path.data?.learning_path.description || 'A sequenced path of free marketing courses.',
    path: `/learning-paths/${slug}`,
  });

  if (path.loading) return <Container><LoadingState label="Loading learning path" /></Container>;
  if (path.error || !path.data) {
    return (
      <Container>
        <Breadcrumbs items={[{ label: 'Home', to: '/' }, { label: 'Learning paths', to: '/learning-paths' }, { label: 'Not found' }]} />
        <ErrorState
          title="Learning path not found"
          message={path.error ? formatError(path.error) : 'This path may have been unpublished or moved.'}
          onRetry={path.reload}
        />
        <Link to="/learning-paths" className="lc-button lc-button-secondary">Back to learning paths</Link>
      </Container>
    );
  }

  const { learning_path: learningPath, courses } = path.data;
  const totalMinutes = courses.reduce((sum, course) => sum + course.duration_minutes, 0);

  return (
    <Container>
      <Breadcrumbs items={[{ label: 'Home', to: '/' }, { label: 'Learning paths', to: '/learning-paths' }, { label: learningPath.title }]} />
      <div className="max-w-3xl">
        <div className="flex flex-wrap items-center gap-2">
          <span className="rounded-full bg-teal-50 px-2.5 py-1 text-xs font-bold text-teal-900">
            {levelLabel(learningPath.level)} path
          </span>
          <DurationBadge minutes={totalMinutes} />
        </div>
        <h1 className="mt-3 text-3xl font-bold tracking-tight text-slate-950 sm:text-4xl">{learningPath.title}</h1>
        <p className="mt-3 text-base leading-7 text-slate-600">{learningPath.description}</p>
        <p className="mt-4 text-sm text-slate-500">
          Work through the courses in order. Each course is free and saves progress to your learning account.
        </p>
      </div>

      <div className="mt-10 grid gap-6 lg:grid-cols-[minmax(0,1fr)_18rem]">
        <ol className="space-y-5">
          {courses.map((course, index) => (
            <li key={course.id} className="relative pl-12">
              <span className="absolute left-0 top-6 flex h-8 w-8 items-center justify-center rounded-full border border-teal-200 bg-teal-50 text-sm font-bold text-teal-900">
                {index + 1}
              </span>
              <CourseCard course={course} />
            </li>
          ))}
        </ol>
        <aside className="lg:sticky lg:top-24 lg:self-start">
          <div className="lc-card p-5">
            <h2 className="text-sm font-bold text-slate-950">Path at a glance</h2>
            <dl className="mt-4 space-y-3 text-sm">
              <div className="flex items-center justify-between gap-3">
                <dt className="text-slate-500">Courses</dt>
                <dd className="font-semibold text-slate-900">{courses.length}</dd>
              </div>
              <div className="flex items-center justify-between gap-3">
                <dt className="text-slate-500">Estimated time</dt>
                <dd className="font-semibold text-slate-900">{Math.round(totalMinutes / 60)} hr</dd>
              </div>
              <div className="flex items-center justify-between gap-3">
                <dt className="text-slate-500">Level</dt>
                <dd className="font-semibold text-slate-900">{levelLabel(learningPath.level)}</dd>
              </div>
            </dl>
            {courses[0] && (
              <Link to={`/courses/${courses[0].slug}`} className="lc-button lc-button-primary mt-5 w-full">
                Start with course 1
              </Link>
            )}
          </div>
        </aside>
      </div>
    </Container>
  );
};

export const PrivacyPage: React.FC = () => {
  useSeo({
    title: 'Privacy',
    description: 'How AdControl Learning handles account, enrollment, progress, and quiz data.',
    path: '/privacy',
  });
  return (
    <Container>
      <Breadcrumbs items={[{ label: 'Home', to: '/' }, { label: 'Privacy' }]} />
      <article className="lc-card mx-auto max-w-3xl p-6 sm:p-10">
        <div className="lc-eyebrow">Privacy</div>
        <h1 className="mt-2 text-3xl font-bold tracking-tight text-slate-950">Your learning data stays yours.</h1>
        <p className="mt-4 text-sm leading-7 text-slate-600">
          AdControl Learning is part of the self-hosted AdControl platform. Learning accounts are separate from
          the advertising dashboard, and we collect only what is needed to run the courses.
        </p>

        <div className="mt-8 space-y-8">
          <section aria-labelledby="privacy-collect">
            <h2 id="privacy-collect" className="text-lg font-bold text-slate-950">What we store</h2>
            <ul className="mt-3 list-disc space-y-2 pl-5 text-sm leading-7 text-slate-600">
              <li>Your email address, a salted password hash, your role, and account timestamps.</li>
              <li>Courses you enroll in, completed lesson identifiers, resume position, and completion time.</li>
              <li>Quiz attempts: the quiz identifier, score, pass result, and number correct. We do not store free-text quiz answers.</li>
              <li>An aggregate view of enrollment and completion counts for course authors. Authors do not see named learners by default.</li>
            </ul>
          </section>

          <section aria-labelledby="privacy-partner">
            <h2 id="privacy-partner" className="text-lg font-bold text-slate-950">Partner applications</h2>
            <p className="mt-3 text-sm leading-7 text-slate-600">
              If you apply through the <Link to="/become-a-publisher" className="lc-link">publisher</Link> or{' '}
              <Link to="/become-a-client" className="lc-link">client</Link> page, we store the name, email,
              company, website, message, and submission timestamps you provide. We use them only to review and
              reply to your application. Nothing is provisioned automatically, and the application is visible
              only to the deployment’s administrators. Ask the administrator who provided this site to delete
              your application after it has been reviewed.
            </p>
          </section>

          <section aria-labelledby="privacy-not-collect">
            <h2 id="privacy-not-collect" className="text-lg font-bold text-slate-950">What we do not do</h2>
            <ul className="mt-3 list-disc space-y-2 pl-5 text-sm leading-7 text-slate-600">
              <li>No third-party advertising or behavioural tracking on learning pages.</li>
              <li>No sale of personal data.</li>
              <li>No automatic changes to advertising campaigns or publisher sites from an exercise.</li>
              <li>No named learner exposure in aggregate author analytics.</li>
            </ul>
          </section>

          <section aria-labelledby="privacy-ai">
            <h2 id="privacy-ai" className="text-lg font-bold text-slate-950">Artificial Intelligence &amp; Third Parties</h2>
            <p className="mt-3 text-sm leading-7 text-slate-600">
              AdControl is 100% self-hosted on private infrastructure. We do not send learner data, campaign records, or quiz telemetry to external AI models or services (such as OpenRouter). All operations are processed locally within our self-hosted boundary.
            </p>
          </section>

          <section aria-labelledby="privacy-storage">
            <h2 id="privacy-storage" className="text-lg font-bold text-slate-950">Sign-in and storage</h2>
            <p className="mt-3 text-sm leading-7 text-slate-600">
              Your session token is kept in this browser’s local storage and sent as a bearer token to the
              AdControl API. It is deleted when you sign out. Lesson pages load video only from an approved
              provider (YouTube privacy-enhanced mode or Vimeo), and only when an author adds one.
            </p>
          </section>

          <section aria-labelledby="privacy-rights">
            <h2 id="privacy-rights" className="text-lg font-bold text-slate-950">Export and deletion</h2>
            <p className="mt-3 text-sm leading-7 text-slate-600">
              From <Link to="/my-learning" className="lc-link">My learning</Link>, you can download a JSON export of
              your account, enrollments, and quiz attempts. Learner accounts can also be deleted from that page;
              deletion removes the account, enrollments, progress, and quiz attempts. Staff accounts are managed
              by an administrator.
            </p>
          </section>

          <section aria-labelledby="privacy-contact">
            <h2 id="privacy-contact" className="text-lg font-bold text-slate-950">Questions &amp; Contact</h2>
            <p className="mt-3 text-sm leading-7 text-slate-600">
              This is a self-hosted deployment. For any privacy requests, questions, or data deletion inquiries, contact us at{' '}
              <a href="mailto:nevatalapp@gmail.com" className="lc-link text-teal-800 underline">nevatalapp@gmail.com</a>.
            </p>
          </section>
        </div>
      </article>
    </Container>
  );
};

export const TermsPage: React.FC = () => {
  useSeo({
    title: 'Terms of Service',
    description: 'Acceptable use and service terms for the self-hosted AdControl platform and courses.',
    path: '/terms',
  });
  return (
    <Container>
      <Breadcrumbs items={[{ label: 'Home', to: '/' }, { label: 'Terms of Service' }]} />
      <article className="lc-card mx-auto max-w-3xl p-6 sm:p-10">
        <div className="lc-eyebrow">Legal</div>
        <h1 className="mt-2 text-3xl font-bold tracking-tight text-slate-950">Terms of Service</h1>
        <p className="mt-4 text-sm leading-7 text-slate-600">
          AdControl and AdControl Learning operate on private, self-hosted infrastructure. By accessing this platform,
          courses, or management tools, you agree to these fair-use terms.
        </p>

        <div className="mt-8 space-y-8">
          <section aria-labelledby="terms-selfhosted">
            <h2 id="terms-selfhosted" className="text-lg font-bold text-slate-950">Self-Hosted Infrastructure</h2>
            <p className="mt-3 text-sm leading-7 text-slate-600">
              AdControl is 100% self-hosted on private servers. Your data stays strictly on this platform and is never
              monetized or sold to third-party data brokers. Services are provided on an &ldquo;as is&rdquo; basis for
              internal campaign management and educational advancement.
            </p>
          </section>

          <section aria-labelledby="terms-acceptable">
            <h2 id="terms-acceptable" className="text-lg font-bold text-slate-950">Acceptable Use</h2>
            <ul className="mt-3 list-disc space-y-2 pl-5 text-sm leading-7 text-slate-600">
              <li>Do not attempt to disrupt delivery endpoints or launch denial-of-service traffic.</li>
              <li>Do not forge click, impression, or event telemetry.</li>
              <li>Do not publish deceptive, malicious, or unlawful advertisements or campaign URLs.</li>
              <li>Learning exercises are educational and do not automatically alter live production ad inventory.</li>
            </ul>
          </section>

          <section aria-labelledby="terms-ai">
            <h2 id="terms-ai" className="text-lg font-bold text-slate-950">Artificial Intelligence Policy</h2>
            <p className="mt-3 text-sm leading-7 text-slate-600">
              AdControl does not use third-party AI providers (such as OpenRouter) for ad delivery or learner evaluations.
              All delivery and scoring logic executes strictly deterministically on our self-hosted backend.
            </p>
          </section>

          <section aria-labelledby="terms-ip">
            <h2 id="terms-ip" className="text-lg font-bold text-slate-950">Intellectual Property &amp; Content</h2>
            <p className="mt-3 text-sm leading-7 text-slate-600">
              Course contents, exercises, and instructional text are provided for study and educational reference.
              You retain rights to your campaign creative assets and original publication materials.
            </p>
          </section>

          <section aria-labelledby="terms-changes">
            <h2 id="terms-changes" className="text-lg font-bold text-slate-950">Modifications</h2>
            <p className="mt-3 text-sm leading-7 text-slate-600">
              Terms may be updated periodically to reflect software releases and feature enhancements.
            </p>
          </section>

          <section aria-labelledby="terms-contact">
            <h2 id="terms-contact" className="text-lg font-bold text-slate-950">Legal Notices &amp; Contact</h2>
            <p className="mt-3 text-sm leading-7 text-slate-600">
              For terms inquiries, abuse reports, or DMCA notices regarding this self-hosted deployment, contact{' '}
              <a href="mailto:nevatalapp@gmail.com" className="lc-link text-teal-800 underline">nevatalapp@gmail.com</a>.
            </p>
          </section>
        </div>
      </article>
    </Container>
  );
};

export const AccessibilityPage: React.FC = () => {
  useSeo({
    title: 'Accessibility',
    description: 'AdControl Learning aims to meet WCAG 2.2 AA, with keyboard, screen reader, responsive, and motion support.',
    path: '/accessibility',
  });
  return (
    <Container>
      <Breadcrumbs items={[{ label: 'Home', to: '/' }, { label: 'Accessibility' }]} />
      <article className="lc-card mx-auto max-w-3xl p-6 sm:p-10">
        <div className="lc-eyebrow">Accessibility</div>
        <h1 className="mt-2 text-3xl font-bold tracking-tight text-slate-950">Designed to be usable by everyone.</h1>
        <p className="mt-4 text-sm leading-7 text-slate-600">
          AdControl Learning targets WCAG 2.2 level AA. Accessibility is part of the authoring and review
          workflow, not an afterthought bolted on at the end.
        </p>

        <div className="mt-8 space-y-8">
          <section aria-labelledby="a11y-keyboard">
            <h2 id="a11y-keyboard" className="text-lg font-bold text-slate-950">Keyboard and screen readers</h2>
            <p className="mt-3 text-sm leading-7 text-slate-600">
              Every page has a skip link, a visible focus indicator, logical heading order, landmarks, and native
              form controls with labels. Curriculum and quiz controls are operable without a mouse. Progress bars
              expose their percentage to assistive technology, and status changes are announced.
            </p>
          </section>
          <section aria-labelledby="a11y-visual">
            <h2 id="a11y-visual" className="text-lg font-bold text-slate-950">Visual design</h2>
            <p className="mt-3 text-sm leading-7 text-slate-600">
              Text and interactive elements meet contrast targets. No status is communicated by colour alone:
              labels and icons accompany status badges. Layout reflows at phone width, and touch targets are
              sized for comfortable use.
            </p>
          </section>
          <section aria-labelledby="a11y-motion">
            <h2 id="a11y-motion" className="text-lg font-bold text-slate-950">Motion and media</h2>
            <p className="mt-3 text-sm leading-7 text-slate-600">
              Animations are brief and respect the operating system’s reduced-motion setting. Embedded videos
              are optional lesson material from approved providers and have a descriptive accessible title; the
              written lesson remains the primary content.
            </p>
          </section>
          <section aria-labelledby="a11y-feedback">
            <h2 id="a11y-feedback" className="text-lg font-bold text-slate-950">Feedback</h2>
            <p className="mt-3 text-sm leading-7 text-slate-600">
              If something blocks you, contact the AdControl administrator who provided your access and describe
              the page and task. We treat an accessibility barrier as a defect to be fixed.
            </p>
          </section>
        </div>
      </article>
    </Container>
  );
};

export const NotFoundPage: React.FC = () => {
  useSeo({
    title: 'Page not found',
    description: 'The page you requested could not be found.',
    noindex: true,
  });
  return (
    <Container>
      <div className="mx-auto max-w-xl py-12 text-center">
        <div className="mx-auto flex h-14 w-14 items-center justify-center rounded-2xl bg-teal-50 text-teal-800">
          <Compass className="h-7 w-7" aria-hidden="true" />
        </div>
        <p className="mt-6 text-sm font-bold uppercase tracking-[0.18em] text-teal-800">404</p>
        <h1 className="mt-2 text-3xl font-bold tracking-tight text-slate-950">We could not find that page.</h1>
        <p className="mt-3 text-sm leading-7 text-slate-600">
          The link may be old, or the course may have been unpublished. The catalog is a good place to pick up again.
        </p>
        <div className="mt-7 flex flex-wrap justify-center gap-3">
          <Link to="/courses" className="lc-button lc-button-primary">Browse courses</Link>
          <Link to="/" className="lc-button lc-button-secondary">Go home</Link>
        </div>
      </div>
    </Container>
  );
};

