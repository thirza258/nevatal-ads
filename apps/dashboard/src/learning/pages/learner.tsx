import React, { FormEvent, useEffect, useMemo, useState } from 'react';
import {
  ArrowRight,
  BookOpen,
  Check,
  CheckCircle2,
  ChevronLeft,
  ChevronRight,
  Clock3,
  Download,
  ExternalLink,
  FileText,
  GraduationCap,
  ListChecks,
  Loader2,
  LockKeyhole,
  PlayCircle,
  ShieldAlert,
  Trash2,
  Trophy,
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
  ProgressBar,
  StatusBadge,
  VideoEmbed,
} from '../components';
import { Markdown } from '../markdown';
import { Link, useRouter } from '../router';
import { useSeo } from '../seo';
import {
  canAuthor,
  formatDate,
  formatError,
  isLearningUser,
  loginPath,
  resourceKindLabel,
  useAuth,
  useRemote,
} from '../state';
import {
  Enrollment,
  LearningLesson,
  LearningResource,
  LessonQuiz,
  QuizAttempt,
  QuizAttemptResponse,
} from '../types';

export const SignInPanel: React.FC<{ next: string; title?: string; message?: string }> = ({
  next,
  title = 'Sign in to continue',
  message = 'Your learning account keeps your courses, progress, and quiz attempts together.',
}) => (
  <Container>
    <div className="lc-card mx-auto max-w-lg p-8 text-center">
      <div className="mx-auto flex h-12 w-12 items-center justify-center rounded-2xl bg-teal-50 text-teal-800">
        <LockKeyhole className="h-6 w-6" aria-hidden="true" />
      </div>
      <h1 className="mt-4 text-2xl font-bold tracking-tight text-slate-950">{title}</h1>
      <p className="mt-2 text-sm leading-6 text-slate-600">{message}</p>
      <div className="mt-6 flex flex-col gap-2 sm:flex-row sm:justify-center">
        <Link to={loginPath(next)} className="lc-button lc-button-primary">Sign in</Link>
        <Link to={`/register?next=${encodeURIComponent(next)}`} className="lc-button lc-button-secondary">
          Create free account
        </Link>
      </div>
      <p className="mt-5 text-xs text-slate-500">
        <Link to="/courses" className="lc-link">Browse the public catalog</Link> without an account.
      </p>
    </div>
  </Container>
);

export const MyLearningPage: React.FC = () => {
  const { authReady, user } = useAuth();
  useSeo({
    title: 'My learning',
    description: 'Your AdControl Learning courses, progress, and account data.',
    path: '/my-learning',
    noindex: true,
  });

  if (!authReady) {
    return <Container><LoadingState label="Restoring your learning account" /></Container>;
  }
  if (!user) {
    return <SignInPanel next="/my-learning" />;
  }
  if (!isLearningUser(user)) {
    return (
      <Container>
        <div className="lc-card mx-auto max-w-lg p-8 text-center">
          <h1 className="text-2xl font-bold text-slate-950">Learning requires a learning account</h1>
          <p className="mt-2 text-sm leading-6 text-slate-600">
            You are signed in with an advertising operations account. Use a learner, instructor, editor,
            or administrator account to take courses.
          </p>
          <a href="/dashboard" className="lc-button lc-button-primary mt-6">Open operations dashboard</a>
        </div>
      </Container>
    );
  }
  return <MyLearningContent />;
};

