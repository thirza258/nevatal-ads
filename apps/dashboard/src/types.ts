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
