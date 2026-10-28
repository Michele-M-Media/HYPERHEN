import { create } from "zustand";

import { safeGetItem, safeSetItem } from "../lib/safeStorage";

/**
 * How a console-to-console send goes, remembered per pair of consoles (#444): `auto` (straight
 * between them when they can connect, else through this computer), `direct` (never through
 * this computer: a phone on mobile data) or `relay` (never direct), and what the sending
 * console dials to reach the receiving one when that is not the receiving console's own
 * address (a local forward on the sending console with ps5-tailscale, a NAT in between).
 */
export type C2cRouteMode = "auto" | "direct" | "relay";

export interface C2cRoute {
  mode: C2cRouteMode;
  /** `host[:port]`, IPv4, as the sending console dials it; "" for the receiver's own address. */
  directAddr: string;
}

const KEY = "ps5upload.c2c.routes.v1";
const MODES: readonly C2cRouteMode[] = ["auto", "direct", "relay"];

export const pairKey = (from: string, to: string) => `${from}>${to}`;

export function parseRoutes(raw: string | null): Record<string, C2cRoute> {
  if (!raw) return {};
  try {
    const v = JSON.parse(raw) as unknown;
    if (!v || typeof v !== "object") return {};
    const out: Record<string, C2cRoute> = {};
    for (const [k, r] of Object.entries(v as Record<string, unknown>)) {
      if (!r || typeof r !== "object") continue;
      const { mode, directAddr } = r as { mode?: unknown; directAddr?: unknown };
      out[k] = {
        mode: MODES.includes(mode as C2cRouteMode) ? (mode as C2cRouteMode) : "auto",
        directAddr: typeof directAddr === "string" ? directAddr.trim() : "",
      };
    }
    return out;
  } catch {
    return {};
  }
}

interface C2cRoutes {
  routes: Record<string, C2cRoute>;
  routeFor: (from: string, to: string) => C2cRoute;
  setRoute: (from: string, to: string, route: C2cRoute) => void;
}

export const useC2cRoutes = create<C2cRoutes>((set, get) => ({
  routes: parseRoutes(safeGetItem(KEY)),
  routeFor: (from, to) => get().routes[pairKey(from, to)] ?? { mode: "auto", directAddr: "" },
  setRoute: (from, to, route) => {
    const routes = { ...get().routes, [pairKey(from, to)]: { ...route, directAddr: route.directAddr.trim() } };
    safeSetItem(KEY, JSON.stringify(routes));
    set({ routes });
  },
}));
