import React, { useEffect, useState } from 'react';
import { User } from '../types';
import { api } from '../api';
import { LogOut, Activity, GraduationCap, LayoutDashboard, Globe, BookOpen } from 'lucide-react';

interface NavbarProps {
  user: User | null;
  onLogout: () => void;
  onOpenSandbox: () => void;
}

export const Navbar: React.FC<NavbarProps> = ({ user, onLogout, onOpenSandbox }) => {
  const [healthy, setHealthy] = useState<boolean | null>(null);

  useEffect(() => {
    const check = async () => {
      try {
        await api.getHealth();
        setHealthy(true);
      } catch (_) {
        setHealthy(false);
      }
    };
    check();
    const interval = setInterval(check, 20000);
    return () => clearInterval(interval);
  }, []);

  return (
    <header className="h-16 bg-white border-b border-slate-200 px-6 flex items-center justify-between sticky top-0 z-30">
      <div className="flex items-center gap-4">
        <div className="flex items-center gap-2.5">
          <div className="w-9 h-9 rounded-lg bg-blue-600 flex items-center justify-center text-white font-bold shadow-md shadow-blue-500/20">
            <LayoutDashboard className="w-5 h-5" />
          </div>
          <div>
            <h1 className="font-bold text-slate-900 text-lg leading-tight flex items-center gap-2">
              AdControl
              <span className="text-[10px] font-semibold uppercase tracking-wider bg-blue-50 text-blue-700 px-2 py-0.5 rounded-full border border-blue-200">
                v2.0 Monetization
              </span>
            </h1>
            <p className="text-xs text-slate-500">Centralized Link & Placement Management</p>
          </div>
        </div>

        {/* Backend Health Badge */}
        <div className="hidden sm:flex items-center gap-1.5 pl-4 border-l border-slate-200 text-xs">
          <Activity
            className={`w-4 h-4 ${
              healthy === true
                ? 'text-emerald-500'
                : healthy === false
                ? 'text-rose-500 animate-pulse'
                : 'text-slate-400'
            }`}
          />
          <span className="text-slate-600">
            Backend API:{' '}
            <strong className={healthy ? 'text-emerald-600' : 'text-rose-600'}>
              {healthy === true ? 'Healthy' : healthy === false ? 'Disconnected' : 'Checking...'}
            </strong>
          </span>
        </div>
      </div>

      <div className="flex items-center gap-3">
        <a
          href="/docs"
          className="hidden items-center gap-1.5 rounded-lg border border-slate-200 bg-white px-3 py-1.5 text-xs font-semibold text-slate-700 transition-colors hover:bg-slate-50 sm:flex"
          title="Read the AdControl documentation"
        >
          <BookOpen className="h-3.5 w-3.5" aria-hidden="true" />
          <span>Docs</span>
        </a>

        <a
          href="/"
          className="hidden items-center gap-1.5 rounded-lg border border-teal-200 bg-teal-50 px-3 py-1.5 text-xs font-semibold text-teal-800 transition-colors hover:bg-teal-100 sm:flex"
          title="Open AdControl Learning"
        >
          <GraduationCap className="h-3.5 w-3.5" aria-hidden="true" />
          <span>Learning</span>
        </a>

        <button
          onClick={onOpenSandbox}
          className="flex items-center gap-1.5 text-xs font-semibold text-blue-600 bg-blue-50 hover:bg-blue-100 border border-blue-200 px-3 py-1.5 rounded-lg transition-colors"
          title="Open Live Host Website Simulator & Integration Sandbox"
        >
          <Globe className="w-3.5 h-3.5" />
          <span>Interactive Sandbox</span>
        </button>

        {user && (
          <div className="flex items-center gap-3 pl-3 border-l border-slate-200">
            <div className="text-right hidden md:block">
              <div className="text-xs font-medium text-slate-900">{user.email}</div>
              <div className="text-[10px] text-slate-400 uppercase tracking-wider">{user.role}</div>
            </div>
            <button
              onClick={onLogout}
              className="p-2 text-slate-400 hover:text-rose-600 hover:bg-rose-50 rounded-lg transition-colors"
              title="Sign Out"
              aria-label="Sign Out"
            >
              <LogOut className="w-4 h-4" />
            </button>
          </div>
        )}
      </div>
    </header>
  );
};
