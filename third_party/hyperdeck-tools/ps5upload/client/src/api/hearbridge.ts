// The console's HearBridge (Bluetooth headset audio; its own web page on :8090..8095)
// through the engine's /api/ps5/hearbridge routes. HearBridge sends no CORS headers, so
// the app never calls it directly; the engine finds its port, holds its token and passes
// only the changes below. Plain fetches against the engine origin, so the desktop app, the
// browser build and the Docker build take the same path.

import { getEngineUrl } from "../state/engine";
import { hostOf } from "../lib/addr";

/** HearBridge's status, read tolerantly: an older or newer HearBridge may leave any field
 *  out, and a missing field is null rather than a guess. */
export interface HbStatus {
  version: string;
  /** "streaming", "connecting", "scanning", "reconnecting", "disconnected", "error", … */
  state: string;
  /** The headset's name, when one is connected or being connected. */
  device: string;
  /** Audio volume boost, 0..500 %. */
  gainPct: number | null;
  muted: boolean | null;
  codec: string;
  latencyTargetMs: number | null;
  latencyEstimateMs: number | null;
  latencyAuto: boolean | null;
  batteryPct: number | null;
  /** Link quality 0..100. */
  linkScore: number | null;
  night: boolean | null;
  /** The running game HearBridge has a profile for, if any. */
  gameName: string;
  gameProfile: boolean;
  events: string[];
}

export interface HbSavedDevice {
  addr: string;
  name: string;
  kind: string;
  current: boolean;
}

export interface HbScannedDevice {
  index: number;
  addr: string;
  name: string;
  kind: string;
  rssi: number | null;
}

export type HbOverview =
  | { present: true; port: number; status: HbStatus }
  | { present: false; error: string };

type Obj = Record<string, unknown>;
const obj = (v: unknown): Obj => (v && typeof v === "object" && !Array.isArray(v) ? (v as Obj) : {});
const str = (v: unknown): string => (typeof v === "string" ? v : "");
const num = (v: unknown): number | null => (typeof v === "number" && Number.isFinite(v) ? v : null);
const flag = (v: unknown): boolean | null =>
  typeof v === "boolean" ? v : typeof v === "number" ? v !== 0 : null;

export function parseHbStatus(raw: unknown): HbStatus {
  const s = obj(raw);
  const latency = obj(s.latency);
  const battery = obj(s.battery);
  const link = obj(s.link);
  const game = obj(s.game);
  const pct = num(battery.pct);
  const score = num(link.score);
  return {
    version: str(s.version),
    state: str(s.state),
    device: str(s.device),
    gainPct: num(s.gain_pct),
    muted: flag(s.muted),
    codec: str(s.codec),
    latencyTargetMs: num(latency.target_ms),
    latencyEstimateMs: num(latency.estimate_ms),
    latencyAuto: flag(obj(s.lat_extra).auto),
    // -1 is HearBridge's "not known".
    batteryPct: pct !== null && pct >= 0 ? pct : null,
    linkScore: score !== null && score >= 0 ? score : null,
    night: flag(obj(s.night).on),
    gameName: str(game.name),
    gameProfile: flag(game.profile) === true,
    events: Array.isArray(s.events) ? s.events.filter((e): e is string => typeof e === "string") : [],
  };
}

export function parseSaved(raw: unknown): HbSavedDevice[] {
  const list = obj(raw).devices;
  if (!Array.isArray(list)) return [];
  return list
    .map(obj)
    .filter((d) => str(d.addr))
    .map((d) => ({ addr: str(d.addr), name: str(d.name), kind: str(d.kind), current: flag(d.current) === true }));
}

export function parseScanned(raw: unknown): HbScannedDevice[] {
  const list = obj(raw).devices;
  if (!Array.isArray(list)) return [];
  return list
    .map(obj)
    .filter((d) => num(d.index) !== null)
    .map((d) => ({
      index: num(d.index) as number,
      addr: str(d.addr),
      name: str(d.name),
      kind: str(d.kind),
      rssi: num(d.rssi),
    }));
}

async function read(res: Response): Promise<Obj> {
  const body = obj(await res.json().catch(() => ({})));
  if (!res.ok) throw new Error(str(body.error) || `request failed (${res.status})`);
  return body;
}

const q = (host: string) => `host=${encodeURIComponent(hostOf(host))}`;

/** Whether HearBridge answers on `host`, on which port, and its status. */
export async function hbOverview(host: string): Promise<HbOverview> {
  const res = await fetch(`${getEngineUrl()}/api/ps5/hearbridge/status?${q(host)}`, {
    signal: AbortSignal.timeout(20_000),
  });
  const w = await read(res);
  if (!w.present) return { present: false, error: str(w.error) || "not running" };
  return { present: true, port: num(w.port) ?? 8090, status: parseHbStatus(w.status) };
}

export async function hbSaved(host: string): Promise<HbSavedDevice[]> {
  const res = await fetch(`${getEngineUrl()}/api/ps5/hearbridge/saved?${q(host)}`, {
    signal: AbortSignal.timeout(20_000),
  });
  return parseSaved(await read(res));
}

export async function hbScanned(host: string): Promise<HbScannedDevice[]> {
  const res = await fetch(`${getEngineUrl()}/api/ps5/hearbridge/devices?${q(host)}`, {
    signal: AbortSignal.timeout(20_000),
  });
  return parseScanned(await read(res));
}

export type HbAction =
  | { action: "volume"; pct: number }
  | { action: "mute"; on: boolean }
  | { action: "night"; on: boolean }
  | { action: "latency"; auto: boolean }
  | { action: "latency"; ms: number }
  | { action: "select"; addr: string }
  | { action: "select"; index: number }
  | { action: "reconnect" }
  | { action: "scan" }
  | { action: "stop" };

/** Make one change. Returns HearBridge's new status when its answer carries one. */
export async function hbAct(host: string, a: HbAction): Promise<HbStatus | null> {
  const res = await fetch(`${getEngineUrl()}/api/ps5/hearbridge/action`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ host: hostOf(host), ...a }),
    signal: AbortSignal.timeout(20_000),
  });
  const w = await read(res);
  const result = obj(w.result);
  return typeof result.version === "string" ? parseHbStatus(result) : null;
}
