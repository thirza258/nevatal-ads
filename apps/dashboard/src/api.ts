import {
  User,
  Site,
  SiteVerification,
  Placement,
  Campaign,
  AffiliateOffer,
  Publisher,
  AnalyticsSummary,
  AnalyticsBreakdownRow,
  TimeseriesPoint,
  AuditLogItem,
  PartnerInquiry,
  PartnerInquiryKind,
  PartnerInquiryStatus,
  AppSettings,
  EmailSettings,
  EmailSettingsPayload,
  SandboxData,
  ConversionEvent,
  ConversionReconciliationRow,
  ConversionSource,
  ConversionStatus,
  ConversionSummary,
  AutomationRun,
  AutomationWorkflow,
  AppNotification,
  CrmActivity,
  CrmLead,
  CrmPipelineStage,
  CrmStage,
  CrmSummary,
  CrmTask,
  CrmTaskStatus,
  SeoProject,
  SeoKeyword,
  SeoRanking,
  SeoCompetitor,
  SeoCompetitorKeyword,
  SeoBacklink,
  SeoAudit,
  SeoSummary,
  GeoQuery,
  GeoMention,
  AudienceSegment,
  Experiment,
  BillingPlan,
  PublisherSubscription,
  BillingSubscriptionPayload,
  PublisherUsage,
  PublisherRevenueSummary,
  BillingInvoice,
  BillingPayment,
  PublisherPayout,
  InvoiceContext,
  InvoiceStatus,
  PaymentMethod,
} from './types';

const TOKEN_KEY = 'adcontrol_token';

export class ApiError extends Error {
  status: number;

  constructor(message: string, status: number) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
  }
}

export function getStoredToken(): string | null {
  return localStorage.getItem(TOKEN_KEY);
}

export function setStoredToken(token: string) {
  localStorage.setItem(TOKEN_KEY, token);
}

export function clearStoredToken() {
  localStorage.removeItem(TOKEN_KEY);
}

function authHeaders(): Record<string, string> {
  const token = getStoredToken();
  return token ? { Authorization: `Bearer ${token}` } : {};
}

async function errorFromResponse(res: Response): Promise<ApiError> {
  let message = `Request failed (${res.status})`;
  try {
    const data = await res.json();
    if (typeof data?.error === 'string') message = data.error;
  } catch (_) {
    // Non-JSON error body; keep the status-based message.
  }
  return new ApiError(message, res.status);
}

async function request<T>(endpoint: string, options: RequestInit = {}): Promise<T> {
  const headers: Record<string, string> = {
    ...authHeaders(),
    ...((options.headers as Record<string, string>) || {}),
  };
  if (options.body && !headers['Content-Type']) {
    headers['Content-Type'] = 'application/json';
  }

  const res = await fetch(endpoint, { ...options, headers });

  if (res.status === 401) {
    clearStoredToken();
    window.dispatchEvent(new Event('auth:unauthorized'));
    throw new ApiError('Unauthorized or session expired', 401);
  }

  if (!res.ok) {
    throw await errorFromResponse(res);
  }

  if (res.status === 204) return undefined as T;
  const text = await res.text();
  if (!text) return undefined as T;
  try {
    return JSON.parse(text) as T;
  } catch (_) {
    throw new ApiError('Server returned an invalid response', res.status);
  }
}

function analyticsParams(filters?: {
  site_id?: string;
  placement_id?: string;
  campaign_id?: string;
  publisher_id?: string;
  start_date?: string;
  end_date?: string;
  days?: number;
}): URLSearchParams {
  const params = new URLSearchParams();
  if (filters?.site_id) params.set('site_id', filters.site_id);
  if (filters?.placement_id) params.set('placement_id', filters.placement_id);
  if (filters?.campaign_id) params.set('campaign_id', filters.campaign_id);
  if (filters?.publisher_id) params.set('publisher_id', filters.publisher_id);
  if (filters?.start_date) params.set('start_date', filters.start_date);
  if (filters?.end_date) params.set('end_date', filters.end_date);
  if (filters?.days) params.set('days', String(filters.days));
  return params;
}

