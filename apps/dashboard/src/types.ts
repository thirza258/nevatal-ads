export type UserRole = 'admin' | 'publisher' | 'analyst';

export interface User {
  id: string;
  email: string;
  role: UserRole | string;
  publisher_id?: string;
  created_at: string;
}

export type AdSourceType = 'house' | 'direct' | 'affiliate' | 'external_network';
export type AdFormat =
  | 'responsive_banner'
  | 'image'
  | 'text_link'
  | 'native_card'
  | 'product_card';

export interface Site {
  id: string;
  workspace_id: string;
  publisher_id: string;
  name: string;
  primary_domain: string;
  allowed_domains: string[];
  public_site_key: string;
  status: 'active' | 'disabled';
  config_version: number;
  verification_status: 'unverified' | 'verified' | 'rejected';
  verified_at: string;
  monetization_status: 'pending' | 'enabled' | 'disabled';
  default_disclosure_text: string;
  allowed_ad_sources: AdSourceType[];
  created_at: string;
  updated_at: string;
  verification_required?: boolean;
}

export interface Placement {
  id: string;
  site_id: string;
  placement_key: string;
  name: string;
  description?: string;
  format: AdFormat;
  width: number;
  height: number;
  fallback_behavior: 'hide' | 'blank' | 'custom';
  allowed_source_types: AdSourceType[];
  max_items: number;
  layout_config: Record<string, unknown>;
  disclosure_mode: 'required' | 'inherited' | 'disabled_only_if_not_applicable';
  status: 'active' | 'disabled';
  created_at: string;
  updated_at: string;
}

export interface Campaign {
  id: string;
  site_id: string;
  placement_id: string;
  name: string;
  destination_url: string;
  format: AdFormat;
  title?: string;
  body?: string;
  image_url?: string;
  alt_text?: string;
  cta_label?: string;
  status: 'draft' | 'scheduled' | 'active' | 'paused' | 'expired' | 'archived';
  source_type: AdSourceType;
  affiliate_offer_id: string;
  disclosure_override: string;
  starts_at?: string;
  ends_at?: string;
  priority: number;
  weight: number;
  cpm_cents?: number;
  daily_budget_cents?: number;
  frequency_cap_impressions?: number;
  frequency_cap_window_hours?: number;
  audience_segment_id?: string;
  created_at: string;
  updated_at: string;
  published_at?: string;
}

export interface AffiliateOffer {
  id: string;
  publisher_id: string;
  provider: 'shopee_affiliate' | 'manual_affiliate' | string;
  offer_name: string;
  product_title: string;
  product_image_url: string;
  product_description: string;
  merchant_name: string;
  affiliate_url: string;
  canonical_product_url: string;
  price_text: string;
  currency: string;
  availability_checked_at: string;
  disclosure_text: string;
  status: 'draft' | 'active' | 'paused' | 'expired' | 'rejected' | 'archived';
  validation_status: 'valid' | 'unverified' | 'rejected';
  validation_reason: string;
  last_link_validation_at: string;
  created_at: string;
  updated_at: string;
}

export interface Publisher {
  id: string;
  name: string;
  owner_user_id: string;
  owner_email: string;
  status: 'pending' | 'active' | 'suspended' | 'rejected';
  contact_email: string;
  timezone: string;
  created_at: string;
  updated_at: string;
}

export interface SiteVerification {
  site_id: string;
  domain: string;
  verification_status: 'unverified' | 'verified' | 'rejected';
  verified_at: string;
  monetization_status: 'pending' | 'enabled' | 'disabled';
  methods: string[];
  manual_verification_available?: boolean;
  method?: string;
  record_type?: string;
  record_name?: string;
  record_value?: string;
  instructions?: string[];
}

export interface AnalyticsSummary {
  impressions: number;
  clicks: number;
  outbound_redirects: number;
  affiliate_clicks: number;
  ctr: number;
  confirmed_conversions: number;
  confirmed_commission: number;
  provenance?: Record<string, string>;
}

export interface TimeseriesPoint {
  date: string;
  impressions: number;
  clicks: number;
  outbound_redirects?: number;
  ctr: number;
}

export interface AnalyticsBreakdownRow {
  key: string;
  name: string;
  impressions: number;
  clicks: number;
  outbound_redirects?: number;
  ctr: number;
}

export type ConversionStatus = 'pending' | 'approved' | 'rejected' | 'reconciled';

export interface ConversionSource {
  id: string;
  site_id: string;
  publisher_id: string;
  name: string;
  public_key: string;
  default_campaign_id: string;
  default_offer_id: string;
  status: 'active' | 'disabled';
  last_event_at: string;
  created_at: string;
  updated_at: string;
  signing_secret?: string;
  webhook_url?: string;
  pixel_url?: string;
}