const MyLearningContent: React.FC = () => {
  const { user, setUser } = useAuth();
  const { navigate } = useRouter();
  const learning = useRemote(() => learningApi.myLearning(), []);
  const [exporting, setExporting] = useState(false);
  const [exportError, setExportError] = useState<string | null>(null);
  const [showDelete, setShowDelete] = useState(false);
  const [deleting, setDeleting] = useState(false);
  const [deleteError, setDeleteError] = useState<string | null>(null);

  const enrollments = learning.data?.enrollments || [];
  const active = enrollments.filter((item) => item.status !== 'completed');
  const completed = enrollments.filter((item) => item.status === 'completed');
  const overallCompleted = enrollments.reduce((sum, item) => sum + item.completed_lessons, 0);

  const handleExport = async () => {
    setExporting(true);
    setExportError(null);
    try {
      const blob = await learningApi.exportData();
      const url = URL.createObjectURL(blob);
      const anchor = document.createElement('a');
      anchor.href = url;
      anchor.download = `adcontrol-learning-export-${new Date().toISOString().slice(0, 10)}.json`;
      document.body.appendChild(anchor);
      anchor.click();
      anchor.remove();
      URL.revokeObjectURL(url);
    } catch (error) {
      setExportError(formatError(error));
    } finally {
      setExporting(false);
    }
  };

  const handleDelete = async () => {
    setDeleting(true);
    setDeleteError(null);
    try {
      await learningApi.deleteAccount();
      setUser(null);
      navigate('/', { replace: true });
    } catch (error) {
      setDeleteError(formatError(error));
      setDeleting(false);
    }
  };

  return (
    <Container>
      <div className="flex flex-col gap-5 sm:flex-row sm:items-end sm:justify-between">
        <div>
          <div className="lc-eyebrow">Your learning</div>
          <h1 className="mt-2 text-3xl font-bold tracking-tight text-slate-950 sm:text-4xl">My learning</h1>
          <p className="mt-2 text-sm text-slate-600">
            {user?.email} · {enrollments.length} {enrollments.length === 1 ? 'course' : 'courses'} ·{' '}
            {overallCompleted} {overallCompleted === 1 ? 'lesson' : 'lessons'} completed
          </p>
        </div>
        <div className="flex flex-wrap gap-2">
          <Link to="/courses" className="lc-button lc-button-secondary">
            <BookOpen className="h-4 w-4" aria-hidden="true" /> Browse catalog
          </Link>
          {canAuthor(user) && (
            <Link to="/author" className="lc-button lc-button-primary">
              Author studio
            </Link>
          )}
        </div>
      </div>

      <div className="mt-8">
        {learning.error ? (
          <ErrorState message={formatError(learning.error)} onRetry={learning.reload} />
        ) : learning.loading ? (
          <LoadingState label="Loading your courses" />
        ) : enrollments.length === 0 ? (
          <EmptyState icon={<GraduationCap className="h-6 w-6" aria-hidden="true" />} title="No courses yet">
            <p>Choose a course from the catalog to begin. Enrollment is free and not automatic.</p>
            <Link to="/courses" className="lc-button lc-button-primary mt-4">Find your first course</Link>
          </EmptyState>
        ) : (
          <div className="space-y-10">
            <section aria-labelledby="in-progress">
              <h2 id="in-progress" className="text-xl font-bold tracking-tight text-slate-950">In progress</h2>
              {active.length === 0 ? (
                <p className="mt-2 text-sm text-slate-600">Nothing in progress. Pick a course to keep learning.</p>
              ) : (
                <div className="mt-4 grid gap-5 lg:grid-cols-2">
                  {active.map((enrollment) => (
                    <EnrollmentCard key={enrollment.id} enrollment={enrollment} />
                  ))}
                </div>
              )}
            </section>

            {completed.length > 0 && (
              <section aria-labelledby="completed-courses">
                <h2 id="completed-courses" className="text-xl font-bold tracking-tight text-slate-950">Completed</h2>
                <div className="mt-4 grid gap-5 lg:grid-cols-2">
                  {completed.map((enrollment) => (
                    <EnrollmentCard key={enrollment.id} enrollment={enrollment} />
                  ))}
                </div>
              </section>
            )}
          </div>
        )}
      </div>

      <section className="mt-16" aria-labelledby="account-data">
        <h2 id="account-data" className="text-xl font-bold tracking-tight text-slate-950">Account and data</h2>
        <div className="mt-4 grid gap-5 lg:grid-cols-2">
          <div className="lc-card p-6">
            <h3 className="font-bold text-slate-950">Your account</h3>
            <dl className="mt-4 space-y-3 text-sm">
              <div>
                <dt className="text-xs font-medium text-slate-500">Email</dt>
                <dd className="mt-0.5 break-all font-medium text-slate-900">{user?.email}</dd>
              </div>
              <div>
                <dt className="text-xs font-medium text-slate-500">Role</dt>
                <dd className="mt-0.5 font-medium capitalize text-slate-900">{user?.role}</dd>
              </div>
              <div>
                <dt className="text-xs font-medium text-slate-500">Member since</dt>
                <dd className="mt-0.5 font-medium text-slate-900">{formatDate(user?.created_at)}</dd>
              </div>
            </dl>
            <button type="button" onClick={handleExport} disabled={exporting} className="lc-button lc-button-secondary mt-5">
              {exporting ? <Loader2 className="h-4 w-4 animate-spin" aria-hidden="true" /> : <Download className="h-4 w-4" aria-hidden="true" />}
              {exporting ? 'Preparing export…' : 'Download my data'}
            </button>
            {exportError && <p className="mt-2 text-xs text-rose-800" role="alert">{exportError}</p>}
            <FieldHint>
              JSON export of your account, enrollments, completed lesson IDs, and quiz attempts.
            </FieldHint>
          </div>

          <div className="lc-card p-6">
            <h3 className="font-bold text-slate-950">Privacy controls</h3>
            {user?.role === 'learner' ? (
              <>
                <p className="mt-2 text-sm leading-6 text-slate-600">
                  Deleting your learning account permanently removes your enrollments, progress, and quiz
                  attempts. This cannot be undone.
                </p>
                {!showDelete ? (
                  <button type="button" onClick={() => setShowDelete(true)} className="lc-button lc-button-danger mt-4">
                    <Trash2 className="h-4 w-4" aria-hidden="true" /> Delete my learning account
                  </button>
                ) : (
                  <div className="mt-4 rounded-lg border border-rose-200 bg-rose-50 p-4">
                    <div className="flex items-start gap-2">
                      <ShieldAlert className="mt-0.5 h-4 w-4 shrink-0 text-rose-800" aria-hidden="true" />
                      <p className="text-sm leading-6 text-rose-950">
                        This deletes the account for <strong>{user?.email}</strong> and all of its learning data.
                        Download your export first if you want a copy.
                      </p>
                    </div>
                    {deleteError && <p className="mt-3 text-xs font-medium text-rose-900" role="alert">{deleteError}</p>}
                    <div className="mt-4 flex flex-wrap items-center gap-2">
                      <ConfirmButton
                        label="Delete account"
                        confirmLabel={deleting ? 'Deleting…' : 'Yes, delete everything'}
                        disabled={deleting}
                        onConfirm={handleDelete}
                      />
                      <button type="button" className="lc-button lc-button-secondary" onClick={() => setShowDelete(false)}>
                        Keep my account
                      </button>
                    </div>
                  </div>
                )}
              </>
            ) : (
              <p className="mt-2 text-sm leading-6 text-slate-600">
                Staff accounts are managed by an administrator. Ask an administrator to remove the account if
                it is no longer needed. You can still export your learning data.
              </p>
            )}
          </div>
        </div>
      </section>
    </Container>
  );
};