export const api = {
  // Auth
  async login(email: string, password: string): Promise<{ token: string; user: User }> {
    const data = await request<{ token: string; user: User }>('/api/v1/auth/login', {
      method: 'POST',
      body: JSON.stringify({ email, password }),
    });
    setStoredToken(data.token);
    return data;
  },

  async logout(): Promise<void> {
    try {
      await request('/api/v1/auth/logout', { method: 'POST' });
    } finally {
      clearStoredToken();
    }
  },

  async getMe(): Promise<{ user: User }> {
    return request<{ user: User }>('/api/v1/auth/me');
  },

  async changePassword(currentPassword: string, newPassword: string): Promise<{ message: string }> {
    return request<{ message: string }>('/api/v1/auth/change-password', {
      method: 'POST',
      body: JSON.stringify({ current_password: currentPassword, new_password: newPassword }),
    });
  },

  // Health & settings
  async getHealth(): Promise<{ status: string; service: string }> {
    return request<{ status: string; service: string }>('/healthz');
  },

  async getSettings(): Promise<AppSettings> {
    return request<AppSettings>('/api/v1/settings');
  },

  // Email marketing / SMTP (administrator only)
  async getEmailSettings(): Promise<EmailSettings> {
    const res = await request<{ settings: EmailSettings }>('/api/v1/email/settings');
    return res.settings;
  },

  async updateEmailSettings(payload: EmailSettingsPayload): Promise<EmailSettings> {
    const res = await request<{ settings: EmailSettings }>('/api/v1/email/settings', {
      method: 'PUT',
      body: JSON.stringify(payload),
    });
    return res.settings;
  },

  async sendTestEmail(to: string): Promise<{ status: string; message: string }> {
    return request<{ status: string; message: string }>('/api/v1/email/test', {
      method: 'POST',
      body: JSON.stringify({ to }),
    });
  },

  // Sites
  async getSites(publisherId?: string): Promise<Site[]> {
    const q = publisherId ? `?publisher_id=${encodeURIComponent(publisherId)}` : '';
    const res = await request<{ sites: Site[] }>(`/api/v1/sites${q}`);
    return res.sites;
  },

  async getSite(id: string): Promise<Site> {
    return request<Site>(`/api/v1/sites/${encodeURIComponent(id)}`);
  },

  async createSite(site: Partial<Site>): Promise<Site> {
    return request<Site>('/api/v1/sites', {
      method: 'POST',
      body: JSON.stringify(site),
    });
  },

  async updateSite(id: string, site: Partial<Site>): Promise<Site> {
    return request<Site>(`/api/v1/sites/${id}`, {
      method: 'PATCH',
      body: JSON.stringify(site),
    });
  },

  async getSiteVerification(id: string): Promise<SiteVerification> {
    return request<SiteVerification>(`/api/v1/sites/${encodeURIComponent(id)}/verification`);
  },

  async verifySite(id: string, method: 'dns_txt' | 'manual' = 'dns_txt'): Promise<{ status: string; site: Site }> {
    return request<{ status: string; site: Site }>(`/api/v1/sites/${encodeURIComponent(id)}/verify`, {
      method: 'POST',
      body: JSON.stringify({ method }),
    });
  },

  async rotateSiteKey(id: string): Promise<Site> {
    return request<Site>(`/api/v1/sites/${id}/rotate-key`, {
      method: 'POST',
    });
  },

  async pauseAllSiteCampaigns(id: string): Promise<{ status: string }> {
    return request<{ status: string }>(`/api/v1/sites/${id}/pause-all`, {
      method: 'POST',
    });
  },

  async deleteSite(id: string): Promise<{ status: string; message?: string }> {
    return request<{ status: string; message?: string }>(`/api/v1/sites/${id}`, {
      method: 'DELETE',
    });
  },

  // Placements
  async getPlacements(siteId?: string): Promise<Placement[]> {
    const q = siteId ? `?site_id=${encodeURIComponent(siteId)}` : '';
    const res = await request<{ placements: Placement[] }>(`/api/v1/placements${q}`);
    return res.placements;
  },

  async createPlacement(placement: Partial<Placement>): Promise<Placement> {
    return request<Placement>('/api/v1/placements', {
      method: 'POST',
      body: JSON.stringify(placement),
    });
  },

  async updatePlacement(id: string, placement: Partial<Placement>): Promise<Placement> {
    return request<Placement>(`/api/v1/placements/${id}`, {
      method: 'PATCH',
      body: JSON.stringify(placement),
    });
  },

  async deletePlacement(id: string): Promise<{ status: string; message?: string }> {
    return request<{ status: string; message?: string }>(`/api/v1/placements/${id}`, {
      method: 'DELETE',
    });
  },

  // Campaigns
  async getCampaigns(filters?: { site_id?: string; placement_id?: string; status?: string }): Promise<Campaign[]> {
    const params = new URLSearchParams();
    if (filters?.site_id) params.set('site_id', filters.site_id);
    if (filters?.placement_id) params.set('placement_id', filters.placement_id);
    if (filters?.status && filters.status !== 'all') params.set('status', filters.status);
    const qs = params.toString() ? `?${params.toString()}` : '';
    const res = await request<{ campaigns: Campaign[] }>(`/api/v1/campaigns${qs}`);
    return res.campaigns;
  },

  async createCampaign(campaign: Partial<Campaign>): Promise<Campaign> {
    return request<Campaign>('/api/v1/campaigns', {
      method: 'POST',
      body: JSON.stringify(campaign),
    });
  },

  async updateCampaign(id: string, campaign: Partial<Campaign>): Promise<Campaign> {
    return request<Campaign>(`/api/v1/campaigns/${id}`, {
      method: 'PATCH',
      body: JSON.stringify(campaign),
    });
  },

  async publishCampaign(id: string): Promise<Campaign> {
    return request<Campaign>(`/api/v1/campaigns/${id}/publish`, {
      method: 'POST',
    });
  },

  async pauseCampaign(id: string): Promise<Campaign> {
    return request<Campaign>(`/api/v1/campaigns/${id}/pause`, {
      method: 'POST',
    });
  },

  async deleteCampaign(id: string): Promise<{ status: string; message?: string }> {
    return request<{ status: string; message?: string }>(`/api/v1/campaigns/${id}`, {
      method: 'DELETE',
    });
  },

  // Publishers
  async getPublishers(): Promise<Publisher[]> {
    const res = await request<{ publishers: Publisher[] }>('/api/v1/publishers');
    return res.publishers;
  },

  async getPublisher(id: string): Promise<{ publisher: Publisher; sites: Site[] }> {
    return request<{ publisher: Publisher; sites: Site[] }>(`/api/v1/publishers/${encodeURIComponent(id)}`);
  },

  async createPublisher(payload: {
    name: string;
    owner_email: string;
    password: string;
    contact_email?: string;
    timezone?: string;
    status?: string;
  }): Promise<Publisher> {
    return request<Publisher>('/api/v1/publishers', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
  },

  async updatePublisher(id: string, payload: Partial<Publisher>): Promise<Publisher> {
    return request<Publisher>(`/api/v1/publishers/${encodeURIComponent(id)}`, {
      method: 'PATCH',
      body: JSON.stringify(payload),
    });
  },

  // Affiliate offers (manual affiliate URL entry; no Shopee API dependency)
  async getAffiliateOffers(filters?: { publisher_id?: string; status?: string }): Promise<AffiliateOffer[]> {
    const params = new URLSearchParams();
    if (filters?.publisher_id) params.set('publisher_id', filters.publisher_id);
    if (filters?.status && filters.status !== 'all') params.set('status', filters.status);
    const qs = params.toString() ? `?${params.toString()}` : '';
    const res = await request<{ affiliate_offers: AffiliateOffer[] }>(`/api/v1/affiliate-offers${qs}`);
    return res.affiliate_offers;
  },

  async getAffiliateOffer(id: string): Promise<{ affiliate_offer: AffiliateOffer; campaigns: Campaign[] }> {
    return request<{ affiliate_offer: AffiliateOffer; campaigns: Campaign[] }>(
      `/api/v1/affiliate-offers/${encodeURIComponent(id)}`,
    );
  },

  async createAffiliateOffer(offer: Partial<AffiliateOffer> & { publisher_id?: string }): Promise<AffiliateOffer> {
    return request<AffiliateOffer>('/api/v1/affiliate-offers', {
      method: 'POST',
      body: JSON.stringify(offer),
    });
  },

  async updateAffiliateOffer(id: string, offer: Partial<AffiliateOffer>): Promise<AffiliateOffer> {
    return request<AffiliateOffer>(`/api/v1/affiliate-offers/${encodeURIComponent(id)}`, {
      method: 'PATCH',
      body: JSON.stringify(offer),
    });
  },

  async validateAffiliateOffer(id: string): Promise<AffiliateOffer> {
    return request<AffiliateOffer>(`/api/v1/affiliate-offers/${encodeURIComponent(id)}/validate`, {
      method: 'POST',
    });
  },

  async publishAffiliateOffer(id: string): Promise<AffiliateOffer> {
    return request<AffiliateOffer>(`/api/v1/affiliate-offers/${encodeURIComponent(id)}/publish`, {
      method: 'POST',
    });
  },

  async pauseAffiliateOffer(id: string): Promise<AffiliateOffer> {
    return request<AffiliateOffer>(`/api/v1/affiliate-offers/${encodeURIComponent(id)}/pause`, {
      method: 'POST',
    });
  },

  async placeAffiliateOffer(
    id: string,
    payload: { placement_id: string; name?: string; status?: string; starts_at?: string; ends_at?: string; priority?: number; weight?: number },
  ): Promise<Campaign> {
    return request<Campaign>(`/api/v1/affiliate-offers/${encodeURIComponent(id)}/placements`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
  },

  async deleteAffiliateOffer(id: string): Promise<{ status: string; message?: string }> {
    return request<{ status: string; message?: string }>(`/api/v1/affiliate-offers/${encodeURIComponent(id)}`, {
      method: 'DELETE',
    });
  },

  // Sandbox (authenticated preview; does not bypass public delivery security)
  async getSandbox(siteId: string): Promise<SandboxData> {
    return request<SandboxData>(`/api/v1/sandbox/sites/${encodeURIComponent(siteId)}`);
  },

  // Analytics
  async getAnalyticsSummary(filters?: {
    site_id?: string;
    placement_id?: string;
    campaign_id?: string;
    publisher_id?: string;
    start_date?: string;
    end_date?: string;
  }): Promise<AnalyticsSummary> {
    const qs = analyticsParams(filters).toString();
    return request<AnalyticsSummary>(`/api/v1/analytics/summary${qs ? `?${qs}` : ''}`);
  },

  async getAnalyticsTimeseries(filters?: {
    site_id?: string;
    placement_id?: string;
    campaign_id?: string;
    publisher_id?: string;
    start_date?: string;
    end_date?: string;
    days?: number;
  }): Promise<TimeseriesPoint[]> {
    const qs = analyticsParams({ ...filters, days: filters?.days || 7 }).toString();
    return request<TimeseriesPoint[]>(`/api/v1/analytics/timeseries?${qs}`);
  },

  async getAnalyticsBreakdown(filters?: {
    dimension?: 'site' | 'placement' | 'campaign' | 'source';
    site_id?: string;
    placement_id?: string;
    campaign_id?: string;
    publisher_id?: string;
    start_date?: string;
    end_date?: string;
  }): Promise<AnalyticsBreakdownRow[]> {
    const params = analyticsParams(filters);
    params.set('dimension', filters?.dimension || 'campaign');
    const res = await request<{ rows: AnalyticsBreakdownRow[] }>(
      `/api/v1/analytics/breakdown?${params.toString()}`,
    );
    return res.rows;
  },

  async downloadAnalyticsCsv(filters?: {
    site_id?: string;
    placement_id?: string;
    campaign_id?: string;
    publisher_id?: string;
    start_date?: string;
    end_date?: string;
  }): Promise<void> {
    const params = analyticsParams(filters);
    const res = await fetch(`/api/v1/analytics/export.csv?${params.toString()}`, {
      headers: authHeaders(),
    });
    if (res.status === 401) {
      clearStoredToken();
      window.dispatchEvent(new Event('auth:unauthorized'));
      throw new ApiError('Unauthorized or session expired', 401);
    }
    if (!res.ok) throw await errorFromResponse(res);

    const blob = await res.blob();
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = `adcontrol-events-${new Date().toISOString().slice(0, 10)}.csv`;
    document.body.appendChild(link);
    link.click();
    link.remove();
    window.setTimeout(() => URL.revokeObjectURL(url), 1000);
  },

  // Partner inquiries
  async getPartnerInquiries(filters?: {
    kind?: PartnerInquiryKind;
    status?: PartnerInquiryStatus;
    limit?: number;
    offset?: number;
  }): Promise<PartnerInquiry[]> {
    const params = new URLSearchParams();
    if (filters?.kind) params.set('kind', filters.kind);
    if (filters?.status) params.set('status', filters.status);
    params.set('limit', String(filters?.limit ?? 200));
    params.set('offset', String(filters?.offset ?? 0));
    const res = await request<{ inquiries: PartnerInquiry[] }>(
      `/api/v1/partner-inquiries?${params.toString()}`,
    );
    return res.inquiries;
  },

  async updatePartnerInquiry(id: string, status: PartnerInquiryStatus): Promise<PartnerInquiry> {
    const res = await request<{ inquiry: PartnerInquiry }>(
      `/api/v1/partner-inquiries/${encodeURIComponent(id)}`,
      { method: 'PATCH', body: JSON.stringify({ status }) },
    );
    return res.inquiry;
  },

  // Audit Logs
  async getAuditLogs(limit = 50, offset = 0): Promise<AuditLogItem[]> {
    const res = await request<{ audit_logs: AuditLogItem[] }>(
      `/api/v1/audit-log?limit=${limit}&offset=${offset}`,
    );
    return res.audit_logs;
  },

  // Conversion tracking and attribution
  async getConversionSources(filters?: { site_id?: string; status?: string }): Promise<ConversionSource[]> {
    const params = new URLSearchParams();
    if (filters?.site_id) params.set('site_id', filters.site_id);
    if (filters?.status) params.set('status', filters.status);
    const q = params.toString();
    const res = await request<{ sources: ConversionSource[] }>(
      `/api/v1/conversion/sources${q ? `?${q}` : ''}`,
    );
    return res.sources;
  },

  async createConversionSource(payload: {
    site_id: string;
    name: string;
    default_campaign_id?: string;
    default_offer_id?: string;
  }): Promise<{ source: ConversionSource; notice: string }> {
    return request(`/api/v1/conversion/sources`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
  },

  async updateConversionSource(
    id: string,
    payload: Partial<Pick<ConversionSource, 'name' | 'status' | 'default_campaign_id' | 'default_offer_id'>>,
  ): Promise<ConversionSource> {
    const res = await request<{ source: ConversionSource }>(
      `/api/v1/conversion/sources/${encodeURIComponent(id)}`,
      { method: 'PATCH', body: JSON.stringify(payload) },
    );
    return res.source;
  },

  async getConversions(filters?: {
    site_id?: string;
    campaign_id?: string;
    status?: ConversionStatus;
    start_date?: string;
    end_date?: string;
    limit?: number;
    offset?: number;
  }): Promise<ConversionEvent[]> {
    const params = new URLSearchParams();
    if (filters?.site_id) params.set('site_id', filters.site_id);
    if (filters?.campaign_id) params.set('campaign_id', filters.campaign_id);
    if (filters?.status) params.set('status', filters.status);
    if (filters?.start_date) params.set('start_date', filters.start_date);
    if (filters?.end_date) params.set('end_date', filters.end_date);
    params.set('limit', String(filters?.limit ?? 200));
    params.set('offset', String(filters?.offset ?? 0));
    const res = await request<{ conversions: ConversionEvent[] }>(
      `/api/v1/conversions?${params.toString()}`,
    );
    return res.conversions;
  },

  async getConversionSummary(filters?: {
    site_id?: string;
    campaign_id?: string;
    start_date?: string;
    end_date?: string;
  }): Promise<ConversionSummary> {
    const params = new URLSearchParams();
    if (filters?.site_id) params.set('site_id', filters.site_id);
    if (filters?.campaign_id) params.set('campaign_id', filters.campaign_id);
    if (filters?.start_date) params.set('start_date', filters.start_date);
    if (filters?.end_date) params.set('end_date', filters.end_date);
    return request<ConversionSummary>(`/api/v1/conversions/summary?${params.toString()}`);
  },

  async getConversionReconciliation(filters?: {
    site_id?: string;
    start_date?: string;
    end_date?: string;
  }): Promise<ConversionReconciliationRow[]> {
    const params = new URLSearchParams();
    if (filters?.site_id) params.set('site_id', filters.site_id);
    if (filters?.start_date) params.set('start_date', filters.start_date);
    if (filters?.end_date) params.set('end_date', filters.end_date);
    const res = await request<{ rows: ConversionReconciliationRow[] }>(
      `/api/v1/conversions/reconciliation?${params.toString()}`,
    );
    return res.rows;
  },

  async updateConversion(
    id: string,
    payload: { status: ConversionStatus; notes?: string },
  ): Promise<ConversionEvent> {
    const res = await request<{ conversion: ConversionEvent }>(
      `/api/v1/conversions/${encodeURIComponent(id)}`,
      { method: 'PATCH', body: JSON.stringify(payload) },
    );
    return res.conversion;
  },

  // Marketing automation
  async getAutomationWorkflows(filters?: { site_id?: string; status?: string }): Promise<AutomationWorkflow[]> {
    const params = new URLSearchParams();
    if (filters?.site_id) params.set('site_id', filters.site_id);
    if (filters?.status) params.set('status', filters.status);
    const q = params.toString();
    const res = await request<{ workflows: AutomationWorkflow[] }>(
      `/api/v1/automation/workflows${q ? `?${q}` : ''}`,
    );
    return res.workflows;
  },

  async createAutomationWorkflow(payload: {
    name: string;
    description?: string;
    site_id?: string;
    status: string;
    trigger_type: string;
    trigger_config: Record<string, any>;
    steps: Array<{ type: string; config: Record<string, any> }>;
  }): Promise<AutomationWorkflow> {
    const res = await request<{ workflow: AutomationWorkflow }>('/api/v1/automation/workflows', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    return res.workflow;
  },

  async updateAutomationWorkflow(
    id: string,
    payload: {
      name?: string;
      description?: string;
      site_id?: string;
      status?: string;
      trigger_type?: string;
      trigger_config?: Record<string, any>;
      steps?: Array<{ type: string; config: Record<string, any> }>;
    },
  ): Promise<AutomationWorkflow> {
    const res = await request<{ workflow: AutomationWorkflow }>(
      `/api/v1/automation/workflows/${encodeURIComponent(id)}`,
      { method: 'PATCH', body: JSON.stringify(payload) },
    );
    return res.workflow;
  },

  async setAutomationWorkflowStatus(id: string, status: string): Promise<AutomationWorkflow> {
    const res = await request<{ workflow: AutomationWorkflow }>(
      `/api/v1/automation/workflows/${encodeURIComponent(id)}/status`,
      { method: 'POST', body: JSON.stringify({ status }) },
    );
    return res.workflow;
  },

  async deleteAutomationWorkflow(id: string): Promise<{ status: string }> {
    return request(`/api/v1/automation/workflows/${encodeURIComponent(id)}`, { method: 'DELETE' });
  },

  async runAutomationWorkflow(id: string, payload?: Record<string, any>): Promise<{ status: string; run_id: string }> {
    return request(`/api/v1/automation/workflows/${encodeURIComponent(id)}/run`, {
      method: 'POST',
      body: JSON.stringify(payload || {}),
    });
  },

  async getAutomationRuns(filters?: { workflow_id?: string; limit?: number }): Promise<AutomationRun[]> {
    const params = new URLSearchParams();
    if (filters?.workflow_id) params.set('workflow_id', filters.workflow_id);
    params.set('limit', String(filters?.limit ?? 100));
    const res = await request<{ runs: AutomationRun[] }>(`/api/v1/automation/runs?${params.toString()}`);
    return res.runs;
  },

  async getAutomationMetrics(filters: {
    site_id?: string;
    campaign_id?: string;
    lookback_hours?: number;
  }): Promise<Record<string, any>> {
    const params = new URLSearchParams();
    if (filters.site_id) params.set('site_id', filters.site_id);
    if (filters.campaign_id) params.set('campaign_id', filters.campaign_id);
    params.set('lookback_hours', String(filters.lookback_hours ?? 24));
    return request(`/api/v1/automation/metrics?${params.toString()}`);
  },

  async getAutomationNotifications(): Promise<{ notifications: AppNotification[]; unread: number }> {
    return request('/api/v1/automation/notifications');
  },

  async markAutomationNotificationRead(id: string): Promise<{ status: string }> {
    return request(`/api/v1/automation/notifications/${encodeURIComponent(id)}/read`, {
      method: 'POST',
    });
  },

  // CRM and lead management
  async getCrmLeads(filters?: { stage?: CrmStage; q?: string; limit?: number }): Promise<{
    leads: CrmLead[];
    pipeline: CrmPipelineStage[];
    summary: CrmSummary;
  }> {
    const params = new URLSearchParams();
    if (filters?.stage) params.set('stage', filters.stage);
    if (filters?.q) params.set('q', filters.q);
    params.set('limit', String(filters?.limit ?? 300));
    const data = await request<{
      leads: CrmLead[];
      pipeline: CrmPipelineStage[] | { stages?: CrmPipelineStage[] };
      summary: CrmSummary;
    }>(`/api/v1/crm/leads?${params.toString()}`);
    const pipeline = Array.isArray(data.pipeline) ? data.pipeline : data.pipeline?.stages || [];
    return { leads: data.leads, pipeline, summary: data.summary };
  },

  async createCrmLead(payload: {
    name: string;
    email: string;
    company?: string;
    website?: string;
    phone?: string;
    source?: string;
    source_detail?: string;
    stage?: CrmStage;
    value_cents?: number;
    currency?: string;
    tags?: string[];
    notes?: string;
    owner_user_id?: string;
    next_follow_up_at?: string;
  }): Promise<CrmLead> {
    const res = await request<{ lead: CrmLead }>('/api/v1/crm/leads', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    return res.lead;
  },

  async getCrmLead(id: string): Promise<{ lead: CrmLead; activities: CrmActivity[]; tasks: CrmTask[] }> {
    return request(`/api/v1/crm/leads/${encodeURIComponent(id)}`);
  },

  async updateCrmLead(
    id: string,
    payload: Partial<Pick<CrmLead,
      'name' | 'email' | 'company' | 'website' | 'phone' | 'source' | 'source_detail' |
      'stage' | 'owner_user_id' | 'value_cents' | 'currency' | 'tags' | 'notes' |
      'next_follow_up_at'>>,
  ): Promise<CrmLead> {
    const res = await request<{ lead: CrmLead }>(`/api/v1/crm/leads/${encodeURIComponent(id)}`, {
      method: 'PATCH',
      body: JSON.stringify(payload),
    });
    return res.lead;
  },

  async deleteCrmLead(id: string): Promise<{ status: string }> {
    return request(`/api/v1/crm/leads/${encodeURIComponent(id)}`, { method: 'DELETE' });
  },

  async addCrmActivity(
    leadId: string,
    payload: { type: 'note' | 'call' | 'meeting'; subject?: string; body: string; occurred_at?: string },
  ): Promise<CrmActivity> {
    const res = await request<{ activity: CrmActivity }>(
      `/api/v1/crm/leads/${encodeURIComponent(leadId)}/activities`,
      { method: 'POST', body: JSON.stringify(payload) },
    );
    return res.activity;
  },

  async sendCrmEmail(
    leadId: string,
    payload: { subject: string; body: string },
  ): Promise<{ activity: CrmActivity; lead: CrmLead }> {
    return request(`/api/v1/crm/leads/${encodeURIComponent(leadId)}/email`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
  },

  async createCrmTask(
    leadId: string,
    payload: { title: string; due_at: string; assigned_to?: string; notes?: string },
  ): Promise<{ task: CrmTask; lead: CrmLead }> {
    return request(`/api/v1/crm/leads/${encodeURIComponent(leadId)}/tasks`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
  },

  async getCrmTasks(filters?: { lead_id?: string; status?: CrmTaskStatus; limit?: number }): Promise<CrmTask[]> {
    const params = new URLSearchParams();
    if (filters?.lead_id) params.set('lead_id', filters.lead_id);
    if (filters?.status) params.set('status', filters.status);
    params.set('limit', String(filters?.limit ?? 200));
    const res = await request<{ tasks: CrmTask[] }>(`/api/v1/crm/tasks?${params.toString()}`);
    return res.tasks;
  },

  async updateCrmTask(
    id: string,
    payload: Partial<Pick<CrmTask, 'title' | 'due_at' | 'status' | 'assigned_to' | 'notes'>>,
  ): Promise<CrmTask> {
    const res = await request<{ task: CrmTask }>(`/api/v1/crm/tasks/${encodeURIComponent(id)}`, {
      method: 'PATCH',
      body: JSON.stringify(payload),
    });
    return res.task;
  },

  async getCrmOwners(): Promise<Array<{ id: string; email: string; role: string }>> {
    const res = await request<{ owners: Array<{ id: string; email: string; role: string }> }>('/api/v1/crm/owners');
    return res.owners;
  },

  // Audience segments and experiments
  async getAudienceSegments(filters?: { site_id?: string }): Promise<AudienceSegment[]> {
    const params = new URLSearchParams();
    if (filters?.site_id) params.set('site_id', filters.site_id);
    const q = params.toString();
    const res = await request<{ segments: AudienceSegment[] }>(
      `/api/v1/audience-segments${q ? `?${q}` : ''}`,
    );
    return res.segments;
  },

  async createAudienceSegment(payload: {
    site_id: string;
    name: string;
    description?: string;
    rules: AudienceSegment['rules'];
    status?: string;
  }): Promise<AudienceSegment> {
    const res = await request<{ segment: AudienceSegment }>('/api/v1/audience-segments', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    return res.segment;
  },

  async updateAudienceSegment(id: string, payload: Partial<AudienceSegment>): Promise<AudienceSegment> {
    const res = await request<{ segment: AudienceSegment }>(
      `/api/v1/audience-segments/${encodeURIComponent(id)}`,
      { method: 'PATCH', body: JSON.stringify(payload) },
    );
    return res.segment;
  },

  async deleteAudienceSegment(id: string): Promise<{ status: string }> {
    return request(`/api/v1/audience-segments/${encodeURIComponent(id)}`, { method: 'DELETE' });
  },

  async getExperiments(filters?: { site_id?: string; placement_id?: string; status?: string }): Promise<Experiment[]> {
    const params = new URLSearchParams();
    if (filters?.site_id) params.set('site_id', filters.site_id);
    if (filters?.placement_id) params.set('placement_id', filters.placement_id);
    if (filters?.status) params.set('status', filters.status);
    const q = params.toString();
    const res = await request<{ experiments: Experiment[] }>(
      `/api/v1/experiments${q ? `?${q}` : ''}`,
    );
    return res.experiments;
  },

  async createExperiment(payload: {
    site_id: string;
    placement_id: string;
    name: string;
    description?: string;
    status?: string;
    objective?: string;
    min_impressions?: number;
    min_lift_percent?: number;
    auto_winner?: boolean;
    variants: Array<{ campaign_id: string; name?: string; weight?: number; is_control?: boolean }>;
  }): Promise<Experiment> {
    const res = await request<{ experiment: Experiment }>('/api/v1/experiments', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    return res.experiment;
  },

  async updateExperiment(id: string, payload: Partial<{
    name: string;
    description: string;
    status: string;
    objective: string;
    min_impressions: number;
    min_lift_percent: number;
    auto_winner: boolean;
    variants: Array<{ campaign_id: string; name?: string; weight?: number; is_control?: boolean }>;
  }>): Promise<Experiment> {
    const res = await request<{ experiment: Experiment }>(`/api/v1/experiments/${encodeURIComponent(id)}`, {
      method: 'PATCH',
      body: JSON.stringify(payload),
    });
    return res.experiment;
  },

  async setExperimentStatus(id: string, status: string): Promise<Experiment> {
    const res = await request<{ experiment: Experiment }>(
      `/api/v1/experiments/${encodeURIComponent(id)}/status`,
      { method: 'POST', body: JSON.stringify({ status }) },
    );
    return res.experiment;
  },

  async deleteExperiment(id: string): Promise<{ status: string }> {
    return request(`/api/v1/experiments/${encodeURIComponent(id)}`, { method: 'DELETE' });
  },

  async optimizeExperiment(id: string): Promise<{ updated_experiments: number; metrics: Experiment['metrics'] }> {
    return request(`/api/v1/experiments/${encodeURIComponent(id)}/optimize`, { method: 'POST' });
  },

  // SEO and GEO intelligence
  async getSeoProjects(): Promise<SeoProject[]> {
    const res = await request<{ projects: SeoProject[] }>('/api/v1/seo/projects');
    return res.projects;
  },

  async createSeoProject(payload: {
    site_id: string;
    name: string;
    domain?: string;
    country?: string;
    language?: string;
  }): Promise<SeoProject> {
    const res = await request<{ project: SeoProject }>('/api/v1/seo/projects', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    return res.project;
  },

  async updateSeoProject(id: string, payload: Partial<SeoProject>): Promise<SeoProject> {
    const res = await request<{ project: SeoProject }>(`/api/v1/seo/projects/${encodeURIComponent(id)}`, {
      method: 'PATCH',
      body: JSON.stringify(payload),
    });
    return res.project;
  },

  async deleteSeoProject(id: string): Promise<{ status: string }> {
    return request(`/api/v1/seo/projects/${encodeURIComponent(id)}`, { method: 'DELETE' });
  },

  async getSeoSummary(projectId: string): Promise<{ project: SeoProject; summary: SeoSummary }> {
    return request(`/api/v1/seo/projects/${encodeURIComponent(projectId)}/summary`);
  },

  async getSeoKeywords(projectId: string): Promise<SeoKeyword[]> {
    const res = await request<{ keywords: SeoKeyword[] }>(
      `/api/v1/seo/projects/${encodeURIComponent(projectId)}/keywords`,
    );
    return res.keywords;
  },

  async createSeoKeyword(projectId: string, payload: {
    keyword: string;
    target_url?: string;
    intent?: string;
    priority?: number;
    search_volume?: number;
    difficulty?: number;
    tags?: string[];
  }): Promise<SeoKeyword> {
    const res = await request<{ keyword: SeoKeyword }>(
      `/api/v1/seo/projects/${encodeURIComponent(projectId)}/keywords`,
      { method: 'POST', body: JSON.stringify(payload) },
    );
    return res.keyword;
  },

  async createSeoKeywordsBulk(
    projectId: string,
    keywords: Array<{ keyword: string; intent?: string }>,
  ): Promise<SeoKeyword[]> {
    const res = await request<{ keywords: SeoKeyword[] }>(
      `/api/v1/seo/projects/${encodeURIComponent(projectId)}/keywords/bulk`,
      { method: 'POST', body: JSON.stringify({ keywords }) },
    );
    return res.keywords;
  },

  async researchSeoKeywords(
    projectId: string,
    seed: string,
    count = 20,
  ): Promise<Array<{ keyword: string; intent: string; search_volume: number; difficulty: number }>> {
    const res = await request<{
      suggestions: Array<{ keyword: string; intent: string; search_volume: number; difficulty: number }>;
    }>(`/api/v1/seo/projects/${encodeURIComponent(projectId)}/research`, {
      method: 'POST',
      body: JSON.stringify({ seed, count }),
    });
    return res.suggestions;
  },

  async updateSeoKeyword(id: string, payload: Partial<SeoKeyword>): Promise<SeoKeyword> {
    const res = await request<{ keyword: SeoKeyword }>(`/api/v1/seo/keywords/${encodeURIComponent(id)}`, {
      method: 'PATCH',
      body: JSON.stringify(payload),
    });
    return res.keyword;
  },

  async deleteSeoKeyword(id: string): Promise<{ status: string }> {
    return request(`/api/v1/seo/keywords/${encodeURIComponent(id)}`, { method: 'DELETE' });
  },

  async getSeoRankings(keywordId: string): Promise<{ keyword: SeoKeyword; rankings: SeoRanking[] }> {
    return request(`/api/v1/seo/keywords/${encodeURIComponent(keywordId)}/rankings`);
  },

  async recordSeoRanking(
    keywordId: string,
    payload: { position: number; url?: string; checked_at?: string; note?: string },
  ): Promise<{ keyword: SeoKeyword; ranking: SeoRanking }> {
    return request(`/api/v1/seo/keywords/${encodeURIComponent(keywordId)}/rankings`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
  },

  async getSeoCompetitors(projectId: string): Promise<SeoCompetitor[]> {
    const res = await request<{ competitors: SeoCompetitor[] }>(
      `/api/v1/seo/projects/${encodeURIComponent(projectId)}/competitors`,
    );
    return res.competitors;
  },

  async createSeoCompetitor(
    projectId: string,
    payload: { domain: string; name?: string; notes?: string },
  ): Promise<SeoCompetitor> {
    const res = await request<{ competitor: SeoCompetitor }>(
      `/api/v1/seo/projects/${encodeURIComponent(projectId)}/competitors`,
      { method: 'POST', body: JSON.stringify(payload) },
    );
    return res.competitor;
  },

  async deleteSeoCompetitor(id: string): Promise<{ status: string }> {
    return request(`/api/v1/seo/competitors/${encodeURIComponent(id)}`, { method: 'DELETE' });
  },

  async addSeoCompetitorKeyword(
    competitorId: string,
    payload: { keyword: string; position?: number; search_volume?: number; url?: string },
  ): Promise<SeoCompetitorKeyword> {
    const res = await request<{ keyword: SeoCompetitorKeyword }>(
      `/api/v1/seo/competitors/${encodeURIComponent(competitorId)}/keywords`,
      { method: 'POST', body: JSON.stringify(payload) },
    );
    return res.keyword;
  },

  async deleteSeoCompetitorKeyword(competitorId: string, keywordId: string): Promise<{ status: string }> {
    return request(
      `/api/v1/seo/competitors/${encodeURIComponent(competitorId)}/keywords/${encodeURIComponent(keywordId)}`,
      { method: 'DELETE' },
    );
  },

  async getSeoBacklinks(projectId: string): Promise<SeoBacklink[]> {
    const res = await request<{ backlinks: SeoBacklink[] }>(
      `/api/v1/seo/projects/${encodeURIComponent(projectId)}/backlinks`,
    );
    return res.backlinks;
  },

  async createSeoBacklink(projectId: string, payload: {
    source_url: string;
    target_url?: string;
    anchor_text?: string;
    status?: string;
    nofollow?: boolean;
    notes?: string;
  }): Promise<SeoBacklink> {
    const res = await request<{ backlink: SeoBacklink }>(
      `/api/v1/seo/projects/${encodeURIComponent(projectId)}/backlinks`,
      { method: 'POST', body: JSON.stringify(payload) },
    );
    return res.backlink;
  },

  async updateSeoBacklink(id: string, payload: Partial<SeoBacklink>): Promise<SeoBacklink> {
    const res = await request<{ backlink: SeoBacklink }>(`/api/v1/seo/backlinks/${encodeURIComponent(id)}`, {
      method: 'PATCH',
      body: JSON.stringify(payload),
    });
    return res.backlink;
  },

  async deleteSeoBacklink(id: string): Promise<{ status: string }> {
    return request(`/api/v1/seo/backlinks/${encodeURIComponent(id)}`, { method: 'DELETE' });
  },

  async runSeoAudit(projectId: string, targetUrl?: string): Promise<SeoAudit> {
    const res = await request<{ audit: SeoAudit }>(
      `/api/v1/seo/projects/${encodeURIComponent(projectId)}/audit`,
      { method: 'POST', body: JSON.stringify({ target_url: targetUrl || '' }) },
    );
    return res.audit;
  },

  async getSeoAudits(projectId: string): Promise<SeoAudit[]> {
    const res = await request<{ audits: SeoAudit[] }>(
      `/api/v1/seo/projects/${encodeURIComponent(projectId)}/audits`,
    );
    return res.audits;
  },

  async getSeoAudit(id: string): Promise<SeoAudit> {
    const res = await request<{ audit: SeoAudit }>(`/api/v1/seo/audits/${encodeURIComponent(id)}`);
    return res.audit;
  },

  async optimizeSeoContent(
    projectId: string,
    payload: { keyword: string; target_url?: string },
  ): Promise<Record<string, any>> {
    return request(`/api/v1/seo/projects/${encodeURIComponent(projectId)}/optimize`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
  },

  async getGeoQueries(projectId: string): Promise<GeoQuery[]> {
    const res = await request<{ queries: GeoQuery[] }>(
      `/api/v1/seo/projects/${encodeURIComponent(projectId)}/geo`,
    );
    return res.queries;
  },

  async createGeoQuery(
    projectId: string,
    payload: { query_text: string; engines: string[]; country?: string },
  ): Promise<GeoQuery> {
    const res = await request<{ query: GeoQuery }>(
      `/api/v1/seo/projects/${encodeURIComponent(projectId)}/geo`,
      { method: 'POST', body: JSON.stringify(payload) },
    );
    return res.query;
  },

  async deleteGeoQuery(id: string): Promise<{ status: string }> {
    return request(`/api/v1/seo/geo-queries/${encodeURIComponent(id)}`, { method: 'DELETE' });
  },

  async addGeoMention(
    queryId: string,
    payload: {
      engine: string;
      mentioned: boolean;
      position?: number;
      sentiment?: string;
      snippet?: string;
      checked_at?: string;
    },
  ): Promise<GeoMention> {
    const res = await request<{ mention: GeoMention }>(
      `/api/v1/seo/geo-queries/${encodeURIComponent(queryId)}/mentions`,
      { method: 'POST', body: JSON.stringify(payload) },
    );
    return res.mention;
  },

  // Billing and monetization
  async getBillingPlans(includeArchived = false): Promise<BillingPlan[]> {
    const q = includeArchived ? '?include_archived=1' : '';
    const res = await request<{ plans: BillingPlan[] }>(`/api/v1/billing/plans${q}`);
    return res.plans;
  },

  async updateBillingPlan(id: string, payload: Partial<BillingPlan>): Promise<BillingPlan> {
    const res = await request<{ plan: BillingPlan }>(
      `/api/v1/billing/plans/${encodeURIComponent(id)}`,
      { method: 'PATCH', body: JSON.stringify(payload) },
    );
    return res.plan;
  },

  async getBillingSubscription(publisherId?: string): Promise<BillingSubscriptionPayload> {
    const q = publisherId ? `?publisher_id=${encodeURIComponent(publisherId)}` : '';
    return request<BillingSubscriptionPayload>(`/api/v1/billing/subscription${q}`);
  },

  async getBillingSubscriptions(): Promise<Array<PublisherSubscription & { plan: BillingPlan | null }>> {
    const res = await request<{ subscriptions: Array<PublisherSubscription & { plan: BillingPlan | null }> }>(
      '/api/v1/billing/subscriptions',
    );
    return res.subscriptions;
  },

  async updateBillingSubscription(
    publisherId: string,
    planId: string,
  ): Promise<{ subscription: PublisherSubscription | null; plan: BillingPlan | null }> {
    return request(`/api/v1/billing/subscriptions/${encodeURIComponent(publisherId)}`, {
      method: 'PUT',
      body: JSON.stringify({ plan_id: planId }),
    });
  },

  async changeBillingPlan(
    planId: string,
    publisherId?: string,
  ): Promise<{ subscription: PublisherSubscription | null; plan: BillingPlan | null; invoice: BillingInvoice | null }> {
    return request('/api/v1/billing/subscription/change', {
      method: 'POST',
      body: JSON.stringify({ plan_id: planId, publisher_id: publisherId }),
    });
  },

  async getBillingUsage(
    publisherId?: string,
    period?: { period_start: string; period_end: string },
  ): Promise<PublisherUsage> {
    const params = new URLSearchParams();
    if (publisherId) params.set('publisher_id', publisherId);
    if (period) {
      params.set('period_start', period.period_start);
      params.set('period_end', period.period_end);
    }
    const q = params.toString();
    return request<PublisherUsage>(`/api/v1/billing/usage${q ? `?${q}` : ''}`);
  },

  async getBillingInvoices(filters?: {
    publisher_id?: string;
    user_id?: string;
    context?: InvoiceContext;
    status?: InvoiceStatus;
    limit?: number;
    offset?: number;
  }): Promise<BillingInvoice[]> {
    const params = new URLSearchParams();
    if (filters?.publisher_id) params.set('publisher_id', filters.publisher_id);
    if (filters?.user_id) params.set('user_id', filters.user_id);
    if (filters?.context) params.set('context', filters.context);
    if (filters?.status) params.set('status', filters.status);
    params.set('limit', String(filters?.limit ?? 200));
    params.set('offset', String(filters?.offset ?? 0));
    const res = await request<{ invoices: BillingInvoice[] }>(
      `/api/v1/billing/invoices?${params.toString()}`,
    );
    return res.invoices;
  },

  async createBillingInvoice(payload: {
    publisher_id?: string;
    user_id?: string;
    context?: InvoiceContext;
    period_start?: string;
    period_end?: string;
    currency?: string;
    due_at?: string;
    notes?: string;
    tax_cents?: number;
    items: Array<{
      kind: BillingInvoice['items'][number]['kind'];
      description: string;
      quantity: number;
      unit_amount_cents: number;
      course_id?: string;
    }>;
  }): Promise<BillingInvoice> {
    const res = await request<{ invoice: BillingInvoice }>('/api/v1/billing/invoices', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    return res.invoice;
  },

  async generateUsageInvoice(
    publisherId: string,
    period?: { period_start: string; period_end: string },
  ): Promise<BillingInvoice> {
    const res = await request<{ invoice: BillingInvoice }>('/api/v1/billing/invoices/usage', {
      method: 'POST',
      body: JSON.stringify({
        publisher_id: publisherId,
        period_start: period?.period_start,
        period_end: period?.period_end,
      }),
    });
    return res.invoice;
  },

  async getBillingInvoice(id: string): Promise<{ invoice: BillingInvoice; payments: BillingPayment[] }> {
    return request(`/api/v1/billing/invoices/${encodeURIComponent(id)}`);
  },

  async recordBillingPayment(
    invoiceId: string,
    payload: {
      amount_cents: number;
      method?: PaymentMethod;
      reference?: string;
      notes?: string;
      received_at?: string;
      currency?: string;
    },
  ): Promise<{ payment: BillingPayment; invoice: BillingInvoice }> {
    return request(`/api/v1/billing/invoices/${encodeURIComponent(invoiceId)}/payments`, {
      method: 'POST',
      body: JSON.stringify(payload),
    });
  },

  async voidBillingInvoice(id: string): Promise<BillingInvoice> {
    const res = await request<{ invoice: BillingInvoice }>(
      `/api/v1/billing/invoices/${encodeURIComponent(id)}/void`,
      { method: 'POST' },
    );
    return res.invoice;
  },

  async getBillingPayments(filters?: {
    publisher_id?: string;
    user_id?: string;
    invoice_id?: string;
    limit?: number;
    offset?: number;
  }): Promise<BillingPayment[]> {
    const params = new URLSearchParams();
    if (filters?.publisher_id) params.set('publisher_id', filters.publisher_id);
    if (filters?.user_id) params.set('user_id', filters.user_id);
    if (filters?.invoice_id) params.set('invoice_id', filters.invoice_id);
    params.set('limit', String(filters?.limit ?? 200));
    params.set('offset', String(filters?.offset ?? 0));
    const res = await request<{ payments: BillingPayment[] }>(
      `/api/v1/billing/payments?${params.toString()}`,
    );
    return res.payments;
  },

  async getBillingRevenue(
    publisherId: string,
    period?: { period_start: string; period_end: string },
  ): Promise<PublisherRevenueSummary> {
    const params = new URLSearchParams({ publisher_id: publisherId });
    if (period) {
      params.set('period_start', period.period_start);
      params.set('period_end', period.period_end);
    }
    return request<PublisherRevenueSummary>(`/api/v1/billing/revenue?${params.toString()}`);
  },

  async getBillingPayouts(publisherId?: string): Promise<PublisherPayout[]> {
    const params = new URLSearchParams();
    if (publisherId) params.set('publisher_id', publisherId);
    params.set('limit', '200');
    const res = await request<{ payouts: PublisherPayout[] }>(
      `/api/v1/billing/payouts?${params.toString()}`,
    );
    return res.payouts;
  },

  async createBillingPayout(payload: {
    publisher_id: string;
    period_start?: string;
    period_end?: string;
    amount_cents: number;
    currency?: string;
    method?: string;
    reference?: string;
    status?: 'pending' | 'paid' | 'void';
    paid_at?: string;
    notes?: string;
  }): Promise<PublisherPayout> {
    const res = await request<{ payout: PublisherPayout }>('/api/v1/billing/payouts', {
      method: 'POST',
      body: JSON.stringify(payload),
    });
    return res.payout;
  },
};
