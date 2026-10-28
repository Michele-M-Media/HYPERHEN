/**
 * The console page's client for the helper's web API.
 *
 * Every route is POST + JSON and needs the per-start token in X-PS5U-Token.
 * The page reads the token once from GET /api/token (same origin; another
 * site's page cannot read that response). A 401 {"error":"token"} means the
 * helper restarted and issued a new token: the page must be reloaded, so
 * onTokenExpired listeners hear about it (the ReloadBanner).
 */

export const TOKEN_HEADER = "X-PS5U-Token";

/** A failed call. status 0 = the request never got an answer. */
export class ApiError extends Error {
  readonly status: number;
  readonly code: string;
  constructor(status: number, code: string) {
    super(`${status} ${code}`);
    this.name = "ApiError";
    this.status = status;
    this.code = code;
  }
}

/** The helper is not answering (network failure) or is shutting down (503 stopping). */
export function isOffline(err: unknown): boolean {
  return err instanceof ApiError && (err.status === 0 || err.code === "stopping");
}

const expiredListeners = new Set<() => void>();

/** Called on every 401 from the API. Returns the unsubscribe function. */
export function onTokenExpired(fn: () => void): () => void {
  expiredListeners.add(fn);
  return () => {
    expiredListeners.delete(fn);
  };
}

let tokenPromise: Promise<string> | null = null;

/** A token read that has not answered by then counts as "nothing answered" (the offline screen). */
export const TOKEN_TIMEOUT_MS = 4000;

async function fetchToken(): Promise<string> {
  let res: Response;
  const stop = typeof AbortController === "function" ? new AbortController() : null;
  const timer = setTimeout(() => stop?.abort(), TOKEN_TIMEOUT_MS);
  try {
    res = await fetch("/api/token", { method: "GET", credentials: "same-origin", cache: "no-store", signal: stop?.signal });
  } catch {
    throw new ApiError(0, "offline");
  } finally {
    clearTimeout(timer);
  }
  if (!res.ok) throw new ApiError(res.status, res.status === 503 ? "stopping" : "token_unavailable");
  // Anything but JSON (another server on the port, a captive page) is not the helper.
  const body = (await res.json().catch(() => {
    throw new ApiError(0, "offline");
  })) as { token?: unknown } | null;
  if (!body || typeof body !== "object") throw new ApiError(0, "offline");
  if (typeof body.token !== "string" || !body.token) throw new ApiError(res.status, "token_unavailable");
  return body.token;
}

function token(): Promise<string> {
  if (!tokenPromise) {
    tokenPromise = fetchToken();
    // A failed read is not cached: the next call asks again.
    tokenPromise.catch(() => {
      tokenPromise = null;
    });
  }
  return tokenPromise;
}

/**
 * Is the helper there? Reads the token (kept for the calls that follow). False only when nothing
 * answered like the helper (the page came from the browser's offline copy: the service worker or
 * the appcache) or it is shutting down; any other answer means it is running.
 */
export async function probeHelper(): Promise<boolean> {
  try {
    await token();
    return true;
  } catch (e) {
    return !isOffline(e);
  }
}

/** The ApiError for a reply's status and its {"error":..} body (`http_<status>` without one). */
function errorOf(status: number, json: unknown): ApiError {
  const code =
    json && typeof (json as { error?: unknown }).error === "string" ? (json as { error: string }).error : `http_${status}`;
  return new ApiError(status, code);
}

function tokenExpired(): ApiError {
  for (const fn of Array.from(expiredListeners)) fn();
  return new ApiError(401, "token");
}

/** POST `body` (JSON, or raw bytes for a streamed route) to `route`; the Response when it is 2xx, else the ApiError. */
async function post(route: string, body?: object, signal?: AbortSignal, keepalive?: boolean, raw?: Blob): Promise<Response> {
  const tok = await token();
  let res: Response;
  try {
    res = await fetch(route, {
      method: "POST",
      credentials: "same-origin",
      cache: "no-store",
      headers: { "Content-Type": raw ? "application/octet-stream" : "application/json", [TOKEN_HEADER]: tok },
      body: raw ?? JSON.stringify(body ?? {}),
      signal,
      ...(keepalive ? { keepalive: true } : {}),
    });
  } catch {
    throw new ApiError(0, "offline");
  }
  if (res.status === 401) throw tokenExpired();
  if (!res.ok) throw errorOf(res.status, await res.json().catch(() => null));
  return res;
}

/** POST `body` as JSON to `route` and return the parsed JSON reply. */
export async function call<T>(route: string, body?: object): Promise<T> {
  const res = await post(route, body);
  return (await res.json().catch(() => null)) as T;
}

/**
 * POST with `keepalive`: the request is sent even if the page is being hidden or
 * left right after (the input log's last batch). Bodies stay small (≤ 64 KiB is the
 * browser's keepalive limit; the input log sends ≤ 4 KiB).
 */
export async function callKeepalive(route: string, body?: object): Promise<void> {
  await post(route, body, undefined, true);
}

/** POST `body` as JSON to `route` and return the raw reply (/api/fs/read's bytes). `signal` aborts it. */
export async function callBlob(route: string, body?: object, signal?: AbortSignal): Promise<Blob> {
  const res = await post(route, body, signal);
  return res.blob();
}

/** POST raw bytes to a streamed route (/api/fs/save-text) and return the parsed JSON reply. */
export async function callRaw<T>(route: string, body: Blob): Promise<T> {
  const res = await post(route, undefined, undefined, false, body);
  return (await res.json().catch(() => null)) as T;
}

/**
 * POST `body` with XMLHttpRequest (fetch reports no upload progress): `onProgress(sent, total)` as the
 * bytes leave, `signal` aborts it (ApiError 0 "aborted"). The parsed JSON reply.
 */
export async function sendWithProgress<T>(
  route: string,
  body: Blob,
  onProgress?: (sent: number, total: number) => void,
  signal?: AbortSignal,
): Promise<T> {
  const tok = await token();
  return new Promise<T>((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    const onAbort = () => xhr.abort();
    const done = () => signal?.removeEventListener("abort", onAbort);
    xhr.open("POST", route);
    xhr.setRequestHeader(TOKEN_HEADER, tok);
    xhr.setRequestHeader("Content-Type", "application/octet-stream");
    xhr.upload.onprogress = (e) => onProgress?.(e.loaded, e.lengthComputable ? e.total : body.size);
    xhr.onload = () => {
      done();
      let json: unknown;
      try {
        json = JSON.parse(xhr.responseText);
      } catch {
        json = null;
      }
      if (xhr.status === 401) reject(tokenExpired());
      else if (xhr.status < 200 || xhr.status >= 300) reject(errorOf(xhr.status, json));
      else resolve(json as T);
    };
    xhr.onerror = () => {
      done();
      reject(new ApiError(0, "offline"));
    };
    xhr.onabort = () => {
      done();
      reject(new ApiError(0, "aborted"));
    };
    if (signal?.aborted) {
      reject(new ApiError(0, "aborted"));
      return;
    }
    signal?.addEventListener("abort", onAbort);
    xhr.send(body);
  });
}