const EnrollmentCard: React.FC<{ enrollment: Enrollment }> = ({ enrollment }) => {
  const course = enrollment.course;
  if (!course) {
    return (
      <div className="lc-card p-6">
        <p className="text-sm text-slate-600">This course is no longer available.</p>
      </div>
    );
  }
  const completed = enrollment.status === 'completed';
  const resume = `/learn/${course.slug}${enrollment.last_lesson_id ? `/${enrollment.last_lesson_id}` : ''}`;
  return (
    <article className="lc-card flex h-full flex-col p-5 sm:p-6">
      <div className="flex flex-wrap items-center gap-2">
        <Link to={`/topics/${course.topic_slug}`} className="text-xs font-bold uppercase tracking-[0.14em] text-teal-800 hover:text-teal-950">
          {course.topic_title}
        </Link>
        {completed ? <StatusBadge status="completed" /> : <StatusBadge status="active" label="In progress" />}
        {!enrollment.available && (
          <span className="rounded-full border border-amber-200 bg-amber-50 px-2.5 py-1 text-xs font-semibold text-amber-950">
            Currently unavailable
          </span>
        )}
      </div>
      <h3 className="mt-3 text-lg font-bold leading-snug text-slate-950">
        <Link to={`/courses/${course.slug}`} className="hover:text-teal-900">{course.title}</Link>
      </h3>
      <p className="mt-1 line-clamp-2 text-sm leading-6 text-slate-600">{course.summary}</p>

      <div className="mt-4">
        <div className="mb-1.5 flex items-center justify-between gap-2 text-xs text-slate-500">
          <span>{enrollment.completed_lessons} of {enrollment.total_lessons} lessons</span>
          <span>{completed ? 'Complete' : `${enrollment.progress_percent}%`}</span>
        </div>
        <ProgressBar value={enrollment.progress_percent} label={`Progress in ${course.title}`} showValue={false} />
      </div>

      <div className="mt-5 flex flex-wrap items-center gap-2 border-t border-slate-100 pt-4">
        {enrollment.available ? (
          <Link to={resume} className="lc-button lc-button-primary">
            {completed ? 'Review course' : 'Continue learning'}
            <ArrowRight className="h-4 w-4" aria-hidden="true" />
          </Link>
        ) : (
          <span className="text-xs text-slate-500">Content is temporarily unavailable. Your progress is safe.</span>
        )}
        <Link to={`/courses/${course.slug}`} className="lc-button lc-button-secondary">Course page</Link>
      </div>
    </article>
  );
};

export const PlayerPage: React.FC<{ slug: string; lessonId?: string }> = ({ slug, lessonId }) => {
  const { authReady, user } = useAuth();
  useSeo({
    title: 'Course player',
    description: 'Your enrolled AdControl Learning course.',
    path: `/learn/${slug}`,
    noindex: true,
  });

  if (!authReady) {
    return <Container><LoadingState label="Opening the course" /></Container>;
  }
  if (!user) {
    return <SignInPanel next={`/learn/${slug}`} title="Sign in to open this course" />;
  }
  if (!isLearningUser(user)) {
    return (
      <Container>
        <div className="lc-card mx-auto max-w-lg p-8 text-center">
          <h1 className="text-2xl font-bold text-slate-950">Use a learning account</h1>
          <p className="mt-2 text-sm leading-6 text-slate-600">
            This course player is for learning accounts. Advertising operations accounts cannot enroll.
          </p>
          <a href="/dashboard" className="lc-button lc-button-primary mt-6">Open operations dashboard</a>
        </div>
      </Container>
    );
  }
  return <PlayerContent slug={slug} lessonId={lessonId} />;
};

