import React from 'react';
import {
  LayoutDashboard,
  Globe,
  LayoutGrid,
  Megaphone,
  BarChart3,
  History,
  FlaskConical,
  Inbox,
  Settings,
  Ticket,
  Users,
} from 'lucide-react';

export type NavTab =
  | 'overview'
  | 'websites'
  | 'placements'
  | 'campaigns'
  | 'affiliate'
  | 'publishers'
  | 'inquiries'
  | 'analytics'
  | 'audit'
  | 'sandbox'
  | 'settings';

export interface NavItem {
  id: NavTab;
  label: string;
  icon: React.ReactNode;
  badge?: string;
  roles?: string[];
}

export const NAV_ITEMS: NavItem[] = [
  { id: 'overview', label: 'Overview', icon: <LayoutDashboard className="w-4 h-4" /> },
  { id: 'websites', label: 'Websites', icon: <Globe className="w-4 h-4" /> },
  { id: 'placements', label: 'Placements', icon: <LayoutGrid className="w-4 h-4" /> },
  { id: 'campaigns', label: 'Ads & Campaigns', icon: <Megaphone className="w-4 h-4" /> },
  {
    id: 'affiliate',
    label: 'Affiliate Offers',
    icon: <Ticket className="w-4 h-4" />,
    roles: ['admin', 'publisher', 'analyst'],
  },
  {
    id: 'publishers',
    label: 'Publishers',
    icon: <Users className="w-4 h-4" />,
    roles: ['admin'],
  },
  {
    id: 'inquiries',
    label: 'Inquiries',
    icon: <Inbox className="w-4 h-4" />,
    roles: ['admin'],
  },
  { id: 'analytics', label: 'Analytics', icon: <BarChart3 className="w-4 h-4" /> },
  {
    id: 'audit',
    label: 'Audit Log',
    icon: <History className="w-4 h-4" />,
    roles: ['admin'],
  },
  {
    id: 'sandbox',
    label: 'Live Sandbox',
    icon: <FlaskConical className="w-4 h-4" />,
    badge: 'Test SDK',
  },
  { id: 'settings', label: 'Settings', icon: <Settings className="w-4 h-4" /> },
];

export function visibleNavItems(role?: string): NavItem[] {
  const effectiveRole = role || 'analyst';
  return NAV_ITEMS.filter((item) => !item.roles || item.roles.includes(effectiveRole));
}

interface SidebarProps {
  activeTab: NavTab;
  onTabChange: (tab: NavTab) => void;
  items?: NavItem[];
}

export const Sidebar: React.FC<SidebarProps> = ({ activeTab, onTabChange, items = NAV_ITEMS }) => {
  return (
    <aside className="w-64 bg-slate-900 text-slate-300 min-h-[calc(100vh-4rem)] p-4 flex flex-col justify-between shrink-0">
      <div className="flex flex-col gap-1">
        <div className="text-[11px] font-bold uppercase tracking-wider text-slate-500 px-3 py-2">
          Management
        </div>

        {items.map((item) => {
          const isActive = activeTab === item.id;
          return (
            <button
              key={item.id}
              onClick={() => onTabChange(item.id)}
              className={`flex items-center justify-between px-3 py-2.5 rounded-lg text-sm font-medium transition-all ${
                isActive
                  ? 'bg-blue-600 text-white shadow-md shadow-blue-600/30 font-semibold'
                  : 'text-slate-400 hover:text-slate-100 hover:bg-slate-800/80'
              }`}
            >
              <div className="flex items-center gap-3">
                <span className={isActive ? 'text-white' : 'text-slate-400'}>{item.icon}</span>
                <span>{item.label}</span>
              </div>
              {item.badge && (
                <span className="text-[10px] bg-blue-500/20 text-blue-300 font-semibold px-2 py-0.5 rounded-full border border-blue-500/30">
                  {item.badge}
                </span>
              )}
            </button>
          );
        })}
      </div>

      <div className="p-3 bg-slate-800/50 rounded-xl border border-slate-800 text-xs text-slate-400">
        <div className="font-semibold text-slate-200 mb-1">Centralized Control</div>
        <p className="text-[11px] leading-relaxed text-slate-400">
          Snippet renders centrally scheduled ads. Changes propagate under 60 seconds without site redeployment.
        </p>
      </div>
    </aside>
  );
};
