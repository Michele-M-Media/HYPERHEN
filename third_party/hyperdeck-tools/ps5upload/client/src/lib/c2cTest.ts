import type { ConnectionTest, ReachResult } from "../api/ava1";

/** One line of "Test connection" (#444), ready for `tr(key, vars, fallback)`. */
export interface TestLine {
  key: string;
  vars: Record<string, string | number>;
  fallback: string;
  tone: "good" | "bad" | "warn";
}

const line = (
  tone: TestLine["tone"],
  key: string,
  vars: Record<string, string | number>,
  fallback: string,
): TestLine => ({ tone, key, vars, fallback });

/** The direct leg: `from` dials `to` the way a send would. */
function directLine(r: ReachResult, from: string, to: string): TestLine {
  const target = r.target ?? to;
  const ms = r.ms ?? 0;
  const detail = r.detail ?? "";
  switch (r.verdict) {
    case "reachable":
      return line("good", "c2c_test_reachable", { from, to, target, ms }, `${from} reaches ${to} at ${target} (${ms} ms): a direct send can connect.`);
    case "refused":
      return line("bad", "c2c_test_refused", { from, target }, `${from} reaches ${target}, but nothing answers on that port: is the helper running there, or the forward's port right?`);
    case "timeout":
      return line("bad", "c2c_test_timeout", { from, target }, `${from} got no answer from ${target}: a firewall, client isolation, or a forward that is not up.`);
    case "unreachable":
      return line("bad", "c2c_test_unreachable", { from, target }, `${from} has no route to ${target}: the consoles are on networks that cannot reach each other.`);
    case "bad_address":
      return line("bad", "c2c_test_bad_address", { target: r.target ?? detail }, `${r.target ?? detail} is not an address the console can dial.`);
    case "helper_too_old":
      return line("warn", "c2c_test_helper_old", { from }, `${from}'s helper is too old to run this test. Update the helper on ${from}.`);
    default:
      return line("warn", "c2c_test_error", { from, detail }, `The test could not run on ${from}: ${detail}`);
  }
}

/** What the test found, one line per leg: direct, then (Auto, relay) this device to each
 *  console, then the optional 4 KiB send. */
export function testLines(t: ConnectionTest, from: string, to: string): TestLine[] {
  const out: TestLine[] = [];
  if (t.direct) out.push(directLine(t.direct, from, to));
  if (t.relay) {
    const bad = [
      [t.relay.from, from],
      [t.relay.to, to],
    ].filter(([r]) => (r as ReachResult).verdict !== "ok") as [ReachResult, string][];
    if (bad.length === 0) {
      out.push(line("good", "c2c_test_relay_ok", {}, "This device reaches both consoles: a send through here works."));
    } else {
      for (const [r, name] of bad) {
        const detail = r.detail ?? r.verdict;
        out.push(line("bad", "c2c_test_relay_bad", { name, detail }, `This device cannot use ${name}: ${detail}`));
      }
    }
  }
  if (t.deep) {
    out.push(
      t.deep.ok
        ? line("good", "c2c_test_deep_ok", { to, ms: t.deep.ms ?? 0 }, `A 4 KiB test send arrived on ${to} (${t.deep.ms ?? 0} ms).`)
        : line("bad", "c2c_test_deep_bad", { detail: t.deep.detail ?? "" }, `The 4 KiB test send did not arrive: ${t.deep.detail ?? ""}`),
    );
  }
  return out;
}
