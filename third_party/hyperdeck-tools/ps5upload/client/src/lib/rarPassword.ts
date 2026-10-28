// An archive upload that failed because the password is missing or wrong. The engine's
// tokens: `rar_password_required` / `rar_password_wrong` (and the AVA1 reasons
// `ava1_rar_password_required` / `ava1_rar_password_wrong`); `password_needed` is the generic
// "ask the person" token. Matched as substrings of the reason or the message.

export type RarPasswordProblem = "required" | "wrong";

export function rarPasswordProblem(
  reason: string | null | undefined,
  message?: string | null,
): RarPasswordProblem | null {
  const t = `${reason ?? ""} ${message ?? ""}`.toLowerCase();
  if (t.includes("password_wrong")) return "wrong";
  if (t.includes("password_required") || t.includes("password_needed")) {
    return "required";
  }
  return null;
}