export interface ConversionTouch {
  utm_source?: string;
  utm_medium?: string;
  utm_campaign?: string;
  utm_term?: string;
  utm_content?: string;
  landing_page?: string;
  referrer?: string;
  captured_at?: string;
  [key: string]: unknown;
}

export interface ConversionEvent {
  id: string;
  source_id: string;
  site_id: string;
  publisher_id: string;
  campaign_id: string;
  offer_id: string;
  external_id: string;
  event_name: string;
  order_id: string;
  value_cents: number;
  commission_cents: number;
  currency: string;
  status: ConversionStatus;
  ingestion: string;
  visitor_id: string;
  page_url: string;
  referrer: string;
  utm_source: string;
  utm_medium: string;
  utm_campaign: string;
  utm_term: string;
  utm_content: string;
  first_touch: ConversionTouch;
  last_touch: ConversionTouch;
  clicked_campaign_id: string;
  occurred_at: string;
  reconciled_at: string;
  notes: string;
  created_at: string;
  updated_at: string;
}

export interface ConversionSummary {
  conversions: number;
  pending_conversions: number;
  approved_conversions: number;
  rejected_conversions: number;
  reconciled_conversions: number;
  gross_value_cents: number;
  commission_cents: number;
  approved_value_cents: number;
  approved_commission_cents: number;
  impressions: number;
  clicks: number;
  ctr: number;
  conversion_rate: number;
  funnel: Array<{ stage: string; count: number }>;
  provenance: Record<string, string>;
}

export interface ConversionReconciliationRow {
  offer_id: string;
  campaign_id: string;
  status: ConversionStatus;
  conversions: number;
  value_cents: number;
  commission_cents: number;
  offer_title: string;
  campaign_name: string;
}

export type AutomationWorkflowStatus = 'draft' | 'active' | 'paused';
export type AutomationTriggerType = 'schedule' | 'performance' | 'conversion' | 'inquiry' | 'manual';
export type AutomationStepType = 'condition' | 'delay' | 'action';
export type AutomationRunStatus = 'queued' | 'running' | 'waiting' | 'completed' | 'failed' | 'skipped';

export interface AutomationStep {
  id: string;
  workflow_id: string;
  type: AutomationStepType;
  config: Record<string, any>;
  position: number;
  created_at: string;
}

export interface AutomationWorkflow {
  id: string;
  publisher_id: string;
  site_id: string;
  name: string;
  description: string;
  status: AutomationWorkflowStatus;
  trigger_type: AutomationTriggerType;
  trigger_config: Record<string, any>;
  last_triggered_at: string;
  next_run_at: string;
  created_by: string;
  created_at: string;
  updated_at: string;
  steps: AutomationStep[];
}

export interface AutomationRun {
  id: string;
  workflow_id: string;
  trigger_key: string;
  status: AutomationRunStatus;
  trigger_payload: Record<string, any>;
  current_step: number;
  resume_at: string;
  started_at: string;
  completed_at: string;
  error: string;
  log: string;
  created_at: string;
  updated_at: string;
}

export interface AppNotification {
  id: string;
  user_id: string;
  publisher_id: string;
  kind: 'info' | 'warning' | 'success' | 'error';
  title: string;
  body: string;
  link: string;
  read_at: string;
  created_at: string;
}

export type CrmStage = 'new' | 'contacted' | 'qualified' | 'proposal' | 'negotiation' | 'won' | 'lost';
export type CrmTaskStatus = 'open' | 'done' | 'cancelled';

export interface CrmLead {
  id: string;
  inquiry_id: string;
  name: string;
  email: string;
  company: string;
  website: string;
  phone: string;
  source: string;
  source_detail: string;
  stage: CrmStage;
  score: number;
  owner_user_id: string;
  owner_email: string;
  value_cents: number;
  currency: string;
  tags: string[];
  notes: string;
  utm_source: string;
  utm_medium: string;
  utm_campaign: string;
  utm_term: string;
  utm_content: string;
  referrer: string;
  last_contact_at: string;
  next_follow_up_at: string;
  converted_at: string;
  created_at: string;
  updated_at: string;
}

export interface CrmActivity {
  id: string;
  lead_id: string;
  type: 'note' | 'email' | 'call' | 'meeting' | 'stage_change' | 'system';
  subject: string;
  body: string;
  occurred_at: string;
  created_by: string;
  metadata: Record<string, unknown>;
  created_at: string;
}

export interface CrmTask {
  id: string;
  lead_id: string;
  title: string;
  due_at: string;
  status: CrmTaskStatus;
  assigned_to: string;
  notes: string;
  completed_at: string;
  created_at: string;
  updated_at: string;
}

