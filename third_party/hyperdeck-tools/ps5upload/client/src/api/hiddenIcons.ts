// Home-screen icons hidden on the console (#407): the engine's /api/ps5/apps/hidden, which asks
// the helper to read or flip app.db's `visible` flag. Plain fetches against the engine origin,
// so the desktop app, the browser build and the Docker build take the same path.

import { getEngineUrl } from "../state/engine";
import { consoleAddr } from "../lib/addr";

interface HiddenWire {
  ok?: boolean;
  hidden?: string[];
  error?: string | null;
}

async function read(res: Response): Promise<HiddenWire> {
  const body = (await res.json().catch(() => ({}))) as HiddenWire;
  if (!res.ok || !body.ok) {
    throw new Error(body.error || `request failed (${res.status})`);
  }
  return body;
}

/** The title ids whose home-screen icon is hidden, read live from the console. */
export async function hiddenIcons(host: string): Promise<string[]> {
  const addr = encodeURIComponent(consoleAddr(host));
  const res = await fetch(`${getEngineUrl()}/api/ps5/apps/hidden?addr=${addr}`, {
    signal: AbortSignal.timeout(15_000),
  });
  return (await read(res)).hidden ?? [];
}

/** Hide (`true`) or show a title's home-screen icon. Throws with the console's reason when it
 *  refuses (a system app, a title with no icon, app.db busy). */
export async function setIconHidden(host: string, titleId: string, hidden: boolean): Promise<void> {
  const res = await fetch(`${getEngineUrl()}/api/ps5/apps/hidden`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ addr: consoleAddr(host), title_id: titleId, hidden }),
    signal: AbortSignal.timeout(20_000),
  });
  await read(res);
}
