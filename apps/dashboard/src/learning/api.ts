import { clearStoredToken, getStoredToken, setStoredToken } from '../api';
import {
  AdminAnalytics,
  AdminUsersResponse,
  AuthResponse,
  AuthenticatedCourseResponse,
  AuthorCoursesResponse,
  AuthoringPayload,
  CompleteLessonResponse,
  CourseAnalytics,
  CourseDetailResponse,
  CourseListResponse,
  CourseRevisionDetail,
  CourseStatusResponse,
  EnrollResponse,
  LearningPathDetailResponse,
  LearningPathsResponse,
  MyLearningResponse,
  PartnerInquiryInput,
  PartnerInquiryResponse,
  PurchaseDetailResponse,
  PurchaseHistoryResponse,
  PurchaseResponse,
  QuizAttemptResponse,
  TopicDetailResponse,
  TopicListResponse,
} from './types';

export class LearningApiError extends Error {
  status: number;

  constructor(message: string, status: number) {
    super(message);
    this.name = 'LearningApiError';
    this.status = status;
  }
}

interface RequestOptions extends RequestInit {
  skipUnauthorizedEvent?: boolean;
}

async function errorFromResponse(res: Response): Promise<LearningApiError> {
  let message = `Request failed (${res.status})`;
  try {
    const data = await res.json();
    if (typeof data?.error === 'string') message = data.error;
  } catch (_) {
    // Keep the status-based message for non-JSON responses.
  }
  return new LearningApiError(message, res.status);
}

async function request<T>(endpoint: string, options: RequestOptions = {}): Promise<T> {
  const { skipUnauthorizedEvent, ...fetchOptions } = options;
  const token = getStoredToken();
  const headers: Record<string, string> = {
    ...((fetchOptions.headers as Record<string, string> | undefined) || {}),
  };
  if (token) headers.Authorization = `Bearer ${token}`;
  if (fetchOptions.body && !headers['Content-Type']) {
    headers['Content-Type'] = 'application/json';
  }

  let res: Response;
  try {
    res = await fetch(endpoint, { ...fetchOptions, headers });
  } catch (_) {
    throw new LearningApiError('The learning service could not be reached. Check your connection and try again.', 0);
  }

  if (res.status === 401) {
    clearStoredToken();
    if (!skipUnauthorizedEvent) window.dispatchEvent(new Event('learning:unauthorized'));
    throw new LearningApiError('Your session expired. Sign in again to continue.', 401);
  }
  if (!res.ok) throw await errorFromResponse(res);
  if (res.status === 204) return undefined as T;

  const text = await res.text();
  if (!text) return undefined as T;
  try {
    return JSON.parse(text) as T;
  } catch (_) {
    throw new LearningApiError('The server returned an invalid response.', res.status);
  }
}

function queryString(params: Record<string, string | number | undefined>): string {
  const query = new URLSearchParams();
  for (const [key, value] of Object.entries(params)) {
    if (value !== undefined && value !== '') query.set(key, String(value));
  }
  const text = query.toString();
  return text ? `?${text}` : '';
}

