import React, { useCallback, useEffect, useMemo, useState } from 'react';
import {
  AlertTriangle,
  CheckCircle2,
  CreditCard,
  Edit,
  Loader2,
  Plus,
  ReceiptText,
  RefreshCw,
  TrendingUp,
  Wallet,
} from 'lucide-react';
import { api } from '../api';
import {
  BillingInvoice,
  BillingPayment,
  BillingPlan,
  BillingSubscriptionPayload,
  PaymentMethod,
  Publisher,
  PublisherPayout,
} from '../types';
import { Modal } from '../components/Modal';

interface BillingViewProps {
  role: string;
  onNotify: (type: 'success' | 'error' | 'info', msg: string) => void;
}

const INVOICE_STYLES: Record<string, string> = {
  open: 'bg-amber-50 text-amber-700 border-amber-200',
  partially_paid: 'bg-blue-50 text-blue-700 border-blue-200',
  paid: 'bg-emerald-50 text-emerald-700 border-emerald-200',
  overdue: 'bg-rose-50 text-rose-700 border-rose-200',
  void: 'bg-slate-100 text-slate-500 border-slate-200',
};

const PAYOUT_STYLES: Record<string, string> = {
  pending: 'bg-amber-50 text-amber-700 border-amber-200',
  paid: 'bg-emerald-50 text-emerald-700 border-emerald-200',
  void: 'bg-slate-100 text-slate-500 border-slate-200',
};

function formatMoney(cents: number | undefined, currency = 'USD'): string {
  const value = (cents || 0) / 100;
  try {
    return new Intl.NumberFormat('en-US', {
      style: 'currency',
      currency: currency || 'USD',
      maximumFractionDigits: 2,
    }).format(value);
  } catch (_) {
    return `${currency || 'USD'} ${value.toFixed(2)}`;
  }
}

function formatNumber(value: number | undefined): string {
  return new Intl.NumberFormat('en-US').format(value || 0);
}

function formatDate(value?: string): string {
  if (!value) return '—';
  const date = new Date(value);
  return Number.isNaN(date.getTime()) ? value : date.toLocaleString();
}

function dollarsToCents(value: string): number {
  const parsed = Number.parseFloat(value);
  return Number.isFinite(parsed) ? Math.round(parsed * 100) : 0;
}

function centsToDollars(cents: number): string {
  return (cents / 100).toFixed(2);
}

function limitLabel(value: number, unit: string): string {
  return value < 0 ? `Unlimited ${unit}` : `${formatNumber(value)} ${unit}`;
}

function usagePercent(used: number, included: number): number {
  if (included < 0) return 0;
  if (included === 0) return used > 0 ? 100 : 0;
  return Math.min(100, Math.round((used / included) * 100));
}

