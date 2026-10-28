// The one app-wide package viewer (see state/packageViewer.ts), on the console of the moment.

import { useConnectionStore } from "../state/connection";
import { usePackageViewer } from "../state/packageViewer";
import { PackagePanel } from "./PackagePanel";

export function GlobalPackageViewer() {
  const request = usePackageViewer((s) => s.request);
  const close = usePackageViewer((s) => s.close);
  const host = useConnectionStore((s) => (s.payloadStatus === "up" ? s.host : null));
  return (
    <PackagePanel
      path={request?.path ?? null}
      host={host || null}
      onClose={close}
      actions={request?.actions ?? []}
    />
  );
}
