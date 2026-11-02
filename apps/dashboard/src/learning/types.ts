export type LearningRole = 'learner' | 'instructor' | 'editor' | 'admin';
export type BackOfficeRole = 'publisher' | 'analyst';

export interface LearningUser {
  id: string;
  email: string;
  role: LearningRole | BackOfficeRole | string;
  publisher_id?: string;
  created_at: string;
  updated_at?: string;
}

export interface AuthResponse {
  token: string;
  expires_at: string;
  user: LearningUser;
}

export interface LearningTopic {
  id: string;
  slug: string;
  title: string;
  description: string;
  status: 'draft' | 'published' | 'archived' | string;
  sort_order: number;
  created_at?: string;
  updated_at?: string;
  course_count?: number;
}

export interface PublicCourse {
  id: string;
  slug: string;
  topic_id: string;
  topic_slug: string;
  topic_title: string;
  title: string;
  summary: string;
  description_md: string;
  level: 'beginner' | 'intermediate' | 'advanced' | string;
  duration_minutes: number;
  outcomes: string[];
  price_cents: number;
  currency: string;
  access_type: 'free' | 'paid' | string;
  version: number;
  published_at: string;
  last_reviewed_at: string;
  next_review_due_at: string;
  updated_at: string;
}

export interface BillingInvoiceItem {
  id: string;
  invoice_id: string;
  kind: 'subscription' | 'overage' | 'course' | 'adjustment' | string;
  description: string;
  quantity: number;
  unit_amount_cents: number;
  amount_cents: number;
  course_id: string;
  metadata: Record<string, unknown>;
  created_at: string;
}

export interface BillingInvoice {
  id: string;
  publisher_id: string;
  user_id: string;
  context: 'subscription' | 'course' | 'manual' | string;
  period_start: string;
  period_end: string;
  currency: string;
  subtotal_cents: number;
  tax_cents: number;
  total_cents: number;
  amount_paid_cents: number;
  balance_cents: number;
  status: 'open' | 'partially_paid' | 'paid' | 'overdue' | 'void' | string;
  due_at: string;
  issued_at: string;
  paid_at: string;
  notes: string;
  created_at: string;
  updated_at: string;
  items: BillingInvoiceItem[];
}

export interface BillingPayment {
  id: string;
  invoice_id: string;
  publisher_id: string;
  user_id: string;
  amount_cents: number;
  currency: string;
  method: string;
  reference: string;
  notes: string;
  received_at: string;
  recorded_by: string;
  created_at: string;
}

export interface CourseEntitlement {
  id: string;
  user_id: string;
  course_id: string;
  invoice_id: string;
  status: 'active' | 'revoked' | string;
  granted_at: string;
  revoked_at: string;
  created_at: string;
  course?: PublicCourse | null;
}

export interface PurchaseResponse {
  invoice: BillingInvoice;
  payment_required: boolean;
}

export interface PurchaseHistoryResponse {
  purchases: BillingInvoice[];
  entitlements: CourseEntitlement[];
  payments: BillingPayment[];
}

export interface PurchaseDetailResponse {
  invoice: BillingInvoice;
  payments: BillingPayment[];
}

export interface QuizOption {
  id: string;
  question_id: string;
  text: string;
  sort_order: number;
  is_correct?: boolean;
}

export interface QuizQuestion {
  id: string;
  prompt: string;
  sort_order: number;
  options: QuizOption[];
  explanation?: string;
}

export interface LessonQuiz {
  id: string;
  lesson_id: string;
  course_id: string;
  title: string;
  passing_score: number;
  questions: QuizQuestion[];
  created_at: string;
  updated_at: string;
}

export interface LearningLesson {
  id: string;
  course_id: string;
  module_id: string;
  slug: string;
  title: string;
  content_md: string;
  estimated_minutes: number;
  sort_order: number;
  is_preview: boolean;
  is_locked?: boolean;
  completed?: boolean;
  status?: 'active' | 'archived' | string;
  video_provider: 'youtube' | 'vimeo' | '' | string;
  video_id: string;
  created_at?: string;
  updated_at?: string;
  quiz?: LessonQuiz | null;
}

export interface LearningModule {
  id: string;
  course_id: string;
  title: string;
  summary: string;
  sort_order: number;
  status: 'active' | 'archived' | string;
  created_at?: string;
  updated_at?: string;
  lessons: LearningLesson[];
}

export interface Curriculum {
  modules: LearningModule[];
  module_count: number;
  lesson_count: number;
}

export interface LearningResource {
  id: string;
  course_id: string;
  lesson_id: string;
  title: string;
  description: string;
  url: string;
  kind: 'link' | 'download' | 'tool' | string;
  sort_order: number;
  created_at: string;
}

