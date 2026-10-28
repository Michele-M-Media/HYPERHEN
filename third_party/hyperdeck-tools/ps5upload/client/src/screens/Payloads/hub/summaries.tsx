import { useEffect, useRef, useState, type ReactNode } from "react";

import { pmOverview } from "../../../api/payloadManager";
import { smpStatus } from "../../../api/ps5";
import { mgmtAddr } from "../../../lib/addr";
import { useTr } from "../../../state/lang";
import type { IntegrationReport } from "./registry";

/** Payload Manager's card line: its version and how many payloads it keeps. Read once per
 *  console; its own panel (Manage) has the live view. */
export function PmSummary({
  host,
  onReport,
  fallback,
}: {
  host: string;
  onReport: (r: IntegrationReport) => void;
  fallback: ReactNode;
}) {
  const tr = useTr();
  const [line, setLine] = useState<string | null>(null);
  const reportRef = useRef(onReport);
  useEffect(() => {
    reportRef.current = onReport;
  });
  useEffect(() => {
    let cancelled = false;
    setLine(null);
    if (!host.trim()) return;
    pmOverview(host)
      .then((v) => {
        if (cancelled || !v.present) return;
        reportRef.current({ version: v.version || null });
        setLine(
          tr("hub_pm_summary", { n: v.payloads.length }, `Payloads stored: ${v.payloads.length}`),
        );
      })
      .catch(() => undefined);
    return () => {
      cancelled = true;
    };
    // tr is stable per language; re-reading on a language switch is not needed.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [host]);
  return <>{line ?? fallback}</>;
}

/** ShadowMount+'s card line: how many game images it has mounted. */
export function SmpSummary({ host, fallback }: { host: string; fallback: ReactNode }) {
  const tr = useTr();
  const [mounted, setMounted] = useState<number | null>(null);
  useEffect(() => {
    let cancelled = false;
    setMounted(null);
    if (!host.trim()) return;
    smpStatus(mgmtAddr(host.trim()))
      .then((s) => {
        if (!cancelled) setMounted(s.mounted_images.length);
      })
      .catch(() => undefined);
    return () => {
      cancelled = true;
    };
  }, [host]);
  if (mounted === null) return <>{fallback}</>;
  return <>{tr("hub_smp_summary", { n: mounted }, `Game images mounted: ${mounted}`)}</>;
}
