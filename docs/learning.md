# AdControl Learning

AdControl Learning is the Phase 3 marketing-learning area embedded in this
deployment. It is a public catalog of free, practical courses plus a signed-in
learning area for enrollment, progress, quizzes, and authoring. The public
learning app is served at `/`; the advertising operations dashboard remains at
`/dashboard`.

Phase 3 is additive. Phase 1 delivery and Phase 2 publisher/affiliate data,
routes, accounts, and analytics continue to work unchanged. Learning has its own
account roles and does not enroll existing users automatically.

## What is implemented

- Public topic, course, and learning-path browsing with searchable course
  catalog pages.
- A focused launch catalog seeded on first start: eight topics, eight published
  courses, 32 lessons, and four learning paths.
- Free learner registration, enrollment, My Learning, lesson progress, resume,
  course completion, and graded knowledge checks.
- A structured authoring studio for course metadata, modules, lessons,
  downloadable resources, and quizzes.
- Draft → review → approved → published workflow with server-side role checks,
  version increments, revision snapshots, and 180-day content-review dates.
- Publication readiness checks and safe, non-executable Markdown content.
- Course metadata, canonical URLs, Schema.org Course markup, an XML sitemap, and
  crawler-friendly robots rules.
- Aggregate course and author analytics that do not expose named learners.