const PlayerContent: React.FC<{ slug: string; lessonId?: string }> = ({ slug, lessonId }) => {
  const { navigate } = useRouter();
  const course = useRemote(() => learningApi.learningCourse(slug), [slug]);
  const [completedIds, setCompletedIds] = useState<string[]>([]);
  const [enrollment, setEnrollment] = useState<Enrollment | null>(null);
  const [completing, setCompleting] = useState(false);
  const [completeError, setCompleteError] = useState<string | null>(null);
  const [justCompleted, setJustCompleted] = useState(false);

  useEffect(() => {
    if (!course.data) return;
    setCompletedIds(course.data.completed_lesson_ids);
    setEnrollment(course.data.enrollment);
  }, [course.data]);

  const curriculum = course.data?.curriculum;
  const lessons = useMemo(
    () => curriculum?.modules.flatMap((module) => module.lessons) || [],
    [curriculum],
  );
  const activeLesson = useMemo(() => {
    if (lessons.length === 0) return undefined;
    return lessons.find((lesson) => lesson.id === lessonId)
      || lessons.find((lesson) => lesson.id === enrollment?.last_lesson_id)
      || lessons[0];
  }, [enrollment?.last_lesson_id, lessonId, lessons]);
  const activeIndex = activeLesson ? lessons.findIndex((lesson) => lesson.id === activeLesson.id) : -1;

  useEffect(() => {
    setJustCompleted(false);
    setCompleteError(null);
  }, [activeLesson?.id]);

  useEffect(() => {
    if (lessons.length === 0) return;
    if (!lessonId && activeLesson) {
      navigate(`/learn/${slug}/${activeLesson.id}`, { replace: true, scroll: false });
    }
  }, [activeLesson, lessonId, lessons.length, navigate, slug]);

  const markComplete = async () => {
    if (!activeLesson || completedIds.includes(activeLesson.id)) return;
    setCompleting(true);
    setCompleteError(null);
    try {
      const response = await learningApi.completeLesson(activeLesson.id);
      setEnrollment(response.enrollment);
      setCompletedIds((current) => current.includes(activeLesson.id) ? current : [...current, activeLesson.id]);
      setJustCompleted(true);
    } catch (error) {
      setCompleteError(formatError(error));
    } finally {
      setCompleting(false);
    }
  };

  if (course.loading) {
    return <Container><LoadingState label="Loading your course" /></Container>;
  }
  if (course.error) {
    const status = (course.error as { status?: number }).status;
    if (status === 403) {
      return (
        <Container>
          <div className="lc-card mx-auto max-w-lg p-8 text-center">
            <LockKeyhole className="mx-auto h-8 w-8 text-teal-800" aria-hidden="true" />
            <h1 className="mt-4 text-2xl font-bold text-slate-950">Enroll to open this course</h1>
            <p className="mt-2 text-sm leading-6 text-slate-600">
              The lessons are available to enrolled learners. Enrollment is free.
            </p>
            <div className="mt-6 flex flex-col gap-2 sm:flex-row sm:justify-center">
              <Link to={`/courses/${slug}`} className="lc-button lc-button-primary">View course and enroll</Link>
              <Link to="/my-learning" className="lc-button lc-button-secondary">My learning</Link>
            </div>
          </div>
        </Container>
      );
    }
    return (
      <Container>
        <ErrorState title="Course could not be opened" message={formatError(course.error)} onRetry={course.reload} />
        <Link to="/my-learning" className="lc-button lc-button-secondary">Back to My learning</Link>
      </Container>
    );
  }
  if (!course.data || !curriculum || !activeLesson) {
    return (
      <Container>
        <EmptyState title="This course has no published lessons yet">
          <Link to="/my-learning" className="lc-link">Back to My learning</Link>
        </EmptyState>
      </Container>
    );
  }

  const { course: courseInfo, resources, enrollment: currentEnrollment } = course.data;
  const isDone = completedIds.includes(activeLesson.id);
  const completedCount = lessons.filter((lesson) => completedIds.includes(lesson.id)).length;
  const progressPercent = currentEnrollment.total_lessons > 0
    ? Math.round((completedCount * 100) / currentEnrollment.total_lessons)
    : 0;
  const lessonResources = resources.filter((resource) => resource.lesson_id === activeLesson.id);
  const courseResources = resources.filter((resource) => !resource.lesson_id);
  const prevLesson = activeIndex > 0 ? lessons[activeIndex - 1] : undefined;
  const nextLesson = activeIndex >= 0 && activeIndex < lessons.length - 1 ? lessons[activeIndex + 1] : undefined;
  const courseCompleted = (enrollment || currentEnrollment).status === 'completed';

  return (
    <div className="mx-auto w-full max-w-7xl px-4 py-6 sm:px-6 lg:px-8">
      <div className="mb-5 flex flex-wrap items-center justify-between gap-3">
        <div className="flex min-w-0 items-center gap-2 text-sm">
          <Link to="/my-learning" className="inline-flex items-center gap-1 font-medium text-slate-600 hover:text-teal-900">
            <ChevronLeft className="h-4 w-4" aria-hidden="true" /> My learning
          </Link>
          <span className="text-slate-300" aria-hidden="true">/</span>
          <span className="truncate font-semibold text-slate-800">{courseInfo.title}</span>
        </div>
        <Link to={`/courses/${courseInfo.slug}`} className="text-xs font-semibold text-teal-800 hover:text-teal-950">
          Course details
        </Link>
      </div>

      <div className="mb-6 lc-card p-4">
        <div className="flex flex-wrap items-center justify-between gap-2">
          <span className="text-sm font-semibold text-slate-900">
            {completedCount} of {lessons.length} lessons complete
          </span>
          <span className="text-xs text-slate-500">{progressPercent}%</span>
        </div>
        <div className="mt-2">
          <ProgressBar value={progressPercent} label={`Progress in ${courseInfo.title}`} showValue={false} />
        </div>
      </div>

      <div className="grid gap-6 lg:grid-cols-[17rem_minmax(0,1fr)] xl:grid-cols-[19rem_minmax(0,1fr)]">
        <aside className="hidden lg:block">
          <div className="sticky top-24 max-h-[calc(100vh-8rem)] overflow-y-auto rounded-xl border border-slate-200 bg-white p-4">
            <CurriculumNav
              modules={curriculum.modules}
              activeId={activeLesson.id}
              completedIds={completedIds}
              slug={slug}
            />
            <div className="mt-4 border-t border-slate-100 pt-3 text-xs text-slate-500">
              Last reviewed {formatDate(courseInfo.last_reviewed_at)}
            </div>
          </div>
        </aside>

        <div className="min-w-0">
          <details className="lc-card mb-5 p-4 lg:hidden">
            <summary className="cursor-pointer text-sm font-semibold text-slate-900">
              Course outline · {completedCount}/{lessons.length} complete
            </summary>
            <div className="mt-4">
              <CurriculumNav
                modules={curriculum.modules}
                activeId={activeLesson.id}
                completedIds={completedIds}
                slug={slug}
              />
            </div>
          </details>

          <article className="lc-card p-5 sm:p-8">
            <div className="flex flex-wrap items-center gap-2 text-xs text-slate-500">
              <span className="inline-flex items-center gap-1">
                <Clock3 className="h-3.5 w-3.5" aria-hidden="true" />
                {activeLesson.estimated_minutes} min
              </span>
              {activeLesson.is_preview && (
                <span className="rounded-full bg-teal-50 px-2 py-0.5 font-semibold text-teal-900">Preview lesson</span>
              )}
            </div>
            <h1 className="mt-2 text-3xl font-bold leading-tight tracking-tight text-slate-950">{activeLesson.title}</h1>

            {activeLesson.video_provider && activeLesson.video_id && (
              <VideoEmbed
                provider={activeLesson.video_provider}
                videoId={activeLesson.video_id}
                title={activeLesson.title}
              />
            )}

            <div className="mt-6">
              <Markdown source={activeLesson.content_md} emptyMessage="This lesson has no written content yet." />
            </div>

            {lessonResources.length > 0 && (
              <section className="mt-8" aria-labelledby="lesson-resources">
                <h2 id="lesson-resources" className="text-lg font-bold text-slate-950">Lesson resources</h2>
                <ResourceList resources={lessonResources} />
              </section>
            )}

            {activeLesson.quiz && (
              <QuizPanel
                lesson={activeLesson}
                quiz={activeLesson.quiz}
                attempts={course.data.quiz_attempts.filter((attempt) => attempt.lesson_id === activeLesson.id)}
              />
            )}

            <div className="mt-10 border-t border-slate-200 pt-6">
              {completeError && (
                <p className="mb-3 rounded-lg bg-rose-50 p-3 text-sm text-rose-900" role="alert">{completeError}</p>
              )}
              {justCompleted && (
                <div className="mb-4 flex items-start gap-2 rounded-lg border border-emerald-200 bg-emerald-50 p-3 text-sm text-emerald-950" role="status">
                  <CheckCircle2 className="mt-0.5 h-4 w-4 shrink-0" aria-hidden="true" />
                  Lesson progress saved.
                </div>
              )}
              {courseCompleted && (
                <div className="mb-4 flex items-start gap-3 rounded-xl border border-emerald-200 bg-emerald-50 p-4 text-emerald-950" role="status">
                  <Trophy className="mt-0.5 h-5 w-5 shrink-0" aria-hidden="true" />
                  <div>
                    <p className="font-bold">Course complete</p>
                    <p className="mt-0.5 text-sm">Every lesson is marked complete. You can revisit any lesson at any time.</p>
                  </div>
                </div>
              )}
              <div className="flex flex-col gap-3 sm:flex-row sm:items-center sm:justify-between">
                <div>
                  {isDone ? (
                    <span className="inline-flex items-center gap-2 text-sm font-semibold text-emerald-800">
                      <CheckCircle2 className="h-4 w-4" aria-hidden="true" /> Lesson completed
                    </span>
                  ) : (
                    <button type="button" onClick={markComplete} disabled={completing} className="lc-button lc-button-primary">
                      {completing ? <Loader2 className="h-4 w-4 animate-spin" aria-hidden="true" /> : <Check className="h-4 w-4" aria-hidden="true" />}
                      {completing ? 'Saving…' : 'Mark lesson complete'}
                    </button>
                  )}
                </div>
                <div className="flex items-center gap-2">
                  {prevLesson ? (
                    <Link to={`/learn/${slug}/${prevLesson.id}`} className="lc-button lc-button-secondary" aria-label={`Previous lesson: ${prevLesson.title}`}>
                      <ChevronLeft className="h-4 w-4" aria-hidden="true" />
                      <span className="hidden sm:inline">Previous</span>
                    </Link>
                  ) : (
                    <span />
                  )}
                  {nextLesson ? (
                    <Link to={`/learn/${slug}/${nextLesson.id}`} className="lc-button lc-button-primary" aria-label={`Next lesson: ${nextLesson.title}`}>
                      <span className="hidden sm:inline">Next lesson</span>
                      <ChevronRight className="h-4 w-4" aria-hidden="true" />
                    </Link>
                  ) : (
                    <Link to="/my-learning" className="lc-button lc-button-secondary">
                      Back to My learning
                    </Link>
                  )}
                </div>
              </div>
            </div>
          </article>

          {courseResources.length > 0 && (
            <section className="lc-card mt-5 p-5" aria-labelledby="course-resources">
              <h2 id="course-resources" className="font-bold text-slate-950">Course resources</h2>
              <ResourceList resources={courseResources} />
            </section>
          )}
        </div>
      </div>
    </div>
  );
};