export interface CrmPipelineStage {
  stage: CrmStage;
  leads: number;
  value_cents: number;
}

export interface CrmSummary {
  total_leads: number;
  open_leads: number;
  won_leads: number;
  pipeline_value_cents: number;
  won_value_cents: number;
  overdue_tasks: number;
}

export interface SeoProject {
  id: string;
  site_id: string;
  publisher_id: string;
  name: string;
  domain: string;
  country: string;
  language: string;
  status: 'active' | 'paused';
  created_at: string;
  updated_at: string;
}

export interface SeoKeyword {
  id: string;
  project_id: string;
  keyword: string;
  target_url: string;
  country: string;
  device: string;
  intent: string;
  priority: number;
  search_volume: number;
  difficulty: number;
  current_position: number;
  previous_position: number;
  last_checked_at: string;
  source: string;
  tags: string[];
  status: string;
  created_at: string;
  updated_at: string;
}

export interface SeoRanking {
  id: string;
  keyword_id: string;
  position: number;
  url: string;
  checked_at: string;
  source: string;
  note: string;
  created_at: string;
}

export interface SeoCompetitorKeyword {
  id: string;
  competitor_id: string;
  keyword: string;
  position: number;
  search_volume: number;
  url: string;
  checked_at: string;
  source: string;
  created_at: string;
}

export interface SeoCompetitor {
  id: string;
  project_id: string;
  domain: string;
  name: string;
  notes: string;
  created_at: string;
  updated_at: string;
  keywords: SeoCompetitorKeyword[];
}

export interface SeoBacklink {
  id: string;
  project_id: string;
  source_url: string;
  target_url: string;
  anchor_text: string;
  domain: string;
  status: 'active' | 'lost' | 'pending';
  nofollow: boolean;
  first_seen_at: string;
  last_checked_at: string;
  notes: string;
  created_at: string;
  updated_at: string;
}

export interface SeoAuditIssue {
  id: string;
  audit_id: string;
  severity: 'critical' | 'high' | 'medium' | 'low' | 'info';
  category: string;
  code: string;
  message: string;
  recommendation: string;
  page_url: string;
}

export interface SeoAudit {
  id: string;
  project_id: string;
  target_url: string;
  status: string;
  score: number;
  checked_at: string;
  summary: Record<string, any>;
  created_at: string;
  issues?: SeoAuditIssue[];
}

export interface GeoQuery {
  id: string;
  project_id: string;
  query_text: string;
  engines: string[];
  country: string;
  status: string;
  last_checked_at: string;
  created_at: string;
  updated_at: string;
  mentions?: GeoMention[];
  checks?: number;
  mention_rate_percent?: number;
}

export interface GeoMention {
  id: string;
  query_id: string;
  engine: string;
  mentioned: boolean;
  position: number;
  sentiment: string;
  snippet: string;
  checked_at: string;
  source: string;
  created_at: string;
}

export interface SeoSummary {
  keywords: number;
  tracked_keywords: number;
  top10_keywords: number;
  improved_keywords: number;
  declined_keywords: number;
  average_position: number;
  backlinks: number;
  active_backlinks: number;
  lost_backlinks: number;
  competitors: number;
  latest_audit_score: number;
  latest_audit_at: string;
  geo_checks: number;
  geo_mentions: number;
  geo_visibility_percent: number;
}

export interface AudienceSegment {
  id: string;
  site_id: string;
  publisher_id: string;
  name: string;
  description: string;
  rules: {
    utm_source?: string | string[];
    utm_medium?: string | string[];
    utm_campaign?: string | string[];
    utm_term?: string | string[];
    utm_content?: string | string[];
    device?: string | string[];
    referrer_contains?: string;
  };
  status: 'active' | 'paused';
  created_at: string;
  updated_at: string;
}

export interface ExperimentVariant {
  id: string;
  experiment_id: string;
  campaign_id: string;
  name: string;
  weight: number;
  is_control: boolean;
  created_at: string;
  campaign_name?: string;
  campaign_status?: string;
  impressions?: number;
  clicks?: number;
  conversions?: number;
  value_cents?: number;
  commission_cents?: number;
  ctr?: number;
  conversion_rate?: number;
  revenue_per_mille_cents?: number;
  score?: number;
}

export interface Experiment {
  id: string;
  site_id: string;
  placement_id: string;
  name: string;
  description: string;
  status: 'draft' | 'running' | 'paused' | 'completed';
  objective: 'ctr' | 'conversion_rate' | 'revenue';
  min_impressions: number;
  min_lift_percent: number;
  auto_winner: boolean;
  winner_campaign_id: string;
  started_at: string;
  ended_at: string;
  created_at: string;
  updated_at: string;
  variants: ExperimentVariant[];
  metrics?: {
    experiment: Experiment;
    variants: ExperimentVariant[];
    total_impressions: number;
    period_start: string;
    period_end: string;
  };
}

