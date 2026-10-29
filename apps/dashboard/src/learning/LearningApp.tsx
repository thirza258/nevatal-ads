import React, { useMemo } from 'react';
import { AuthProvider } from './state';
import { LearningLayout } from './layout';
import { RouterProvider, useRouter } from './router';
import { AuthorCoursesPage, NewCoursePage, TeamPage, AdminAnalyticsPage } from './pages/author';
import { AuthorCoursePage } from './pages/author-course';
import { LoginPage, RegisterPage } from './pages/auth';
import {
  AccessibilityPage,
  CourseDetailPage,
  CoursesPage,
  HomePage,
  NotFoundPage,
  PathPage,
  PathsPage,
  PrivacyPage,
  TopicPage,
  TopicsPage,
} from './pages/catalog';
import { DocsPage } from './pages/docs';
import { MyLearningPage, PlayerPage } from './pages/learner';
import { BecomeClientPage, BecomePublisherPage } from './pages/partners';

const SLUG_PATTERN = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
const ID_PATTERN = /^[A-Za-z0-9_-]{1,80}$/;

function decodeSegment(segment: string | undefined): string | null {
  if (!segment) return null;
  try {
    const decoded = decodeURIComponent(segment);
    if (!decoded || decoded.includes('/') || decoded.includes('\\') || decoded.length > 120) return null;
    return decoded;
  } catch (_) {
    return null;
  }
}

function slugParam(segment: string | undefined): string | null {
  const decoded = decodeSegment(segment);
  return decoded && SLUG_PATTERN.test(decoded) ? decoded : null;
}

function idParam(segment: string | undefined): string | null {
  const decoded = decodeSegment(segment);
  return decoded && ID_PATTERN.test(decoded) ? decoded : null;
}

const LearningRoutes: React.FC = () => {
  const { path } = useRouter();
  const normalized = path !== '/' ? path.replace(/\/+$/, '') : '/';

  const page = useMemo(() => {
    const segments = normalized.split('/').filter(Boolean);

    if (segments.length === 0) return <HomePage />;

    if (segments.length === 1) {
      switch (segments[0]) {
        case 'courses': return <CoursesPage />;
        case 'topics': return <TopicsPage />;
        case 'learning-paths': return <PathsPage />;
        case 'login': return <LoginPage />;
        case 'register': return <RegisterPage />;
        case 'my-learning': return <MyLearningPage />;
        case 'author': return <AuthorCoursesPage />;
        case 'become-a-publisher': return <BecomePublisherPage />;
        case 'become-a-client': return <BecomeClientPage />;
        case 'docs': return <DocsPage />;
        case 'privacy': return <PrivacyPage />;
        case 'accessibility': return <AccessibilityPage />;
        default: return <NotFoundPage />;
      }
    }

    if (segments.length === 2 && segments[0] === 'courses') {
      const slug = slugParam(segments[1]);
      return slug ? <CourseDetailPage slug={slug} /> : <NotFoundPage />;
    }
    if (segments.length === 2 && segments[0] === 'topics') {
      const slug = slugParam(segments[1]);
      return slug ? <TopicPage slug={slug} /> : <NotFoundPage />;
    }
    if (segments.length === 2 && segments[0] === 'learning-paths') {
      const slug = slugParam(segments[1]);
      return slug ? <PathPage slug={slug} /> : <NotFoundPage />;
    }

    if (segments[0] === 'learn' && (segments.length === 2 || segments.length === 3)) {
      const slug = slugParam(segments[1]);
      if (!slug) return <NotFoundPage />;
      if (segments.length === 2) return <PlayerPage slug={slug} />;
      const lessonId = idParam(segments[2]);
      return lessonId ? <PlayerPage slug={slug} lessonId={lessonId} /> : <NotFoundPage />;
    }

    if (segments[0] === 'author') {
      if (segments.length === 2 && segments[1] === 'team') return <TeamPage />;
      if (segments.length === 2 && segments[1] === 'analytics') return <AdminAnalyticsPage />;
      if (segments.length === 3 && segments[1] === 'courses' && segments[2] === 'new') {
        return <NewCoursePage />;
      }
      if (segments.length === 3 && segments[1] === 'courses') {
        const courseId = idParam(segments[2]);
        return courseId ? <AuthorCoursePage courseId={courseId} /> : <NotFoundPage />;
      }
    }

    return <NotFoundPage />;
  }, [normalized]);

  return <LearningLayout>{page}</LearningLayout>;
};

export const LearningApp: React.FC = () => (
  <RouterProvider>
    <AuthProvider>
      <LearningRoutes />
    </AuthProvider>
  </RouterProvider>
);