const CurriculumNav: React.FC<{
  modules: Array<{ id: string; title: string; lessons: LearningLesson[] }>;
  activeId: string;
  completedIds: string[];
  slug: string;
}> = ({ modules, activeId, completedIds, slug }) => (
  <nav aria-label="Course curriculum">
    <ol className="space-y-5">
      {modules.map((module, moduleIndex) => (
        <li key={module.id}>
          <h2 className="text-xs font-bold uppercase tracking-[0.12em] text-slate-500">
            Module {moduleIndex + 1}
          </h2>
          <p className="mt-1 text-sm font-semibold text-slate-800">{module.title}</p>
          <ul className="mt-2 space-y-0.5">
            {module.lessons.map((lesson) => {
              const current = lesson.id === activeId;
              const done = completedIds.includes(lesson.id);
              return (
                <li key={lesson.id}>
                  <Link
                    to={`/learn/${slug}/${lesson.id}`}
                    aria-current={current ? 'page' : undefined}
                    className={`flex items-start gap-2 rounded-lg px-2 py-2 text-xs leading-5 transition ${
                      current ? 'bg-teal-50 font-semibold text-teal-950' : 'text-slate-600 hover:bg-slate-50 hover:text-slate-900'
                    }`}
                  >
                    {done ? (
                      <CheckCircle2 className="mt-0.5 h-3.5 w-3.5 shrink-0 text-emerald-700" aria-label="Completed" />
                    ) : current ? (
                      <PlayCircle className="mt-0.5 h-3.5 w-3.5 shrink-0 text-teal-700" aria-hidden="true" />
                    ) : (
                      <span className="mt-1.5 h-2 w-2 shrink-0 rounded-full border border-slate-300" aria-hidden="true" />
                    )}
                    <span>{lesson.title}</span>
                  </Link>
                </li>
              );
            })}
          </ul>
        </li>
      ))}
    </ol>
  </nav>
);