export interface AuditLogItem {
  id: string;
  actor: string;
  action: string;
  entity_type: string;
  entity_id: string;
  details: unknown;
  created_at: string;
}

export type PartnerInquiryKind = 'publisher' | 'client';
export type PartnerInquiryStatus = 'new' | 'reviewing' | 'closed';

export interface PartnerInquiry {
  id: string;
  kind: PartnerInquiryKind;
  name: string;
  email: string;
  company: string;
  website: string;
  message: string;
  status: PartnerInquiryStatus;
  created_at: string;
  updated_at: string;
}

export type SmtpEncryption = 'starttls' | 'ssl' | 'none';

export interface EmailSettings {
  enabled: boolean;
  host: string;
  port: number;
  encryption: SmtpEncryption | string;
  username: string;
  password_set: boolean;
  from_email: string;
  from_name: string;
  reply_to: string;
  updated_at: string;
  updated_by: string;
}

export interface EmailSettingsPayload {
  enabled: boolean;
  host: string;
  port: number;
  encryption: SmtpEncryption;
  username: string;
  from_email: string;
  from_name: string;
  reply_to: string;
  password?: string;
}

export interface AppSettings {
  app_name: string;
  environment: string;
  timezone: string;
  sdk_version: string;
  sdk_url: string;
  config_cache_seconds: number;
  max_stale_seconds: number;
  event_retention_days: number;
  click_token_ttl_seconds: number;
  delivery_rate_limit_per_minute: number;
  event_rate_limit_per_minute: number;
}

export interface SandboxItem {
  placement: Placement;
  campaign: Campaign | null;
  affiliate_offer: AffiliateOffer | null;
  disclosure_text: string;
  click_url: string | null;
}

export interface SandboxData {
  site: Site;
  allowed_domains: string[];
  placements: SandboxItem[];
}

export type BillingPlanStatus = 'active' | 'archived';
export type InvoiceStatus = 'open' | 'partially_paid' | 'paid' | 'overdue' | 'void';
export type InvoiceContext = 'subscription' | 'course' | 'manual';
export type PaymentMethod = 'manual' | 'bank_transfer' | 'cash' | 'other';

export interface BillingPlan {
  id: string;
  name: string;
  description: string;
  price_cents: number;
  currency: string;
  billing_interval: string;
  included_campaigns: number;
  included_impressions: number;
  overage_per_1000_impressions_cents: number;
  revenue_share_percent: number;
  features: string[];
  status: BillingPlanStatus;
  sort_order: number;
  created_at: string;
  updated_at: string;
}

export interface PublisherSubscription {
  id: string;
  publisher_id: string;
  plan_id: string;
  status: 'active' | 'past_due' | 'canceled';
  current_period_start: string;
  current_period_end: string;
  cancel_at_period_end: boolean;
  created_at: string;
  updated_at: string;
}

export interface PublisherUsage {
  period_start: string;
  period_end: string;
  impressions: number;
  campaigns: number;
  included_impressions: number;
  included_campaigns: number;
  overage_impressions: number;
  overage_cents: number;
  revenue_share_percent: number;
  hard_cap: boolean;
  impression_quota_exceeded: boolean;
  campaign_quota_exceeded: boolean;
}

export interface PublisherRevenueSummary {
  period_start: string;
  period_end: string;
  direct_impressions: number;
  gross_cents: number;
  share_percent: number;
  publisher_cents: number;
}

export interface BillingInvoiceItem {
  id: string;
  invoice_id: string;
  kind: 'subscription' | 'overage' | 'course' | 'adjustment';
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
  context: InvoiceContext;
  period_start: string;
  period_end: string;
  currency: string;
  subtotal_cents: number;
  tax_cents: number;
  total_cents: number;
  amount_paid_cents: number;
  balance_cents: number;
  status: InvoiceStatus;
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
  method: PaymentMethod;
  reference: string;
  notes: string;
  received_at: string;
  recorded_by: string;
  created_at: string;
}

export interface PublisherPayout {
  id: string;
  publisher_id: string;
  period_start: string;
  period_end: string;
  amount_cents: number;
  currency: string;
  method: string;
  reference: string;
  status: 'pending' | 'paid' | 'void';
  paid_at: string;
  recorded_by: string;
  notes: string;
  created_at: string;
}

export interface BillingSubscriptionPayload {
  subscription: PublisherSubscription;
  plan: BillingPlan | null;
  usage: PublisherUsage;
  revenue: PublisherRevenueSummary;
}
