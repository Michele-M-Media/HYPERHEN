import { useEffect, useMemo } from "react";
import { Send } from "lucide-react";

import { Button } from "../../components";
import { formatBytes } from "../../lib/format";
import { useTr } from "../../state/lang";
import {
  actionsFor,
  adoptItem,
  useRemoteSends,
  visibleRemoteSends,
  type RemoteSendAction,
  type ShownSend,
} from "../../state/remoteSends";
import { profileNameForAddr, useRosterStore } from "../../state/roster";
import { useUploadQueueStore } from "../../state/uploadQueue";

/** Tasks: console-to-console sends other devices started (#444), polled while this is open,
 *  with Watch / Take over / Resume here. Nothing shows when there are none. */
export function RemoteSends() {
  const tr = useTr();
  const profiles = useRosterStore((s) => s.profiles);
  const byConsole = useRemoteSends((s) => s.byConsole);
  // A row this app gave up because another device took the send over stays listed there, so
  // it can be taken back from here.
  const txIds = useUploadQueueStore((s) =>
    s.items
      .filter((i) => i.errorReason !== "ava1_taken_over")
      .map((i) => i.txIdHex)
      .join(","),
  );

  useEffect(
    () =>
      useRemoteSends.getState().watch(() => useRosterStore.getState().profiles.map((p) => p.host)),
    [],
  );

  const sends = useMemo(
    () => visibleRemoteSends(byConsole, new Set(txIds.split(",").filter(Boolean))),
    [byConsole, txIds],
  );
  if (sends.length === 0) return null;

  const nameOf = (h: string) => profileNameForAddr(h, profiles) || h;
  const adopt = (s: ShownSend, action: RemoteSendAction) => {
    const q = useUploadQueueStore.getState();
    // The old "taken over" row of this same send gives way to the one that takes it back.
    for (const it of q.items) {
      if (it.txIdHex === s.txId && it.status !== "running") q.remove(it.id);
    }
    const id = q.add(adoptItem(s, action, profiles.map((p) => p.host)));
    void q.startHost(s.console, { onlyIds: [id] });
  };

  return (
    <section className="mb-6" data-testid="remote-sends">
      <header className="mb-3 flex items-center gap-2 text-xs font-semibold uppercase tracking-wide text-[var(--color-muted)]">
        <Send size={14} aria-hidden />
        {tr("remote_sends_title", undefined, "Sends from other devices")}
        <span className="text-xs">· {sends.length}</span>
      </header>
      <ul className="space-y-2">
        {sends.map((s) => (
          <RemoteSendRow
            key={s.txId}
            send={s}
            fromName={nameOf(s.from)}
            toName={nameOf(s.console)}
            onAction={(a) => adopt(s, a)}
          />
        ))}
      </ul>
    </section>
  );
}

export function RemoteSendRow({
  send: s,
  fromName,
  toName,
  onAction,
}: {
  send: ShownSend;
  fromName: string;
  toName: string;
  onAction: (a: RemoteSendAction) => void;
}) {
  const tr = useTr();
  const name = s.ownerName || tr("remote_sends_another_device", undefined, "another device");
  const status =
    s.cls === "running_elsewhere"
      ? tr("remote_sends_running", { name }, `Running on ${name}`)
      : s.cls === "paused"
        ? tr("remote_sends_paused", { name }, `Paused on ${name}`)
        : tr("remote_sends_stopped", { name }, `Stopped on ${name}`);
  const actions = actionsFor(s);
  const leaf = s.src.replace(/\/+$/, "").split("/").pop() || s.src;
  return (
    <li
      className="rounded-[var(--radius-card)] border border-[var(--glass-edge)] bg-[var(--color-surface-raised)] px-5 py-4 text-xs shadow-[var(--edge-highlight),var(--shadow-1)]"
      data-testid="remote-send"
    >
      <div className="flex flex-wrap items-center gap-2">
        <span className="min-w-0 flex-1 truncate text-sm font-semibold" title={s.src}>
          {leaf}
        </span>
        {actions.map((a) => (
          <Button
            key={a}
            variant={a === "watch" ? "secondary" : "primary"}
            size="sm"
            onClick={() => onAction(a)}
            data-testid={`remote-send-${a}`}
          >
            {a === "watch"
              ? tr("remote_sends_watch", undefined, "Watch")
              : a === "take"
                ? tr("remote_sends_take_over", undefined, "Take over")
                : tr("remote_sends_resume", undefined, "Resume here")}
          </Button>
        ))}
      </div>
      <div className="mt-1 truncate font-mono text-[var(--color-muted)]" title={`${s.src} → ${s.dest}`}>
        {fromName} → {toName} · {s.dest}
      </div>
      <div className="mt-1 flex flex-wrap gap-x-3 gap-y-0.5 text-[var(--color-muted)]">
        <span>{status}</span>
        {s.bytesTotal > 0 && (
          <span className="font-mono">
            {formatBytes(s.bytesDone)} / {formatBytes(s.bytesTotal)}
          </span>
        )}
        {s.liveRoute && (
          <span>
            {s.liveRoute === "direct"
              ? tr("remote_sends_route_direct", undefined, "Direct")
              : tr("remote_sends_route_relay", { name }, `Through ${name}`)}
          </span>
        )}
      </div>
      {s.cls === "interrupted" && !s.confirmed && (
        <div className="mt-1 text-[var(--color-warn)]">
          {tr(
            "remote_sends_unconfirmed",
            undefined,
            "It may still be running: the receiving console's helper is older and cannot say.",
          )}
        </div>
      )}
      {s.cls === "running_elsewhere" && !s.canTakeOver && (
        <div className="mt-1 text-[var(--color-muted)]">
          {tr(
            "remote_sends_old_helper",
            { name },
            `Taking over a send while it runs needs the newer helper on both consoles. Pause it on ${name} first, or update the helpers.`,
          )}
        </div>
      )}
      {s.cls === "running_elsewhere" && s.liveRoute === "relay" && (
        <div className="mt-1 text-[var(--color-muted)]">
          {tr(
            "remote_sends_relay_no_watch",
            { name },
            `Its bytes run through ${name}, so it can be taken over but not watched.`,
          )}
        </div>
      )}
    </li>
  );
}