export const BillingView: React.FC<BillingViewProps> = ({ role, onNotify }) => {
  const isAdmin = role === 'admin';
  const [publishers, setPublishers] = useState<Publisher[]>([]);
  const [selectedPublisherId, setSelectedPublisherId] = useState('');
  const [plans, setPlans] = useState<BillingPlan[]>([]);
  const [billing, setBilling] = useState<BillingSubscriptionPayload | null>(null);
  const [invoices, setInvoices] = useState<BillingInvoice[]>([]);
  const [payments, setPayments] = useState<BillingPayment[]>([]);
  const [payouts, setPayouts] = useState<PublisherPayout[]>([]);
  const [loading, setLoading] = useState(true);
  const [busy, setBusy] = useState(false);

  const [selectedInvoice, setSelectedInvoice] = useState<BillingInvoice | null>(null);
  const [paymentOpen, setPaymentOpen] = useState(false);
  const [paymentAmount, setPaymentAmount] = useState('');
  const [paymentMethod, setPaymentMethod] = useState<PaymentMethod>('manual');
  const [paymentReference, setPaymentReference] = useState('');
  const [paymentNotes, setPaymentNotes] = useState('');

  const [payoutOpen, setPayoutOpen] = useState(false);
  const [payoutAmount, setPayoutAmount] = useState('');
  const [payoutReference, setPayoutReference] = useState('');
  const [payoutNotes, setPayoutNotes] = useState('');

  const [editPlan, setEditPlan] = useState<BillingPlan | null>(null);
  const [planName, setPlanName] = useState('');
  const [planDescription, setPlanDescription] = useState('');
  const [planPrice, setPlanPrice] = useState('');
  const [planCampaigns, setPlanCampaigns] = useState('');
  const [planImpressions, setPlanImpressions] = useState('');
  const [planOverage, setPlanOverage] = useState('');
  const [planShare, setPlanShare] = useState('');
  const [planFeatures, setPlanFeatures] = useState('');

  const selectedPublisher = useMemo(
    () => publishers.find((publisher) => publisher.id === selectedPublisherId) || null,
    [publishers, selectedPublisherId],
  );

  const load = useCallback(async () => {
    setLoading(true);
    try {
      const planRows = await api.getBillingPlans(isAdmin);
      setPlans(planRows);

      let target = selectedPublisherId;
      if (isAdmin) {
        const publisherRows = await api.getPublishers();
        setPublishers(publisherRows);
        if (!target && publisherRows.length > 0) {
          target = publisherRows[0].id;
          setSelectedPublisherId(target);
        }
      }

      if (isAdmin && !target) {
        setBilling(null);
        setInvoices([]);
        setPayments([]);
        setPayouts([]);
        return;
      }

      if (!isAdmin) target = '';
      const [subscription, invoiceRows, paymentRows, payoutRows] = await Promise.all([
        api.getBillingSubscription(target || undefined),
        api.getBillingInvoices(target ? { publisher_id: target } : undefined),
        api.getBillingPayments(target ? { publisher_id: target } : undefined),
        api.getBillingPayouts(target || undefined),
      ]);
      setBilling(subscription);
      setInvoices(invoiceRows);
      setPayments(paymentRows);
      setPayouts(payoutRows);
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to load billing data');
    } finally {
      setLoading(false);
    }
  }, [isAdmin, onNotify, selectedPublisherId]);

  useEffect(() => {
    load();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [isAdmin, selectedPublisherId]);

  const outstandingCents = useMemo(
    () =>
      invoices
        .filter((invoice) => invoice.status === 'open' || invoice.status === 'partially_paid' || invoice.status === 'overdue')
        .reduce((total, invoice) => total + invoice.balance_cents, 0),
    [invoices],
  );

  const currentPlanId = billing?.subscription.plan_id || billing?.plan?.id || '';

  const canManage = isAdmin || !!billing;

  const handleChangePlan = async (plan: BillingPlan) => {
    if (plan.id === currentPlanId) return;
    if (plan.price_cents > 0) {
      const confirmed = window.confirm(
        `Switch to ${plan.name}? An open invoice for ${formatMoney(plan.price_cents, plan.currency)} will be created and must be settled offline.`,
      );
      if (!confirmed) return;
    } else if (!window.confirm(`Switch to ${plan.name}?`)) {
      return;
    }
    setBusy(true);
    try {
      const result = await api.changeBillingPlan(plan.id, isAdmin ? selectedPublisherId : undefined);
      onNotify('success', `Plan changed to ${result.plan?.name || plan.name}.`);
      if (result.invoice) {
        onNotify('info', `Invoice ${formatMoney(result.invoice.total_cents, result.invoice.currency)} is awaiting payment.`);
      }
      await load();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to change plan');
    } finally {
      setBusy(false);
    }
  };

  const handleGenerateInvoice = async () => {
    if (!isAdmin || !selectedPublisherId) return;
    setBusy(true);
    try {
      const period = billing
        ? { period_start: billing.usage.period_start, period_end: billing.usage.period_end }
        : undefined;
      const invoice = await api.generateUsageInvoice(selectedPublisherId, period);
      onNotify('success', `Usage invoice ${formatMoney(invoice.total_cents, invoice.currency)} generated.`);
      await load();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to generate usage invoice');
    } finally {
      setBusy(false);
    }
  };

  const openPayment = (invoice: BillingInvoice) => {
    setSelectedInvoice(invoice);
    setPaymentAmount(centsToDollars(invoice.balance_cents));
    setPaymentMethod('manual');
    setPaymentReference('');
    setPaymentNotes('');
    setPaymentOpen(true);
  };

  const handleRecordPayment = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!selectedInvoice) return;
    const amount = dollarsToCents(paymentAmount);
    if (amount <= 0) {
      onNotify('error', 'Enter a payment amount greater than zero.');
      return;
    }
    setBusy(true);
    try {
      const result = await api.recordBillingPayment(selectedInvoice.id, {
        amount_cents: amount,
        method: paymentMethod,
        reference: paymentReference,
        notes: paymentNotes,
        currency: selectedInvoice.currency,
      });
      onNotify('success', `Payment of ${formatMoney(amount, selectedInvoice.currency)} recorded.`);
      if (result.invoice.status === 'paid' && selectedInvoice.context === 'course') {
        onNotify('info', 'Course access has been granted automatically.');
      }
      setPaymentOpen(false);
      setSelectedInvoice(null);
      await load();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to record payment');
    } finally {
      setBusy(false);
    }
  };

  const handleVoid = async (invoice: BillingInvoice) => {
    if (!window.confirm(`Void invoice ${invoice.id}? This cannot be undone.`)) return;
    setBusy(true);
    try {
      await api.voidBillingInvoice(invoice.id);
      onNotify('success', 'Invoice voided.');
      await load();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to void invoice');
    } finally {
      setBusy(false);
    }
  };

  const handlePayout = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!isAdmin || !selectedPublisherId) return;
    const amount = dollarsToCents(payoutAmount);
    if (amount <= 0) {
      onNotify('error', 'Enter a payout amount greater than zero.');
      return;
    }
    setBusy(true);
    try {
      await api.createBillingPayout({
        publisher_id: selectedPublisherId,
        amount_cents: amount,
        reference: payoutReference,
        notes: payoutNotes,
        status: 'paid',
      });
      onNotify('success', `Payout of ${formatMoney(amount)} recorded.`);
      setPayoutOpen(false);
      setPayoutAmount('');
      setPayoutReference('');
      setPayoutNotes('');
      await load();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to record payout');
    } finally {
      setBusy(false);
    }
  };

  const openPlanEditor = (plan: BillingPlan) => {
    setEditPlan(plan);
    setPlanName(plan.name);
    setPlanDescription(plan.description);
    setPlanPrice(centsToDollars(plan.price_cents));
    setPlanCampaigns(String(plan.included_campaigns));
    setPlanImpressions(String(plan.included_impressions));
    setPlanOverage(centsToDollars(plan.overage_per_1000_impressions_cents));
    setPlanShare(String(plan.revenue_share_percent));
    setPlanFeatures(plan.features.join('\n'));
  };

  const handleSavePlan = async (event: React.FormEvent) => {
    event.preventDefault();
    if (!editPlan) return;
    setBusy(true);
    try {
      await api.updateBillingPlan(editPlan.id, {
        name: planName,
        description: planDescription,
        price_cents: dollarsToCents(planPrice),
        included_campaigns: Number.parseInt(planCampaigns, 10) || 0,
        included_impressions: Number.parseInt(planImpressions, 10) || 0,
        overage_per_1000_impressions_cents: dollarsToCents(planOverage),
        revenue_share_percent: Number.parseInt(planShare, 10) || 0,
        features: planFeatures
          .split('\n')
          .map((line) => line.trim())
          .filter(Boolean),
      });
      onNotify('success', `Plan "${planName}" updated.`);
      setEditPlan(null);
      await load();
    } catch (err: any) {
      onNotify('error', err.message || 'Failed to update plan');
    } finally {
      setBusy(false);
    }
  };

  if (loading) {
    return (
      <div className="p-12 text-center text-slate-500 flex items-center justify-center gap-2">
        <Loader2 className="w-4 h-4 animate-spin" /> Loading billing...
      </div>
    );
  }

  if (isAdmin && publishers.length === 0) {
    return (
      <div className="bg-white border border-slate-200 rounded-xl p-12 text-center text-slate-500">
        <Wallet className="w-10 h-10 text-slate-300 mx-auto mb-3" />
        <div className="font-semibold text-slate-700">No publishers yet</div>
        <div className="text-xs mt-1">Provision a publisher account before configuring billing.</div>
      </div>
    );
  }

  const usage = billing?.usage;
  const revenue = billing?.revenue;
  const plan = billing?.plan;
  const campaignPercent = usage ? usagePercent(usage.campaigns, usage.included_campaigns) : 0;
  const impressionPercent = usage ? usagePercent(usage.impressions, usage.included_impressions) : 0;

  return (
    <div className="flex flex-col gap-6">
      <div className="flex flex-col lg:flex-row lg:items-end justify-between gap-4">
        <div>
          <h2 className="text-2xl font-bold text-slate-900">Billing &amp; Monetization</h2>
          <p className="text-sm text-slate-500 mt-1 max-w-3xl">
            Manage the publisher&apos;s plan, usage allowances, offline invoices, payments, and revenue-share payouts.
            Settlement is provider-agnostic: record a bank transfer, cash, or other manual payment when it arrives.
          </p>
        </div>
        <div className="flex flex-wrap items-center gap-2">
          {isAdmin && (
            <select
              value={selectedPublisherId}
              onChange={(event) => setSelectedPublisherId(event.target.value)}
              className="px-3 py-2 bg-white border border-slate-200 rounded-lg text-xs font-semibold text-slate-700 focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              {publishers.map((publisher) => (
                <option key={publisher.id} value={publisher.id}>
                  {publisher.name}
                </option>
              ))}
            </select>
          )}
          <button
            onClick={load}
            disabled={busy}
            className="inline-flex items-center gap-1.5 px-3 py-2 bg-white border border-slate-200 hover:bg-slate-50 text-slate-700 font-semibold text-xs rounded-lg disabled:opacity-50"
          >
            <RefreshCw className="w-4 h-4" /> Refresh
          </button>
          {isAdmin && (
            <>
              <button
                onClick={handleGenerateInvoice}
                disabled={busy || !selectedPublisherId}
                className="inline-flex items-center gap-1.5 px-3 py-2 bg-blue-600 hover:bg-blue-700 text-white font-semibold text-xs rounded-lg disabled:opacity-50"
              >
                <ReceiptText className="w-4 h-4" /> Generate usage invoice
              </button>
              <button
                onClick={() => setPayoutOpen(true)}
                disabled={busy || !selectedPublisherId}
                className="inline-flex items-center gap-1.5 px-3 py-2 bg-emerald-600 hover:bg-emerald-700 text-white font-semibold text-xs rounded-lg disabled:opacity-50"
              >
                <Wallet className="w-4 h-4" /> Record payout
              </button>
            </>
          )}
        </div>
      </div>

      {isAdmin && selectedPublisher && (
        <div className="rounded-lg border border-slate-200 bg-white px-4 py-3 text-xs text-slate-500">
          Billing tenant: <span className="font-semibold text-slate-800">{selectedPublisher.name}</span>
          {selectedPublisher.owner_email && <> · {selectedPublisher.owner_email}</>}
          <span className="font-mono text-[10px] text-slate-400 ml-2">{selectedPublisher.id}</span>
        </div>
      )}

      <div className="grid grid-cols-1 sm:grid-cols-2 xl:grid-cols-4 gap-4">
        <div className="bg-white border border-slate-200 rounded-xl p-5 shadow-sm">
          <div className="flex items-center justify-between">
            <div className="text-xs font-semibold uppercase tracking-wider text-slate-500">Current plan</div>
            <CreditCard className="w-5 h-5 text-blue-500" />
          </div>
          <div className="mt-2 text-2xl font-bold text-slate-900">{plan?.name || 'Free'}</div>
          <div className="text-xs text-slate-500 mt-1">
            {plan ? `${formatMoney(plan.price_cents, plan.currency)} / ${plan.billing_interval}` : 'No plan assigned'}
          </div>
          <div className="mt-3 text-[11px] text-slate-400">
            {plan?.revenue_share_percent ?? 0}% revenue share · {plan && plan.overage_per_1000_impressions_cents > 0 ? 'overage billed' : 'hard impression cap'}
          </div>
        </div>

        <div className="bg-white border border-slate-200 rounded-xl p-5 shadow-sm">
          <div className="flex items-center justify-between">
            <div className="text-xs font-semibold uppercase tracking-wider text-slate-500">Impressions</div>
            <TrendingUp className="w-5 h-5 text-indigo-500" />
          </div>
          <div className="mt-2 text-2xl font-bold text-slate-900">
            {formatNumber(usage?.impressions)}
            <span className="text-sm font-medium text-slate-400">
              {' '}/ {usage && usage.included_impressions < 0 ? '∞' : formatNumber(usage?.included_impressions)}
            </span>
          </div>
          <div className="mt-3 h-2 bg-slate-100 rounded-full overflow-hidden">
            <div
              className={`h-full rounded-full ${usage?.impression_quota_exceeded ? 'bg-rose-500' : 'bg-blue-500'}`}
              style={{ width: `${usage && usage.included_impressions < 0 ? 4 : impressionPercent}%` }}
            />
          </div>
          <div className="mt-2 text-[11px] text-slate-400">
            {usage?.overage_cents
              ? `${formatMoney(usage.overage_cents, plan?.currency)} overage this period`
              : usage?.hard_cap
                ? 'Hard cap: delivery stops at the allowance'
                : 'No overage charges this period'}
          </div>
        </div>

        <div className="bg-white border border-slate-200 rounded-xl p-5 shadow-sm">
          <div className="flex items-center justify-between">
            <div className="text-xs font-semibold uppercase tracking-wider text-slate-500">Campaigns</div>
            <CheckCircle2 className="w-5 h-5 text-emerald-500" />
          </div>
          <div className="mt-2 text-2xl font-bold text-slate-900">
            {formatNumber(usage?.campaigns)}
            <span className="text-sm font-medium text-slate-400">
              {' '}/ {usage && usage.included_campaigns < 0 ? '∞' : formatNumber(usage?.included_campaigns)}
            </span>
          </div>
          <div className="mt-3 h-2 bg-slate-100 rounded-full overflow-hidden">
            <div
              className={`h-full rounded-full ${usage?.campaign_quota_exceeded ? 'bg-rose-500' : 'bg-emerald-500'}`}
              style={{ width: `${usage && usage.included_campaigns < 0 ? 4 : campaignPercent}%` }}
            />
          </div>
          <div className="mt-2 text-[11px] text-slate-400">
            {usage?.campaign_quota_exceeded
              ? 'Campaign limit reached'
              : 'Active, draft, and paused campaigns count toward the limit'}
          </div>
        </div>

        <div className="bg-white border border-slate-200 rounded-xl p-5 shadow-sm">
          <div className="flex items-center justify-between">
            <div className="text-xs font-semibold uppercase tracking-wider text-slate-500">Publisher revenue</div>
            <Wallet className="w-5 h-5 text-amber-500" />
          </div>
          <div className="mt-2 text-2xl font-bold text-slate-900">
            {formatMoney(revenue?.publisher_cents, plan?.currency)}
          </div>
          <div className="text-xs text-slate-500 mt-1">
            {revenue?.share_percent ?? 0}% of {formatMoney(revenue?.gross_cents, plan?.currency)} direct spend
          </div>
          <div className="mt-3 text-[11px] text-slate-400">
            {formatNumber(revenue?.direct_impressions)} direct impressions · outstanding{' '}
            {formatMoney(outstandingCents, plan?.currency)}
          </div>
        </div>
      </div>

      <div>
        <div className="flex items-center justify-between mb-3">
          <h3 className="text-lg font-semibold text-slate-900">Plans</h3>
          <span className="text-xs text-slate-400">Prices shown per billing interval</span>
        </div>
        <div className="grid grid-cols-1 md:grid-cols-2 xl:grid-cols-4 gap-4">
          {plans.map((planRow) => {
            const isCurrent = planRow.id === currentPlanId;
            return (
              <div
                key={planRow.id}
                className={`bg-white border rounded-xl p-5 shadow-sm flex flex-col ${
                  isCurrent ? 'border-blue-500 ring-2 ring-blue-100' : 'border-slate-200'
                }`}
              >
                <div className="flex items-start justify-between gap-2">
                  <div>
                    <div className="text-lg font-bold text-slate-900">{planRow.name}</div>
                    <div className="text-xs text-slate-500 mt-1 min-h-[32px]">{planRow.description}</div>
                  </div>
                  {isAdmin && (
                    <button
                      onClick={() => openPlanEditor(planRow)}
                      className="p-1.5 text-slate-400 hover:text-slate-700 hover:bg-slate-100 rounded-lg"
                      title="Edit plan"
                    >
                      <Edit className="w-4 h-4" />
                    </button>
                  )}
                </div>
                <div className="mt-3 text-2xl font-bold text-slate-900">
                  {formatMoney(planRow.price_cents, planRow.currency)}
                  <span className="text-xs font-medium text-slate-400"> / {planRow.billing_interval}</span>
                </div>
                <ul className="mt-4 space-y-1.5 text-xs text-slate-600 flex-1">
                  <li>{limitLabel(planRow.included_campaigns, 'campaigns')}</li>
                  <li>{limitLabel(planRow.included_impressions, 'impressions / month')}</li>
                  <li>{planRow.revenue_share_percent}% publisher revenue share</li>
                  {planRow.overage_per_1000_impressions_cents > 0 ? (
                    <li>{formatMoney(planRow.overage_per_1000_impressions_cents, planRow.currency)} per 1,000 overage impressions</li>
                  ) : (
                    <li>Hard impression cap (no overage)</li>
                  )}
                  {planRow.features.map((feature) => (
                    <li key={feature} className="text-slate-500">
                      · {feature}
                    </li>
                  ))}
                </ul>
                <button
                  onClick={() => handleChangePlan(planRow)}
                  disabled={busy || isCurrent || planRow.status !== 'active' || !canManage}
                  className={`mt-4 w-full px-3 py-2 rounded-lg text-xs font-semibold transition-colors ${
                    isCurrent
                      ? 'bg-blue-50 text-blue-700 border border-blue-200'
                      : 'bg-slate-900 hover:bg-slate-800 text-white disabled:opacity-40'
                  }`}
                >
                  {isCurrent ? 'Current plan' : planRow.status !== 'active' ? 'Archived' : 'Switch to this plan'}
                </button>
              </div>
            );
          })}
        </div>
      </div>

      <div className="bg-white border border-slate-200 rounded-xl shadow-sm overflow-hidden">
        <div className="p-4 border-b border-slate-100 flex flex-wrap items-center justify-between gap-3">
          <div>
            <h3 className="text-sm font-semibold text-slate-900">Invoices</h3>
            <p className="text-[11px] text-slate-500 mt-0.5">
              Subscription, overage, course, and manual invoices. Record offline payments to settle a balance.
            </p>
          </div>
          <div className="text-xs text-slate-500">
            Outstanding <span className="font-semibold text-slate-900">{formatMoney(outstandingCents, plan?.currency)}</span>
          </div>
        </div>
        {invoices.length === 0 ? (
          <div className="p-10 text-center text-slate-500 text-xs">No invoices yet.</div>
        ) : (
          <div className="overflow-x-auto">
            <table className="w-full text-left text-sm">
              <thead className="bg-slate-50 border-b border-slate-200 text-xs font-semibold uppercase tracking-wider text-slate-500">
                <tr>
                  <th className="px-4 py-3.5">Invoice</th>
                  <th className="px-4 py-3.5">Context</th>
                  <th className="px-4 py-3.5">Period</th>
                  <th className="px-4 py-3.5 text-right">Total</th>
                  <th className="px-4 py-3.5 text-right">Balance</th>
                  <th className="px-4 py-3.5">Status</th>
                  <th className="px-4 py-3.5">Issued</th>
                  <th className="px-4 py-3.5 text-right">Actions</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-slate-100">
                {invoices.map((invoice) => (
                  <tr key={invoice.id} className="hover:bg-slate-50/60">
                    <td className="px-4 py-3">
                      <button
                        onClick={() => setSelectedInvoice(invoice)}
                        className="font-mono text-[11px] font-semibold text-blue-700 hover:underline"
                      >
                        {invoice.id.slice(0, 8)}…
                      </button>
                      <div className="text-[10px] text-slate-400">{invoice.items.length} line(s)</div>
                    </td>
                    <td className="px-4 py-3 text-xs capitalize text-slate-600">{invoice.context}</td>
                    <td className="px-4 py-3 text-[11px] text-slate-500">
                      {invoice.period_start ? `${formatDate(invoice.period_start)} → ${formatDate(invoice.period_end)}` : '—'}
                    </td>
                    <td className="px-4 py-3 text-right text-xs font-semibold text-slate-900">
                      {formatMoney(invoice.total_cents, invoice.currency)}
                    </td>
                    <td className="px-4 py-3 text-right text-xs text-slate-600">
                      {formatMoney(invoice.balance_cents, invoice.currency)}
                    </td>
                    <td className="px-4 py-3">
                      <span className={`inline-flex text-[10px] font-semibold px-2 py-0.5 rounded-full border ${INVOICE_STYLES[invoice.status] || ''}`}>
                        {invoice.status.replace('_', ' ')}
                      </span>
                    </td>
                    <td className="px-4 py-3 text-[11px] text-slate-500">{formatDate(invoice.issued_at || invoice.created_at)}</td>
                    <td className="px-4 py-3 text-right">
                      <div className="flex items-center justify-end gap-1.5">
                        <button
                          onClick={() => setSelectedInvoice(invoice)}
                          className="px-2 py-1 text-[11px] font-semibold text-slate-600 hover:bg-slate-100 rounded"
                        >
                          View
                        </button>
                        {isAdmin && invoice.status !== 'paid' && invoice.status !== 'void' && (
                          <button
                            onClick={() => openPayment(invoice)}
                            disabled={busy}
                            className="px-2 py-1 text-[11px] font-semibold text-emerald-700 hover:bg-emerald-50 rounded disabled:opacity-50"
                          >
                            Pay
                          </button>
                        )}
                        {isAdmin && (invoice.status === 'open' || invoice.status === 'partially_paid' || invoice.status === 'overdue') && (
                          <button
                            onClick={() => handleVoid(invoice)}
                            disabled={busy}
                            className="px-2 py-1 text-[11px] font-semibold text-rose-600 hover:bg-rose-50 rounded disabled:opacity-50"
                          >
                            Void
                          </button>
                        )}
                      </div>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>

      <div className="grid grid-cols-1 xl:grid-cols-2 gap-6">
        <div className="bg-white border border-slate-200 rounded-xl shadow-sm overflow-hidden">
          <div className="p-4 border-b border-slate-100">
            <h3 className="text-sm font-semibold text-slate-900">Payment history</h3>
          </div>
          {payments.length === 0 ? (
            <div className="p-8 text-center text-slate-500 text-xs">No payments recorded yet.</div>
          ) : (
            <div className="divide-y divide-slate-100">
              {payments.slice(0, 12).map((payment) => (
                <div key={payment.id} className="px-4 py-3 flex items-center justify-between gap-3">
                  <div>
                    <div className="text-xs font-semibold text-slate-900">
                      {formatMoney(payment.amount_cents, payment.currency)}
                      <span className="ml-2 text-[10px] font-medium uppercase text-slate-400">{payment.method.replace('_', ' ')}</span>
                    </div>
                    <div className="text-[11px] text-slate-500">
                      {formatDate(payment.received_at)} · invoice {payment.invoice_id.slice(0, 8)}…
                      {payment.reference ? ` · ${payment.reference}` : ''}
                    </div>
                  </div>
                  <div className="text-[10px] text-slate-400 text-right">{payment.recorded_by || 'system'}</div>
                </div>
              ))}
            </div>
          )}
        </div>

        <div className="bg-white border border-slate-200 rounded-xl shadow-sm overflow-hidden">
          <div className="p-4 border-b border-slate-100 flex items-center justify-between">
            <div>
              <h3 className="text-sm font-semibold text-slate-900">Revenue-share payouts</h3>
              <p className="text-[11px] text-slate-500 mt-0.5">
                {formatMoney(revenue?.publisher_cents, plan?.currency)} earned this period
              </p>
            </div>
            {isAdmin && (
              <button
                onClick={() => setPayoutOpen(true)}
                disabled={busy}
                className="inline-flex items-center gap-1 px-2.5 py-1.5 bg-slate-900 hover:bg-slate-800 text-white text-[11px] font-semibold rounded-lg disabled:opacity-50"
              >
                <Plus className="w-3.5 h-3.5" /> Payout
              </button>
            )}
          </div>
          {payouts.length === 0 ? (
            <div className="p-8 text-center text-slate-500 text-xs">No payouts recorded yet.</div>
          ) : (
            <div className="divide-y divide-slate-100">
              {payouts.slice(0, 12).map((payout) => (
                <div key={payout.id} className="px-4 py-3 flex items-center justify-between gap-3">
                  <div>
                    <div className="text-xs font-semibold text-slate-900">
                      {formatMoney(payout.amount_cents, payout.currency)}
                      <span className={`ml-2 inline-flex text-[10px] font-semibold px-2 py-0.5 rounded-full border ${PAYOUT_STYLES[payout.status] || ''}`}>
                        {payout.status}
                      </span>
                    </div>
                    <div className="text-[11px] text-slate-500">
                      {formatDate(payout.paid_at || payout.created_at)}
                      {payout.reference ? ` · ${payout.reference}` : ''}
                    </div>
                  </div>
                  <div className="text-[10px] text-slate-400 text-right">{payout.recorded_by || 'system'}</div>
                </div>
              ))}
            </div>
          )}
        </div>
      </div>

      {!isAdmin && plan && plan.overage_per_1000_impressions_cents === 0 && (
        <div className="rounded-lg border border-amber-200 bg-amber-50 px-4 py-3 text-xs text-amber-900 flex gap-2">
          <AlertTriangle className="w-4 h-4 shrink-0 mt-0.5" />
          <span>
            Your current plan has a hard impression cap. When the monthly allowance is exhausted, ad delivery and
            impression recording stop until the next period or until the plan is upgraded.
          </span>
        </div>
      )}

      <Modal
        isOpen={!!selectedInvoice}
        onClose={() => setSelectedInvoice(null)}
        title={`Invoice ${selectedInvoice?.id.slice(0, 8) || ''}`}
        maxWidth="max-w-2xl"
      >
        {selectedInvoice && (
          <div className="flex flex-col gap-4">
            <div className="grid grid-cols-2 sm:grid-cols-4 gap-3 text-xs">
              <div>
                <div className="text-slate-400 uppercase tracking-wider text-[10px]">Status</div>
                <div className="mt-1 font-semibold capitalize text-slate-800">{selectedInvoice.status.replace('_', ' ')}</div>
              </div>
              <div>
                <div className="text-slate-400 uppercase tracking-wider text-[10px]">Context</div>
                <div className="mt-1 font-semibold capitalize text-slate-800">{selectedInvoice.context}</div>
              </div>
              <div>
                <div className="text-slate-400 uppercase tracking-wider text-[10px]">Issued</div>
                <div className="mt-1 font-semibold text-slate-800">{formatDate(selectedInvoice.issued_at || selectedInvoice.created_at)}</div>
              </div>
              <div>
                <div className="text-slate-400 uppercase tracking-wider text-[10px]">Due</div>
                <div className="mt-1 font-semibold text-slate-800">{formatDate(selectedInvoice.due_at)}</div>
              </div>
            </div>

            <div className="border border-slate-200 rounded-lg overflow-hidden">
              <table className="w-full text-left text-xs">
                <thead className="bg-slate-50 text-[10px] uppercase tracking-wider text-slate-500">
                  <tr>
                    <th className="px-3 py-2">Description</th>
                    <th className="px-3 py-2">Kind</th>
                    <th className="px-3 py-2 text-right">Qty</th>
                    <th className="px-3 py-2 text-right">Unit</th>
                    <th className="px-3 py-2 text-right">Amount</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-slate-100">
                  {selectedInvoice.items.map((item) => (
                    <tr key={item.id}>
                      <td className="px-3 py-2 text-slate-700">{item.description}</td>
                      <td className="px-3 py-2 capitalize text-slate-500">{item.kind}</td>
                      <td className="px-3 py-2 text-right">{item.quantity}</td>
                      <td className="px-3 py-2 text-right">{formatMoney(item.unit_amount_cents, selectedInvoice.currency)}</td>
                      <td className="px-3 py-2 text-right font-semibold">{formatMoney(item.amount_cents, selectedInvoice.currency)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>

            <div className="flex flex-col items-end gap-1 text-xs">
              <div className="flex justify-between w-56">
                <span className="text-slate-500">Subtotal</span>
                <span>{formatMoney(selectedInvoice.subtotal_cents, selectedInvoice.currency)}</span>
              </div>
              <div className="flex justify-between w-56">
                <span className="text-slate-500">Tax</span>
                <span>{formatMoney(selectedInvoice.tax_cents, selectedInvoice.currency)}</span>
              </div>
              <div className="flex justify-between w-56 font-semibold text-slate-900 border-t border-slate-200 pt-1">
                <span>Total</span>
                <span>{formatMoney(selectedInvoice.total_cents, selectedInvoice.currency)}</span>
              </div>
              <div className="flex justify-between w-56 text-emerald-700">
                <span>Paid</span>
                <span>{formatMoney(selectedInvoice.amount_paid_cents, selectedInvoice.currency)}</span>
              </div>
              <div className="flex justify-between w-56 font-semibold text-rose-600">
                <span>Balance</span>
                <span>{formatMoney(selectedInvoice.balance_cents, selectedInvoice.currency)}</span>
              </div>
            </div>

            {selectedInvoice.notes && (
              <div className="rounded-lg bg-slate-50 border border-slate-200 px-3 py-2 text-xs text-slate-600">
                {selectedInvoice.notes}
              </div>
            )}

            <div className="flex justify-end gap-2 pt-2 border-t border-slate-100">
              <button
                onClick={() => setSelectedInvoice(null)}
                className="px-4 py-2 text-xs font-semibold text-slate-600 hover:bg-slate-100 rounded-lg"
              >
                Close
              </button>
              {isAdmin && selectedInvoice.status !== 'paid' && selectedInvoice.status !== 'void' && (
                <button
                  onClick={() => openPayment(selectedInvoice)}
                  disabled={busy}
                  className="px-4 py-2 text-xs font-semibold bg-emerald-600 hover:bg-emerald-700 text-white rounded-lg disabled:opacity-50"
                >
                  Record payment
                </button>
              )}
            </div>
          </div>
        )}
      </Modal>

      <Modal isOpen={paymentOpen} onClose={() => setPaymentOpen(false)} title="Record offline payment">
        <form onSubmit={handleRecordPayment} className="flex flex-col gap-4">
          <div className="rounded-lg bg-slate-50 border border-slate-200 px-3 py-2 text-xs text-slate-600">
            Invoice {selectedInvoice?.id.slice(0, 8)} · balance{' '}
            {formatMoney(selectedInvoice?.balance_cents || 0, selectedInvoice?.currency)}
          </div>
          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">Amount</label>
            <input
              required
              type="number"
              min="0.01"
              step="0.01"
              value={paymentAmount}
              onChange={(event) => setPaymentAmount(event.target.value)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>
          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">Method</label>
            <select
              value={paymentMethod}
              onChange={(event) => setPaymentMethod(event.target.value as PaymentMethod)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            >
              <option value="manual">Manual</option>
              <option value="bank_transfer">Bank transfer</option>
              <option value="cash">Cash</option>
              <option value="other">Other</option>
            </select>
          </div>
          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">Reference</label>
            <input
              value={paymentReference}
              onChange={(event) => setPaymentReference(event.target.value)}
              placeholder="Bank reference, receipt number, etc."
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>
          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">Notes</label>
            <textarea
              rows={2}
              value={paymentNotes}
              onChange={(event) => setPaymentNotes(event.target.value)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>
          <div className="flex justify-end gap-2 pt-4 border-t border-slate-100">
            <button
              type="button"
              onClick={() => setPaymentOpen(false)}
              className="px-4 py-2 text-xs font-semibold text-slate-600 hover:bg-slate-100 rounded-lg"
            >
              Cancel
            </button>
            <button
              type="submit"
              disabled={busy}
              className="inline-flex items-center gap-1.5 px-4 py-2 text-xs font-semibold bg-emerald-600 hover:bg-emerald-700 text-white rounded-lg disabled:opacity-50"
            >
              {busy && <Loader2 className="w-3.5 h-3.5 animate-spin" />}
              Record payment
            </button>
          </div>
        </form>
      </Modal>

      <Modal isOpen={payoutOpen} onClose={() => setPayoutOpen(false)} title="Record publisher payout">
        <form onSubmit={handlePayout} className="flex flex-col gap-4">
          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">Amount</label>
            <input
              required
              type="number"
              min="0.01"
              step="0.01"
              value={payoutAmount}
              onChange={(event) => setPayoutAmount(event.target.value)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
            <p className="text-[11px] text-slate-400 mt-1">
              Period revenue share: {formatMoney(revenue?.publisher_cents, plan?.currency)}. Payouts are recorded after
              the offline transfer is made.
            </p>
          </div>
          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">Reference</label>
            <input
              value={payoutReference}
              onChange={(event) => setPayoutReference(event.target.value)}
              placeholder="Transfer reference"
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>
          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">Notes</label>
            <textarea
              rows={2}
              value={payoutNotes}
              onChange={(event) => setPayoutNotes(event.target.value)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>
          <div className="flex justify-end gap-2 pt-4 border-t border-slate-100">
            <button
              type="button"
              onClick={() => setPayoutOpen(false)}
              className="px-4 py-2 text-xs font-semibold text-slate-600 hover:bg-slate-100 rounded-lg"
            >
              Cancel
            </button>
            <button
              type="submit"
              disabled={busy}
              className="inline-flex items-center gap-1.5 px-4 py-2 text-xs font-semibold bg-emerald-600 hover:bg-emerald-700 text-white rounded-lg disabled:opacity-50"
            >
              {busy && <Loader2 className="w-3.5 h-3.5 animate-spin" />}
              Record payout
            </button>
          </div>
        </form>
      </Modal>

      <Modal isOpen={!!editPlan} onClose={() => setEditPlan(null)} title={`Edit plan: ${editPlan?.name || ''}`} maxWidth="max-w-2xl">
        <form onSubmit={handleSavePlan} className="flex flex-col gap-4">
          <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">Name</label>
              <input
                required
                value={planName}
                onChange={(event) => setPlanName(event.target.value)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">Monthly price</label>
              <input
                required
                type="number"
                min="0"
                step="0.01"
                value={planPrice}
                onChange={(event) => setPlanPrice(event.target.value)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
          </div>
          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">Description</label>
            <textarea
              rows={2}
              value={planDescription}
              onChange={(event) => setPlanDescription(event.target.value)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>
          <div className="grid grid-cols-2 sm:grid-cols-4 gap-4">
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">Campaigns</label>
              <input
                type="number"
                min="-1"
                value={planCampaigns}
                onChange={(event) => setPlanCampaigns(event.target.value)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
              <p className="text-[10px] text-slate-400 mt-1">-1 = unlimited</p>
            </div>
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">Impressions</label>
              <input
                type="number"
                min="-1"
                value={planImpressions}
                onChange={(event) => setPlanImpressions(event.target.value)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
              <p className="text-[10px] text-slate-400 mt-1">-1 = unlimited</p>
            </div>
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">Overage / 1k</label>
              <input
                type="number"
                min="0"
                step="0.01"
                value={planOverage}
                onChange={(event) => setPlanOverage(event.target.value)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
              <p className="text-[10px] text-slate-400 mt-1">0 = hard cap</p>
            </div>
            <div>
              <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">Revenue share %</label>
              <input
                type="number"
                min="0"
                max="100"
                value={planShare}
                onChange={(event) => setPlanShare(event.target.value)}
                className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
              />
            </div>
          </div>
          <div>
            <label className="block text-xs font-semibold text-slate-700 uppercase tracking-wider mb-1">
              Features (one per line)
            </label>
            <textarea
              rows={5}
              value={planFeatures}
              onChange={(event) => setPlanFeatures(event.target.value)}
              className="w-full px-3 py-2 bg-slate-50 border border-slate-200 rounded-lg text-sm font-mono focus:bg-white focus:outline-none focus:ring-2 focus:ring-blue-500"
            />
          </div>
          <div className="flex justify-end gap-2 pt-4 border-t border-slate-100">
            <button
              type="button"
              onClick={() => setEditPlan(null)}
              className="px-4 py-2 text-xs font-semibold text-slate-600 hover:bg-slate-100 rounded-lg"
            >
              Cancel
            </button>
            <button
              type="submit"
              disabled={busy}
              className="inline-flex items-center gap-1.5 px-4 py-2 text-xs font-semibold bg-blue-600 hover:bg-blue-700 text-white rounded-lg disabled:opacity-50"
            >
              {busy && <Loader2 className="w-3.5 h-3.5 animate-spin" />}
              Save plan
            </button>
          </div>
        </form>
      </Modal>
    </div>
  );
};