const ResourceList: React.FC<{ resources: LearningResource[] }> = ({ resources }) => (
  <ul className="mt-3 divide-y divide-slate-100">
    {resources.map((resource) => (
      <li key={resource.id} className="py-3 first:pt-0 last:pb-0">
        <a
          href={resource.url}
          target="_blank"
          rel="noopener noreferrer"
          className="group flex items-start gap-3"
        >
          <span className="mt-0.5 flex h-8 w-8 shrink-0 items-center justify-center rounded-lg bg-slate-100 text-slate-600 group-hover:bg-teal-50 group-hover:text-teal-800">
            <FileText className="h-4 w-4" aria-hidden="true" />
          </span>
          <span className="min-w-0">
            <span className="flex items-center gap-1.5 text-sm font-semibold text-slate-900 group-hover:text-teal-900">
              {resource.title}
              <ExternalLink className="h-3 w-3 shrink-0 text-slate-400" aria-hidden="true" />
            </span>
            {resource.description && <span className="mt-0.5 block text-xs leading-5 text-slate-500">{resource.description}</span>}
            <span className="mt-0.5 block text-[11px] font-medium uppercase tracking-wide text-slate-400">
              {resourceKindLabel(resource.kind)}
            </span>
          </span>
        </a>
      </li>
    ))}
  </ul>
);

