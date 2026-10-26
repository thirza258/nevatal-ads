import React, { useMemo, useRef, useState } from 'react';

export interface TrendPoint {
  label: string;
  value: number;
}

interface TrendChartProps {
  title: string;
  subtitle?: string;
  data: TrendPoint[];
  /** CSS custom property name, e.g. "--series-1". */
  colorVar: string;
  formatValue?: (value: number) => string;
  emptyMessage?: string;
}

const WIDTH = 640;
const HEIGHT = 220;
const MARGIN = { top: 26, right: 54, bottom: 32, left: 46 };

function niceMax(value: number): number {
  if (!Number.isFinite(value) || value <= 0) return 1;
  const exponent = Math.floor(Math.log10(value));
  const fraction = value / 10 ** exponent;
  const nice = fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 5 ? 5 : 10;
  return nice * 10 ** exponent;
}

function compactNumber(value: number): string {
  if (Math.abs(value) >= 1_000_000) return `${(value / 1_000_000).toFixed(value % 1_000_000 === 0 ? 0 : 1)}M`;
  if (Math.abs(value) >= 1_000) return `${(value / 1_000).toFixed(value % 1_000 === 0 ? 0 : 1)}K`;
  return `${Math.round(value)}`;
}

export const TrendChart: React.FC<TrendChartProps> = ({
  title,
  subtitle,
  data,
  colorVar,
  formatValue = compactNumber,
  emptyMessage = 'No events in this date range.',
}) => {
  const [hover, setHover] = useState<number | null>(null);
  const containerRef = useRef<HTMLDivElement>(null);

  const points = data.length > 0 ? data : [{ label: '—', value: 0 }];
  const n = points.length;
  const plotWidth = WIDTH - MARGIN.left - MARGIN.right;
  const plotHeight = HEIGHT - MARGIN.top - MARGIN.bottom;
  const maxValue = useMemo(() => {
    const rawMax = Math.max(...points.map((p) => p.value), 0);
    return niceMax(rawMax);
  }, [points]);

  const xFor = (index: number) =>
    MARGIN.left + (n === 1 ? plotWidth / 2 : (index / (n - 1)) * plotWidth);
  const yFor = (value: number) =>
    MARGIN.top + plotHeight - (Math.max(0, value) / maxValue) * plotHeight;

  const linePath = points
    .map((point, index) => `${index === 0 ? 'M' : 'L'}${xFor(index).toFixed(2)},${yFor(point.value).toFixed(2)}`)
    .join(' ');
  const baseline = MARGIN.top + plotHeight;
  const areaPath = `${linePath} L${xFor(n - 1).toFixed(2)},${baseline} L${xFor(0).toFixed(2)},${baseline} Z`;

  const yTicks = [0, 1, 2, 3, 4].map((step) => (maxValue / 4) * step);
  const tickCount = Math.min(5, n);
  const xTickIndices = new Set<number>();
  for (let i = 0; i < tickCount; i += 1) {
    xTickIndices.add(tickCount === 1 ? 0 : Math.round((i / (tickCount - 1)) * (n - 1)));
  }

  const last = points[n - 1];
  const lastX = xFor(n - 1);
  const lastY = yFor(last.value);
  const showLastLabel = n > 1 || last.value > 0;
  const labelAnchor = lastX > WIDTH - MARGIN.right - 24 ? 'end' : 'start';
  const labelX = labelAnchor === 'end' ? lastX - 8 : lastX + 8;

  const hoverPoint = hover !== null ? points[hover] : null;

  const handlePointer = (event: React.PointerEvent<SVGSVGElement>) => {
    if (!containerRef.current) return;
    const rect = event.currentTarget.getBoundingClientRect();
    const svgX = ((event.clientX - rect.left) / rect.width) * WIDTH;
    const ratio = (svgX - MARGIN.left) / plotWidth;
    const index = Math.round(ratio * (n - 1));
    setHover(Math.max(0, Math.min(n - 1, index)));
  };

  const handleKey = (event: React.KeyboardEvent<HTMLDivElement>) => {
    if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return;
    event.preventDefault();
    setHover((current) => {
      if (current === null) return n - 1;
      const next = event.key === 'ArrowRight' ? current + 1 : current - 1;
      return Math.max(0, Math.min(n - 1, next));
    });
  };

  const style = {
    '--chart-series': `var(${colorVar})`,
    '--chart-grid': 'var(--chart-grid)',
    '--chart-axis': 'var(--chart-axis)',
    '--chart-muted': 'var(--chart-muted)',
    '--chart-surface': 'var(--chart-surface)',
  } as React.CSSProperties;

  return (
    <div className="bg-white border border-slate-200 rounded-xl p-4 shadow-sm flex flex-col gap-2">
      <div>
        <h3 className="text-sm font-semibold text-slate-900">{title}</h3>
        {subtitle && <p className="text-xs text-slate-500 mt-0.5">{subtitle}</p>}
      </div>

      <div
        ref={containerRef}
        className="relative outline-none focus-visible:ring-2 focus-visible:ring-blue-500 rounded-lg"
        style={style}
        tabIndex={0}
        onKeyDown={handleKey}
        onBlur={() => setHover(null)}
        role="group"
        aria-label={`${title} trend chart. Use left and right arrow keys to inspect values.`}
      >
        {hoverPoint && (
          <div
            className="pointer-events-none absolute top-0 z-10 -translate-x-1/2 rounded-lg border border-slate-200 bg-white px-3 py-2 shadow-md min-w-[120px]"
            style={{ left: `${(xFor(hover as number) / WIDTH) * 100}%` }}
            role="status"
          >
            <div className="text-[11px] text-slate-500">{hoverPoint.label}</div>
            <div className="flex items-center gap-1.5 mt-0.5">
              <span className="inline-block w-3 h-0.5 rounded" style={{ background: `var(${colorVar})` }} />
              <span className="text-sm font-semibold text-slate-900 tabular-nums">
                {formatValue(hoverPoint.value)}
              </span>
            </div>
          </div>
        )}

        <svg
          viewBox={`0 0 ${WIDTH} ${HEIGHT}`}
          className="w-full h-auto touch-none"
          onPointerMove={handlePointer}
          onPointerDown={handlePointer}
          onPointerLeave={() => setHover(null)}
          aria-hidden="true"
        >
          {yTicks.map((tick) => {
            const y = yFor(tick);
            return (
              <g key={tick}>
                <line
                  x1={MARGIN.left}
                  x2={WIDTH - MARGIN.right}
                  y1={y}
                  y2={y}
                  stroke="var(--chart-grid)"
                  strokeWidth="1"
                />
                <text
                  x={MARGIN.left - 8}
                  y={y + 4}
                  textAnchor="end"
                  fontSize="10"
                  fill="var(--chart-muted)"
                  className="tabular-nums"
                >
                  {formatValue(tick)}
                </text>
              </g>
            );
          })}

          <line
            x1={MARGIN.left}
            x2={WIDTH - MARGIN.right}
            y1={baseline}
            y2={baseline}
            stroke="var(--chart-axis)"
            strokeWidth="1"
          />

          {xTickIndices.size > 0 &&
            Array.from(xTickIndices).map((index) => (
              <text
                key={index}
                x={xFor(index)}
                y={HEIGHT - 10}
                textAnchor={index === 0 ? 'start' : index === n - 1 ? 'end' : 'middle'}
                fontSize="10"
                fill="var(--chart-muted)"
                className="tabular-nums"
              >
                {points[index].label.length > 10
                  ? points[index].label.slice(5, 10)
                  : points[index].label}
              </text>
            ))}

          <path d={areaPath} fill="var(--chart-series)" opacity="0.1" />
          <path
            d={linePath}
            fill="none"
            stroke="var(--chart-series)"
            strokeWidth="2"
            strokeLinejoin="round"
            strokeLinecap="round"
          />

          {showLastLabel && (
            <>
              <circle cx={lastX} cy={lastY} r="4" fill="var(--chart-series)" stroke="var(--chart-surface)" strokeWidth="2" />
              <text
                x={labelX}
                y={Math.max(MARGIN.top - 6, lastY - 8)}
                textAnchor={labelAnchor}
                fontSize="11"
                fontWeight="600"
                fill="var(--text-primary)"
                className="tabular-nums"
              >
                {formatValue(last.value)}
              </text>
            </>
          )}

          {hover !== null && hoverPoint && (
            <g>
              <line
                x1={xFor(hover)}
                x2={xFor(hover)}
                y1={MARGIN.top}
                y2={baseline}
                stroke="var(--chart-axis)"
                strokeWidth="1"
              />
              <circle
                cx={xFor(hover)}
                cy={yFor(hoverPoint.value)}
                r="4"
                fill="var(--chart-series)"
                stroke="var(--chart-surface)"
                strokeWidth="2"
              />
            </g>
          )}
        </svg>
      </div>

      {data.length === 0 && (
        <p className="text-xs text-slate-500 text-center py-1">{emptyMessage}</p>
      )}
    </div>
  );
};