export interface Enrollment {
  id: string;
  course_id: string;
  status: 'active' | 'completed' | string;
  last_lesson_id: string;
  enrolled_at: string;
  completed_at: string;
  updated_at: string;
  completed_lessons: number;
  total_lessons: number;
  progress_percent: number;
  course?: PublicCourse | null;
  available?: boolean;
  last_reviewed_at?: string;
}

export interface QuizAttempt {
  id: string;
  enrollment_id: string;
  course_id: string;
  lesson_id: string;
  quiz_id: string;
  score: number;
  passed: boolean;
  correct_count: number;
  total_questions: number;
  attempted_at: string;
}

export interface CourseDetailResponse {
  course: PublicCourse;
  curriculum: Curriculum;
  has_preview: boolean;
  enrollment_required: boolean;
}

export interface CourseListResponse {
  courses: PublicCourse[];
}

export interface TopicListResponse {
  topics: LearningTopic[];
}

export interface TopicDetailResponse {
  topic: LearningTopic;
  courses: PublicCourse[];
}

export interface LearningPath {
  id: string;
  slug: string;
  title: string;
  description: string;
  level: string;
  status: string;
  sort_order: number;
  published_at: string;
  created_at: string;
  updated_at: string;
  courses?: PublicCourse[];
}

export interface LearningPathsResponse {
  learning_paths: LearningPath[];
}

export interface LearningPathDetailResponse {
  learning_path: LearningPath;
  courses: PublicCourse[];
}

export interface MyLearningResponse {
  enrollments: Enrollment[];
}

export interface EnrollResponse {
  created: boolean;
  enrollment: Enrollment;
  course: PublicCourse;
}

export interface AuthenticatedCourseResponse {
  course: PublicCourse;
  curriculum: Curriculum;
  resources: LearningResource[];
  enrollment: Enrollment;
  completed_lesson_ids: string[];
  quiz_attempts: QuizAttempt[];
}

export interface CompleteLessonResponse {
  completed: boolean;
  enrollment: Enrollment;
}

export interface QuizAttemptResult {
  question_id: string;
  selected_option_id: string;
  correct_option_id: string;
  is_correct: boolean;
  explanation: string;
}

export interface QuizAttemptResponse {
  attempt: QuizAttempt;
  score: number;
  passed: boolean;
  passing_score: number;
  correct_count: number;
  total_questions: number;
  results: QuizAttemptResult[];
  note: string;
}

export interface LearningExport {
  exported_at: string;
  user: LearningUser;
  enrollments: Array<Enrollment & { completed_lesson_ids: string[] }>;
  quiz_attempts: QuizAttempt[];
  note: string;
}

export interface AuthorCourse extends PublicCourse {
  status: 'draft' | 'in_review' | 'approved' | 'published' | 'archived' | string;
  author_id: string;
  author_email: string;
  reviewer_id: string;
  reviewer_email: string;
  created_at: string;
}

export interface CourseRevisionSummary {
  id: string;
  version: number;
  created_by: string;
  created_by_email: string;
  note: string;
  created_at: string;
}

export interface AuthoringPayload {
  course: AuthorCourse;
  modules: LearningModule[];
  resources: LearningResource[];
  revisions?: CourseRevisionSummary[];
}

export interface CourseAnalytics {
  course_id: string;
  enrollments: number;
  active_enrollments: number;
  completions: number;
  average_progress_percent: number;
  quiz_attempts: number;
  average_quiz_score: number;
  note: string;
}

export interface CourseRevisionDetail extends CourseRevisionSummary {
  course_id: string;
  snapshot: {
    course?: AuthorCourse;
    modules?: Array<LearningModule & { lessons: LearningLesson[] }>;
    resources?: LearningResource[];
  };
}

export interface AdminAnalytics {
  users_by_role: {
    learners: number;
    instructors: number;
    editors: number;
  };
  totals: {
    enrollments: number;
    completions: number;
    quiz_attempts: number;
  };
  courses: Array<{
    course_id: string;
    slug: string;
    title: string;
    status: string;
    author_email: string;
    analytics: CourseAnalytics;
  }>;
  note: string;
}

export interface AuthorCoursesResponse {
  courses: AuthorCourse[];
}

export interface AdminUsersResponse {
  users: LearningUser[];
}

export interface CourseStatusResponse {
  course: AuthorCourse;
  status: string;
}

export type PartnerInquiryKind = 'publisher' | 'client';

export interface PartnerInquiryInput {
  kind: PartnerInquiryKind;
  name: string;
  email: string;
  company: string;
  website: string;
  message: string;
  source?: string;
  utm_source?: string;
  utm_medium?: string;
  utm_campaign?: string;
  utm_term?: string;
  utm_content?: string;
  referrer?: string;
  /** Hidden anti-bot field; people never see or fill it. */
  fax?: string;
}

export interface PartnerInquiryResponse {
  status: 'received';
}
