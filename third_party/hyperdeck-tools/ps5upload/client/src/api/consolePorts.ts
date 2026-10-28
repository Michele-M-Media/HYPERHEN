// The ports in use on the console (Discord ask: "see what payloads are using what ports, with
// links"): the engine's /api/ps5/net/ports, which asks the helper to bind each port and let go.
// A plain fetch against the engine origin, so the desktop app and the web UI take the same path.

import { getEngineUrl } from "../state/engine";
import { consoleAddr, withPort } from "../lib/addr";

/** Where a held port can be reached from (see payload/include/net_ports.h). */
export type PortScope = "all" | "console" | "lan" | "busy";

export interface ConsolePort {
  port: number;
  proto: "tcp" | "udp";
  scope: PortScope;
  /** What usually listens there, guessed from the number. */
  name?: string | null;
  /** For a well-known port: whether it serves a web page. Unknown ports leave it out. */
  web?: boolean | null;
}

export interface ConsolePorts {
  ports: ConsolePort[];
  elapsed_ms: number;
  truncated?: boolean;
}

export async function consolePorts(host: string): Promise<ConsolePorts> {
  const addr = encodeURIComponent(consoleAddr(host));
  const res = await fetch(`${getEngineUrl()}/api/ps5/net/ports?addr=${addr}`, {
    signal: AbortSignal.timeout(30_000),
  });
  const body = (await res.json().catch(() => ({}))) as Partial<ConsolePorts> & { error?: string };
  if (!res.ok || !Array.isArray(body.ports)) {
    throw new Error(body.error || `request failed (${res.status})`);
  }
  return { ports: body.ports, elapsed_ms: body.elapsed_ms ?? 0, truncated: body.truncated };
}

/** A port bound only to the LAN address is almost always the console's end of an outgoing
 *  connection, not a service: hidden unless asked for. */
export const isOutgoing = (p: ConsolePort): boolean => p.scope === "lan";

/** The browser link for a port a PC can reach (TCP on every address); null otherwise, and for
 *  a known service with no web page (elfldr, the helper: a stray HTTP request is not what they
 *  expect). An unknown port gets a link: the user is the one who can tell what it is. */
export function portLink(host: string, p: ConsolePort): string | null {
  if (p.proto !== "tcp" || p.scope !== "all" || p.web === false) return null;
  const hp = withPort(host, p.port);
  return hp ? `http://${hp}/` : null;
}
