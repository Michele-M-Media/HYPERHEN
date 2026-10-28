import logo from "../../../public/logo-square.png?inline";

/** What /api/status answers (payload/src/webui_api.c r_status). */
export interface ConsoleStatus {
  version: string | null;
  firmware: string | null;
  model: string | null;
  temp_c: number | null;
  running: { title_id: string; app_id: number; name: string | null } | null;
}

const BRAND = "PS5Upload Minion";

/** Logo, name, and the console chip: model · firmware · temperature. */
export function TopBar({ status, chipFallback }: { status: ConsoleStatus | null; chipFallback: string | null }) {
  const parts = status
    ? [status.model, status.firmware, status.temp_c != null ? `${status.temp_c}°C` : null].filter(
        (p): p is string => !!p,
      )
    : [];
  const chip = chipFallback ?? parts.join(" · ");
  return (
    <header className="cw-top">
      <img src={logo} alt="" width={40} height={40} />
      <span className="cw-brand">{BRAND}</span>
      {chip ? <span className={`cw-chip${chipFallback ? " is-off" : ""}`}>{chip}</span> : null}
    </header>
  );
}
