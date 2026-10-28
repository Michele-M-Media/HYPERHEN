import { useState } from "react";
import { KeyRound } from "lucide-react";

import { Button } from "../../components";
import { useTr } from "../../state/lang";
import type { RarPasswordProblem } from "../../lib/rarPassword";

/** Shown on a failed archive row whose upload stopped for a missing or wrong password: type
 *  it, and the same job resumes with it. The password stays in this component's state until
 *  submitted, then on the in-memory queue item; it is never logged or saved. */
export function RarPasswordPrompt({
  problem,
  onSubmit,
}: {
  problem: RarPasswordProblem;
  onSubmit: (password: string) => void;
}) {
  const tr = useTr();
  const [pw, setPw] = useState("");
  return (
    <form
      className="mt-2 flex flex-wrap items-center gap-2"
      onSubmit={(e) => {
        e.preventDefault();
        if (pw) onSubmit(pw);
      }}
    >
      <KeyRound size={14} className="text-[var(--color-warn)]" aria-hidden />
      <span className="text-xs text-[var(--color-warn)]">
        {problem === "wrong"
          ? tr("queue_rar_password_wrong", undefined, "That password was wrong. Try again.")
          : tr("queue_rar_password_required", undefined, "This archive needs a password.")}
      </span>
      <input
        type="password"
        autoComplete="off"
        value={pw}
        onChange={(e) => setPw(e.target.value)}
        placeholder={tr("upload_rar_password_placeholder", "Password")}
        aria-label={tr("upload_rar_password_placeholder", "Password")}
        className="input w-44"
      />
      <Button type="submit" variant="primary" size="sm" disabled={!pw}>
        {tr("queue_rar_password_retry", undefined, "Retry with password")}
      </Button>
    </form>
  );
}