export const learningApi = {
  async login(email: string, password: string): Promise<AuthResponse> {
    const data = await request<AuthResponse>('/api/v1/auth/login', {
      method: 'POST',
      body: JSON.stringify({ email, password }),
      skipUnauthorizedEvent: true,
    });
    setStoredToken(data.token);
    return data;
  },

  async register(email: string, password: string): Promise<AuthResponse> {
    const data = await request<AuthResponse>('/api/v3/auth/register', {
      method: 'POST',
      body: JSON.stringify({ email, password }),
      skipUnauthorizedEvent: true,
    });
    setStoredToken(data.token);
    return data;
  },

  async submitPartnerInquiry(input: PartnerInquiryInput): Promise<PartnerInquiryResponse> {
    return request<PartnerInquiryResponse>('/api/v3/partners/inquiries', {
      method: 'POST',
      body: JSON.stringify(input),
      skipUnauthorizedEvent: true,
    });
  },

  async logout(): Promise<void> {
    try {
      await request('/api/v1/auth/logout', { method: 'POST', skipUnauthorizedEvent: true });
    } finally {
      clearStoredToken();
    }
  },

  async getMe(): Promise<AuthResponse['user']> {
    const data = await request<{ user: AuthResponse['user'] }>('/api/v1/auth/me', {
      skipUnauthorizedEvent: true,
    });
    return data.user;
  },

  async getCourses(params: { topic?: string; q?: string; limit?: number } = {}): Promise<CourseListResponse> {
    return request<CourseListResponse>(`/api/v3/catalog/courses${queryString(params)}`);
  },

  async getCourse(slug: string): Promise<CourseDetailResponse> {
    return request<CourseDetailResponse>(`/api/v3/catalog/courses/${encodeURIComponent(slug)}`);
  },

  async getTopics(): Promise<TopicListResponse> {
    return request<TopicListResponse>('/api/v3/catalog/topics');
  },

  async getTopic(slug: string): Promise<TopicDetailResponse> {
    return request<TopicDetailResponse>(`/api/v3/catalog/topics/${encodeURIComponent(slug)}`);
  },

  async getPaths(): Promise<LearningPathsResponse> {
    return request<LearningPathsResponse>('/api/v3/catalog/learning-paths');
  },

  async getPath(slug: string): Promise<LearningPathDetailResponse> {
    return request<LearningPathDetailResponse>(`/api/v3/catalog/learning-paths/${encodeURIComponent(slug)}`);
  },

  async enroll(slug: string): Promise<EnrollResponse> {
    return request<EnrollResponse>(`/api/v3/learning/courses/${encodeURIComponent(slug)}/enroll`, {
      method: 'POST',
    });
  },

  async purchaseCourse(slug: string): Promise<PurchaseResponse> {
    return request<PurchaseResponse>(
      `/api/v3/learning/courses/${encodeURIComponent(slug)}/purchase`,
      { method: 'POST' },
    );
  },

  async purchases(): Promise<PurchaseHistoryResponse> {
    return request<PurchaseHistoryResponse>('/api/v3/learning/purchases');
  },

  async purchase(invoiceId: string): Promise<PurchaseDetailResponse> {
    return request<PurchaseDetailResponse>(
      `/api/v3/learning/purchases/${encodeURIComponent(invoiceId)}`,
    );
  },

  async myLearning(): Promise<MyLearningResponse> {
    return request<MyLearningResponse>('/api/v3/learning/my-learning');
  },

  async learningCourse(slug: string): Promise<AuthenticatedCourseResponse> {
    return request<AuthenticatedCourseResponse>(`/api/v3/learning/courses/${encodeURIComponent(slug)}`);
  },

  async completeLesson(lessonId: string): Promise<CompleteLessonResponse> {
    return request<CompleteLessonResponse>(
      `/api/v3/learning/lessons/${encodeURIComponent(lessonId)}/complete`,
      { method: 'POST' },
    );
  },

  async submitQuiz(
    lessonId: string,
    answers: Array<{ question_id: string; option_id: string }>,
  ): Promise<QuizAttemptResponse> {
    return request<QuizAttemptResponse>(
      `/api/v3/learning/lessons/${encodeURIComponent(lessonId)}/quiz/attempt`,
      { method: 'POST', body: JSON.stringify({ answers }) },
    );
  },

  async exportData(): Promise<Blob> {
    const token = getStoredToken();
    const res = await fetch('/api/v3/learning/me/export', {
      headers: token ? { Authorization: `Bearer ${token}` } : {},
    });
    if (res.status === 401) {
      clearStoredToken();
      window.dispatchEvent(new Event('learning:unauthorized'));
      throw new LearningApiError('Your session expired. Sign in again to continue.', 401);
    }
    if (!res.ok) throw await errorFromResponse(res);
    return res.blob();
  },

  async deleteAccount(): Promise<void> {
    await request('/api/v3/learning/me', { method: 'DELETE' });
    clearStoredToken();
  },

  async authorTopics(): Promise<TopicListResponse> {
    return request<TopicListResponse>('/api/v3/author/topics');
  },

  async authorCourses(status?: string): Promise<AuthorCoursesResponse> {
    return request<AuthorCoursesResponse>(`/api/v3/author/courses${queryString({ status })}`);
  },

  async authorCourse(courseId: string): Promise<AuthoringPayload> {
    return request<AuthoringPayload>(`/api/v3/author/courses/${encodeURIComponent(courseId)}`);
  },

  async createCourse(input: Record<string, unknown>): Promise<AuthoringPayload> {
    return request<AuthoringPayload>('/api/v3/author/courses', {
      method: 'POST',
      body: JSON.stringify(input),
    });
  },

  async updateCourse(courseId: string, input: Record<string, unknown>): Promise<AuthoringPayload> {
    return request<AuthoringPayload>(`/api/v3/author/courses/${encodeURIComponent(courseId)}`, {
      method: 'PATCH',
      body: JSON.stringify(input),
    });
  },

  async setCourseStatus(courseId: string, status: string, note: string): Promise<CourseStatusResponse> {
    return request<CourseStatusResponse>(
      `/api/v3/author/courses/${encodeURIComponent(courseId)}/status`,
      { method: 'POST', body: JSON.stringify({ status, note }) },
    );
  },

  async createModule(courseId: string, input: Record<string, unknown>): Promise<void> {
    await request(`/api/v3/author/courses/${encodeURIComponent(courseId)}/modules`, {
      method: 'POST',
      body: JSON.stringify(input),
    });
  },

  async updateModule(moduleId: string, input: Record<string, unknown>): Promise<void> {
    await request(`/api/v3/author/modules/${encodeURIComponent(moduleId)}`, {
      method: 'PATCH',
      body: JSON.stringify(input),
    });
  },

  async archiveModule(moduleId: string): Promise<void> {
    await request(`/api/v3/author/modules/${encodeURIComponent(moduleId)}`, { method: 'DELETE' });
  },

  async createLesson(courseId: string, input: Record<string, unknown>): Promise<void> {
    await request(`/api/v3/author/courses/${encodeURIComponent(courseId)}/lessons`, {
      method: 'POST',
      body: JSON.stringify(input),
    });
  },

  async updateLesson(lessonId: string, input: Record<string, unknown>): Promise<void> {
    await request(`/api/v3/author/lessons/${encodeURIComponent(lessonId)}`, {
      method: 'PATCH',
      body: JSON.stringify(input),
    });
  },

  async archiveLesson(lessonId: string): Promise<void> {
    await request(`/api/v3/author/lessons/${encodeURIComponent(lessonId)}`, { method: 'DELETE' });
  },

  async createResource(courseId: string, input: Record<string, unknown>): Promise<void> {
    await request(`/api/v3/author/courses/${encodeURIComponent(courseId)}/resources`, {
      method: 'POST',
      body: JSON.stringify(input),
    });
  },

  async deleteResource(resourceId: string): Promise<void> {
    await request(`/api/v3/author/resources/${encodeURIComponent(resourceId)}`, { method: 'DELETE' });
  },

  async saveQuiz(lessonId: string, input: Record<string, unknown>): Promise<void> {
    await request(`/api/v3/author/lessons/${encodeURIComponent(lessonId)}/quiz`, {
      method: 'PUT',
      body: JSON.stringify(input),
    });
  },

  async deleteQuiz(lessonId: string): Promise<void> {
    await request(`/api/v3/author/lessons/${encodeURIComponent(lessonId)}/quiz`, { method: 'DELETE' });
  },

  async revision(courseId: string, revisionId: string): Promise<CourseRevisionDetail> {
    return request<CourseRevisionDetail>(
      `/api/v3/author/courses/${encodeURIComponent(courseId)}/revisions/${encodeURIComponent(revisionId)}`,
    );
  },

  async courseAnalytics(courseId: string): Promise<CourseAnalytics> {
    return request<CourseAnalytics>(
      `/api/v3/author/courses/${encodeURIComponent(courseId)}/analytics`,
    );
  },

  async adminUsers(): Promise<AdminUsersResponse> {
    return request<AdminUsersResponse>('/api/v3/admin/learning/users');
  },

  async createStaff(input: { email: string; password: string; role: string }): Promise<void> {
    await request('/api/v3/admin/learning/users', {
      method: 'POST',
      body: JSON.stringify(input),
    });
  },

  async adminAnalytics(): Promise<AdminAnalytics> {
    return request<AdminAnalytics>('/api/v3/admin/learning/analytics');
  },
};