const QuizPanel: React.FC<{
  lesson: LearningLesson;
  quiz: LessonQuiz;
  attempts: QuizAttempt[];
}> = ({ lesson, quiz, attempts }) => {
  const [answers, setAnswers] = useState<Record<string, string>>({});
  const [result, setResult] = useState<QuizAttemptResponse | null>(null);
  const [history, setHistory] = useState<QuizAttempt[]>(attempts);
  const [error, setError] = useState<string | null>(null);
  const [submitting, setSubmitting] = useState(false);

  const answeredCount = Object.keys(answers).length;
  const allAnswered = quiz.questions.length > 0 && answeredCount === quiz.questions.length;

  const submit = async (event: FormEvent) => {
    event.preventDefault();
    setError(null);
    if (!allAnswered) {
      setError('Answer every question before submitting.');
      return;
    }
    setSubmitting(true);
    try {
      const response = await learningApi.submitQuiz(
        lesson.id,
        quiz.questions.map((question) => ({
          question_id: question.id,
          option_id: answers[question.id],
        })),
      );
      setResult(response);
      setHistory((current) => [...current, response.attempt]);
    } catch (err) {
      setError(formatError(err));
    } finally {
      setSubmitting(false);
    }
  };

  const retake = () => {
    setAnswers({});
    setResult(null);
    setError(null);
  };

  const resultFor = (questionId: string) => result?.results.find((item) => item.question_id === questionId);

  return (
    <section className="mt-10 border-t border-slate-200 pt-8" aria-labelledby="knowledge-check">
      <div className="flex flex-wrap items-start justify-between gap-3">
        <div>
          <div className="lc-eyebrow">Knowledge check</div>
          <h2 id="knowledge-check" className="mt-1 text-2xl font-bold tracking-tight text-slate-950">{quiz.title}</h2>
          <p className="mt-1 text-sm text-slate-600">
            {quiz.questions.length} {quiz.questions.length === 1 ? 'question' : 'questions'} · Passing score {quiz.passing_score}%
          </p>
        </div>
        <span className="inline-flex items-center gap-1.5 rounded-full bg-slate-100 px-3 py-1 text-xs font-semibold text-slate-700">
          <ListChecks className="h-3.5 w-3.5" aria-hidden="true" />
          {answeredCount}/{quiz.questions.length} answered
        </span>
      </div>

      {result && (
        <div
          className={`mt-5 rounded-xl border p-5 ${
            result.passed ? 'border-emerald-200 bg-emerald-50' : 'border-amber-200 bg-amber-50'
          }`}
          role="status"
        >
          <div className="flex items-start gap-3">
            {result.passed ? (
              <Trophy className="mt-0.5 h-5 w-5 shrink-0 text-emerald-800" aria-hidden="true" />
            ) : (
              <ListChecks className="mt-0.5 h-5 w-5 shrink-0 text-amber-800" aria-hidden="true" />
            )}
            <div>
              <p className={`font-bold ${result.passed ? 'text-emerald-950' : 'text-amber-950'}`}>
                {result.passed ? 'You passed' : 'Not passed yet'} · {result.score}%
              </p>
              <p className={`mt-1 text-sm ${result.passed ? 'text-emerald-900' : 'text-amber-900'}`}>
                {result.correct_count} of {result.total_questions} correct.
                {result.passed
                  ? ' Explanations are shown with each question below.'
                  : ` You need ${result.passing_score}% to pass. Review the explanations and try again when ready.`}
              </p>
            </div>
          </div>
        </div>
      )}

      {error && (
        <p className="mt-4 rounded-lg border border-rose-200 bg-rose-50 p-3 text-sm text-rose-900" role="alert">{error}</p>
      )}

      <form onSubmit={submit} className={result ? 'mt-6' : 'mt-6'}>
        <ol className="space-y-6">
          {quiz.questions.map((question, questionIndex) => {
            const questionResult = resultFor(question.id);
            return (
              <li key={question.id} className="rounded-xl border border-slate-200 bg-white p-5">
                <fieldset>
                  <legend className="text-base font-semibold leading-6 text-slate-950">
                    <span className="mr-2 text-sm font-bold text-teal-800">{questionIndex + 1}.</span>
                    {question.prompt}
                  </legend>
                  <div className="mt-4 space-y-2">
                    {question.options.map((option) => {
                      const selected = answers[question.id] === option.id;
                      const isCorrect = questionResult?.correct_option_id === option.id;
                      const isWrongSelected = !!questionResult && selected && !questionResult.is_correct;
                      return (
                        <label
                          key={option.id}
                          className={`flex cursor-pointer items-start gap-3 rounded-lg border p-3 text-sm transition ${
                            result
                              ? isCorrect
                                ? 'border-emerald-300 bg-emerald-50'
                                : selected
                                  ? 'border-rose-300 bg-rose-50'
                                  : 'border-slate-200 bg-white'
                              : selected
                                ? 'border-teal-500 bg-teal-50'
                                : 'border-slate-200 bg-white hover:border-slate-300 hover:bg-slate-50'
                          }`}
                        >
                          <input
                            type="radio"
                            name={`quiz-${question.id}`}
                            value={option.id}
                            checked={selected}
                            disabled={!!result || submitting}
                            onChange={() => setAnswers((current) => ({ ...current, [question.id]: option.id }))}
                            className="mt-1 h-4 w-4 border-slate-300 text-teal-700 focus:ring-teal-700"
                          />
                          <span className="min-w-0 flex-1">
                            <span className="text-slate-800">{option.text}</span>
                            {result && isCorrect && (
                              <span className="mt-1 flex items-center gap-1 text-xs font-semibold text-emerald-800">
                                <CheckCircle2 className="h-3.5 w-3.5" aria-hidden="true" /> Correct answer
                              </span>
                            )}
                            {isWrongSelected && (
                              <span className="mt-1 flex items-center gap-1 text-xs font-semibold text-rose-800">
                                <XCircle className="h-3.5 w-3.5" aria-hidden="true" /> Your answer
                              </span>
                            )}
                          </span>
                        </label>
                      );
                    })}
                  </div>
                </fieldset>
                {questionResult && (
                  <div
                    className={`mt-4 rounded-lg p-3 text-sm leading-6 ${
                      questionResult.is_correct ? 'bg-emerald-50 text-emerald-950' : 'bg-slate-50 text-slate-800'
                    }`}
                  >
                    <p className="font-semibold">
                      {questionResult.is_correct ? 'Correct' : 'Review this one'}
                    </p>
                    {questionResult.explanation && <p className="mt-1">{questionResult.explanation}</p>}
                    {!questionResult.explanation && (
                      <p className="mt-1 text-slate-600">No explanation was provided for this question.</p>
                    )}
                  </div>
                )}
              </li>
            );
          })}
        </ol>

        <div className="mt-6 flex flex-wrap items-center gap-3">
          {!result ? (
            <button type="submit" disabled={submitting} className="lc-button lc-button-primary">
              {submitting ? <Loader2 className="h-4 w-4 animate-spin" aria-hidden="true" /> : <ListChecks className="h-4 w-4" aria-hidden="true" />}
              {submitting ? 'Checking…' : 'Submit answers'}
            </button>
          ) : (
            <button type="button" onClick={retake} className="lc-button lc-button-secondary">Try again</button>
          )}
          <span className="text-xs text-slate-500">
            {result ? 'Explanations and the answer key are shown only after a graded attempt.' : 'Your answers are graded by the server.'}
          </span>
        </div>
      </form>

      {history.length > 0 && (
        <details className="mt-6 rounded-lg border border-slate-200 bg-slate-50 p-4">
          <summary className="cursor-pointer text-sm font-semibold text-slate-800">
            Attempt history ({history.length})
          </summary>
          <ul className="mt-3 space-y-2">
            {history.slice().reverse().map((attempt) => (
              <li key={attempt.id} className="flex flex-wrap items-center justify-between gap-2 rounded-lg bg-white px-3 py-2 text-xs">
                <span className="text-slate-500">{formatDate(attempt.attempted_at)}</span>
                <span className="font-semibold text-slate-800">
                  {attempt.correct_count}/{attempt.total_questions} · {attempt.score}% · {attempt.passed ? 'Passed' : 'Not passed'}
                </span>
              </li>
            ))}
          </ul>
        </details>
      )}
    </section>
  );
};
