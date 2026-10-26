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
  AppSettings,
  SandboxData,
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

  // Audit Logs
  async getAuditLogs(limit = 50, offset = 0): Promise<AuditLogItem[]> {
    const res = await request<{ audit_logs: AuditLogItem[] }>(
      `/api/v1/audit-log?limit=${limit}&offset=${offset}`,
    );
    return res.audit_logs;
  },
};