Paid courses use a manual invoice and entitlement flow: a purchase creates an
invoice, and access is granted after an administrator records settlement. Card
checkout, automatic renewal, tax calculation, certificates, instructor
marketplaces, cohorts, owned-video hosting, and AI tutoring are **not**
implemented. See [Boundaries](#boundaries-and-later-phases).

## Routes

| Route | Audience | Purpose |
| --- | --- | --- |
| `/` | Public | Learning home, featured courses, topics, and paths |
| `/courses` | Public | Searchable, filterable catalog |
| `/courses/<slug>` | Public | Course detail, metadata, preview lessons, enrollment |
| `/topics` | Public | Topic index |
| `/topics/<slug>` | Public | Courses in one topic |
| `/learning-paths` | Public | Curated course sequences |
| `/learning-paths/<slug>` | Public | Learning path detail |
| `/register` | Public | Create a learner account |
| `/login` | Public | Sign in for learning or staff roles |
| `/my-learning` | Signed in | Enrollments, progress, resume, data export, deletion |
| `/learn/<course-slug>` | Enrolled | Course player; redirects to the resume lesson |
| `/learn/<course-slug>/<lesson-id>` | Enrolled | Specific lesson, resources, and quiz |
| `/author` | Instructor, editor, admin | Course list and creation entry |
| `/author/courses/new` | Instructor, editor, admin | Create a draft course |
| `/author/courses/<id>` | Course owner/editor | Metadata, curriculum, quiz, review, revisions, analytics |
| `/author/team` | Admin | List learning accounts and create instructors/editors |
| `/author/analytics` | Admin | Aggregate learning totals |
| `/become-a-publisher` | Public | Publisher application and onboarding explanation |
| `/become-a-client` | Public | Advertising brief and onboarding explanation |
| `/privacy`, `/accessibility` | Public | Student-data, partner-application, and accessibility notices |
| `/dashboard` | Operations roles | The Phase 1/2 advertising dashboard |

Legacy root hashes such as `/#campaigns` still open the operations dashboard
instead of the learning app. The client only loads the public learning bundle or
the dashboard bundle for the matching route.

## Accounts and roles

Learning roles are separate from the advertising roles `admin`, `publisher`, and
`analyst`.

| Role | Created by | Learning access |
| --- | --- | --- |
| `learner` | Self-registration at `/register` | Public catalog, enrollment, player, quizzes, own data export/deletion |
| `instructor` | Admin through `/author/team` or the admin API | Learner access plus own courses; submit and withdraw from review; archive own drafts |
| `editor` | Admin through `/author/team` or the admin API | Learner access plus all courses; approve, publish, unpublish, and archive |
| `admin` | Seeded/back-office account | All learning administration, staff creation, aggregate reports, authoring |

`POST /api/v3/auth/register` always creates a `learner`, even if the request body
contains another role. There is no public staff signup. Learning staff are
provisioned only by an administrator, and their passwords are never returned.

The API authenticates learning sessions independently of the operations API:

- `authenticate_learning` accepts `learner`, `instructor`, `editor`, or `admin`.
- `authenticate_learning_staff` accepts `instructor`, `editor`, or `admin`.
- `authenticate_learning_editor` accepts `editor` or `admin`.
- `authenticate_admin` requires the back-office `admin` role.
- The Phase 1/2 `authenticate` helper rejects learning roles, so publisher and
  analyst accounts cannot read authoring or learner endpoints, and learners
  cannot read the operations dashboard API.

An instructor can see and edit only courses where `author_id` is their own user
ID. Editors and administrators can manage every course. Ownership is checked on
the server for each course, module, lesson, resource, quiz, and analytics
request, not merely hidden in the UI.

The MVP exposes staff creation and listing but no staff-removal endpoint. Plan
staff lifecycle before creating accounts; learner self-deletion cannot remove
staff. Use an explicit, reviewed database operation for offboarding until an
administrator staff-management flow is added.

## Learner journey

### Registration and sign-in

1. A visitor registers with a valid email and a 12–128 character password.
2. The account is created with role `learner`. The response is a normal opaque
   bearer session: no password hash, salt, or role escalation is exposed.
3. Registration is rate-limited to 8 attempts per IP per 5 minutes.
4. Registration does **not** enroll the new user in any course. Enrollment is an
   explicit action from a published course page.

Learning and operations share `POST /api/v1/auth/login` and session storage. The
public learning header sends a signed-in learner to `/my-learning`; staff also
get links to the authoring area. An operations account cannot use the public
enrollment UI.

### Enrollment and access

- Enrollment is free and available only for a `published` course whose topic is
  also `published`.
- The first request creates an enrollment (`201`); repeating it is idempotent
  and returns `200`.
- An unpublished course returns `404` to the catalog, enrollment, lesson, and
  progress endpoints. Draft course IDs and slugs are not a hidden preview path.
- Enrolling is required before lesson content, quizzes, or progress recording is
  available. A signed-in non-enrolled learner receives `403`.
- Enrollment history is retained when a course is unpublished. My Learning marks
  the course unavailable instead of silently deleting progress; re-publishing
  makes it accessible again.

### Player, progress, and completion

- The player lists every active module and lesson in curriculum order.
- Progress is computed over active lessons, not lesson views. Opening a lesson
  does not mark it complete; the learner must choose **Mark lesson complete**.
- The enrollment stores the last completed lesson and a resume position.
  `/learn/<slug>` resumes there when no lesson ID is supplied.
- Completing every active lesson sets the enrollment to `completed` and records
  `completed_at`. Revisiting a completed course does not remove progress.
- Module and lesson deletion is a soft archive. Archived lessons stop counting
  toward completion while historical progress rows remain in the database.
- Resources are displayed at course or lesson level and are normal HTTPS links.
  AdControl does not fetch or proxy them.

### Quizzes and answer-key protection

Each lesson may have one quiz with 1–50 questions, 2–10 options per question,
exactly one correct option, and a passing score from 0–100. A submitted attempt
is scored as `floor(correct / total * 100)` and stored with the score, pass flag,
counts, and timestamp.

The answer key is protected at the API boundary:

- The public course detail never includes quiz objects.
- An enrolled learner receives quizzes **without** `is_correct` or question
  `explanation` fields.
- Only after a graded attempt does the response include the selected option, the
  correct option, `is_correct`, and the explanation, so the learner can review
  the result.
- Authoring responses do include the answer key because the caller is an
  authorized course owner/editor.
- An answer that references an unknown question or option is rejected with
  `400`; duplicate question IDs are denied.

Quizzes are optional. A quiz attempt and a lesson completion are separate
actions; passing a quiz does not automatically mark the lesson complete.

### My Learning, export, and deletion

My Learning shows each enrollment, progress, completion state, last review date,
and availability. It offers two account-data controls:

- **Download my data** returns a JSON attachment containing the signed-in
  account (`id`, email, role, timestamps), enrollments with completed lesson
  IDs, and quiz attempts. It never includes password hashes, salts, or another
  learner's data.
- **Delete my learning account** is available only to a `learner`. It first
  removes the user's sessions and then deletes the user, with enrollments,
  progress, and attempts removed by foreign-key cascades. The token becomes
  invalid immediately (`401`). Instructors, editors, and administrators cannot
  self-delete through this endpoint.

Export is limited to 5 requests per user per 5 minutes. Account deletion is
intended to be irreversible; backups may retain a copy until their retention
expires. See [backup-restore.md](backup-restore.md).

## Catalog and public exposure

Public catalog responses use a deliberately reduced course shape. Course detail
includes title, summary, description, outcomes, duration, level, topic, version,
publication/review dates, and a curriculum. It does not include author identity,
reviewer identity, draft content, or answer keys.

For an anonymous visitor:

- Preview lessons expose their written content and approved video embedding.
- Non-preview lessons remain in the outline but return `content_md: ""`,
  `video_*: ""`, and `is_locked: true`.
- No lesson quiz object is present.

A draft course, an archived course, or a course in an unpublished topic is absent
from the list, topic page, path, sitemap, and detail endpoint. Enrollment also
re-checks published status and topic status. Search-result hygiene is not the
access control; server-side status checks are.

### Authoring workflow

| Current status | Target | Who may do it |
| --- | --- | --- |
| `draft` | `in_review` | Owning instructor, editor, admin |
| `in_review` | `draft` | Owning instructor, editor, admin |
| `in_review` | `approved` | Editor, admin only |
| `approved` | `published` | Editor, admin only |
| `approved`, `published`, `archived` | `draft` | Editor, admin only |
| Any non-archived | `archived` | Editor, admin |
| `draft` | `archived` | Owning instructor |

Only a `draft` course can change metadata, modules, lessons, resources, or
quizzes. Other statuses return `409`; the authoring UI moves an editable course
back to draft first. Workflow transitions, course create/update, module and lesson
create/archive, resource create/delete, and quiz replace/delete are audited.
In-place module and lesson field edits are not separately audited in this MVP;
revision snapshots capture published curriculum versions.

Approval and publication both require the course to be ready:

- at least one active module;
- at least one active lesson;
- at least one active lesson in every active module; and
- non-empty written content in every active lesson.

Publishing increments the course version and writes an immutable curriculum
snapshot to the revision history. Version 0 is the initial working draft; seeded
courses begin published at version 1. Unpublishing keeps revisions and learner
history; republishing creates the next version and a new snapshot.

On publication, `last_reviewed_at` is refreshed and `next_review_due_at` is set
180 days later; `published_at` keeps the first publication date. The course
detail and authoring views surface these dates. The review interval is a
content-governance reminder, not an automatic unpublish trigger.

Topics and learning paths are seeded and exposed as read-only catalog
structures in this MVP. Staff can choose a topic for a course; there is no topic
or path editor UI/API yet. The seed catalog is inserted once and is not recreated
after an operator edits or archives it.

## Content safety

Lesson and course descriptions use a restricted Markdown subset rendered directly
to React nodes. The renderer does not use `dangerouslySetInnerHTML`,
`innerHTML`, `eval`, or `new Function`, and raw HTML is displayed as text rather
than executed.

Server-side validation rejects unsupported markup, including `<script`,
`<iframe>`, `<object>`, `<embed>`, `<style>`, `<link>`, `<meta`, `javascript:`,
`vbscript:`, `data:text/html`, `data:application/`, `file://`, and common inline
event handlers. Titles, summaries, module fields, quiz fields, resource text, and
status notes are plain text. Control characters are rejected.

Video is allowlisted to two providers:

- `youtube` with a 6–64 character `[A-Za-z0-9_-]` video ID;
- `vimeo` with a 6–64 character `[A-Za-z0-9_-]` video ID.

The browser renders only the corresponding privacy-enhanced embed URL.
nginx's CSP allows frames only from
`https://www.youtube-nocookie.com` and `https://player.vimeo.com`; arbitrary
iframes, scripts, or third-party hosts are blocked.

Resource URLs must be HTTPS, pass the shared safe-URL validator, and are at most
2048 characters. They are links only; AdControl never fetches them, so authoring
does not create an SSRF surface. Authors should use durable, trustworthy
destinations and must not require learners to download executable files.

Practical exercises may reference the AdControl sandbox, but they must be
non-destructive: never instruct a learner to change production campaigns, ad
destinations, DNS, credentials, or other live systems automatically. The
learning area has no write path into Phase 2 inventory.

## SEO, sitemap, and robots

The public learning app updates `document.title`, description, Open Graph tags,
Twitter metadata, and a canonical link per route. Course detail pages also emit
Schema.org `Course` microdata, including `name`, `description`, `courseCode`,
`educationalLevel`, provider, and `isAccessibleForFree`.

The API generates `/sitemap.xml` from `PUBLIC_BASE_URL`:

- `/`, `/courses`, `/topics`, `/learning-paths`;
- every published topic;
- every published course whose topic is published; and
- every published learning path.

It never includes drafts, authoring pages, account pages, the player, or the
operations dashboard. `<lastmod>` uses the entity's `updated_at`; entries are
XML-escaped. The sitemap response is cached for 30 minutes.

`/robots.txt` uses `User-agent: *` with `Allow: /`, `/courses`, `/topics`, and
`/learning-paths`. It disallows `/api/`, `/author`, `/my-learning`, and
`/dashboard` to keep private app shells out of search results, and advertises
the sitemap. These rules are crawl hints only; authorization remains server-side.

`PUBLIC_BASE_URL` must be an absolute HTTPS origin with no path or trailing
slash. An invalid or HTTP value falls back to `https://marketing.nevatal.id` so
canonical URLs are never downgraded. Metadata is applied client-side; search or
social crawlers that do not execute JavaScript may see only the default shell
metadata. Server-side prerendering is a later enhancement.

## Accessibility and performance

The learning UI targets WCAG 2.2 AA. It includes a skip link, landmarks, a
managed main-content focus target, labeled form controls and buttons, visible
focus styles, semantic headings, live status regions for progress and quiz
feedback, keyboard-operable curriculum navigation, and reduced-motion-aware
transitions where present.

The layout is responsive and tested at a 390 px phone width for horizontal
overflow. The public learning bundle and the operations dashboard are separate
lazy chunks, so visiting `/` does not download the advertising dashboard code.
Hashed assets are cached immutably; document and API responses are `no-store`
except public catalog responses (2 minutes) and the sitemap/robots cache (30
minutes).

The automated browser smoke test checks route rendering, canonical metadata,
phone-width overflow, dashboard entry, and console/CSP errors. It does not
replace a manual WCAG 2.2 AA audit with assistive technology.

## Privacy and reporting

Learning data is intentionally limited to:

- the account email, password hash, salt, role, and timestamps;
- enrollments (course, status, last completed lesson, completion date);
- completed lesson IDs and quiz-attempt scores/counts/timestamps;
- authoring content, revisions, and audit rows.

Course analytics are aggregate: enrollments, active enrollments, completions,
average progress, quiz attempts, and average quiz score. The author endpoint
returns aggregates only for the courses the caller may manage. The administrator
endpoint returns aggregate totals plus course-level counts and author email; it
does **not** return named learner activity, individual progress, or attempts.
The note in both responses states this explicitly.

Learner IP addresses, persistent visitor identifiers, and cookies are not used by
the learning catalog. Learning requests use the normal session token in the
`Authorization` header. Staff should not paste personal data into course content,
quiz explanations, or learner support notes.

The public partner pages are separate from learning accounts. Applications store
the submitted name, email, company, website, message, and timestamps for
administrator review only; they are documented in
[security.md](security.md#partner-inquiry-data), and the learning privacy page
links to them. An application never creates a learner or advertising account.

## Operations and verification

The learning schema is additive and created at API startup. Back up before an
upgrade. The seed is guarded by `learning_seed_state` (`launch-catalog-v1`), so
restarting does not duplicate or overwrite edited catalog content.

After deployment, verify:

```bash
# Public learning app and operations dashboard are distinct routes.
curl -fsSI https://marketing.nevatal.id/ | head
curl -fsSI https://marketing.nevatal.id/dashboard | head

# SEO surfaces are served by the API through nginx.
curl -fsS https://marketing.nevatal.id/robots.txt
curl -fsS https://marketing.nevatal.id/sitemap.xml | head -30

# Public catalog is available; authoring requires authentication.
curl -fsS https://marketing.nevatal.id/api/v3/catalog/courses | jq '.courses | length'
curl -sS -o /dev/null -w '%{http_code}\n' https://marketing.nevatal.id/api/v3/author/courses
```

The last request should be `401`. Create staff accounts as an administrator at
`/author/team` or with `POST /api/v3/admin/learning/users`; share initial
credentials out of band. To change public SEO origins, set `PUBLIC_BASE_URL` in
`deploy/.env` and recreate the API container.

The integration suite starts an isolated API container and covers catalog
visibility, SEO output, role boundaries, enrollment, answer-key protection,
progress, quiz grading, revisions, unpublish/republish, export, deletion, and
aggregate reporting:

```bash
bash scripts/test-api.sh
```

The browser smoke suite covers public rendering, canonical URLs, phone width,
and dashboard entry:

```bash
bash tests/browser/run.sh
```

The stack suite additionally checks the public learning shell, `/dashboard`, CSP
frame allowlist, sitemap/robots proxying, cache headers, and catalog access
through nginx.

## Boundaries and later phases

- **Manual paid-course settlement.** Courses can be free or paid. A paid course
  creates a manual invoice; enrollment requires an active entitlement, which is
  granted when an administrator records invoice payment. There is no card
  checkout, coupon engine, automatic renewal, tax calculation, certificate, or
  cohort. The public catalog shows the price and purchase state.
- **No certificates or credentials.** Completion is an AdControl record, not an
  accredited certificate.
- **No learner social features.** There are no discussions, profiles, cohorts,
  direct messages, or public learner lists.
- **No named learner analytics.** Course and admin reports are aggregate only.
- **No owned video hosting.** Videos are allowlisted embeds; AdControl does not
  upload, transcode, or proxy media.
- **No AI tutor.** Course content is authored and reviewed by people.
- **No authoring UI for topics or paths.** They are seeded and read-only in this
  MVP.
- **No staff offboarding endpoint.** Learner self-deletion exists; staff removal
  is an operator procedure.
- **No automatic AdControl production changes.** Exercises may refer to the
  sandbox, never mutate live inventory.
- **No server-side rendering.** Route metadata and Course markup are applied by
  the client; the sitemap is server-generated.

Related: [API reference](api.md#learning-api), [architecture](architecture.md),
[security](security.md), [operations](operations.md), and
[browser tests](../tests/browser/README.md).
