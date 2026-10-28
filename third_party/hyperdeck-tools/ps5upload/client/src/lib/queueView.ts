// What a console-queue row opens in the package viewer, if anything.

import type { QueueItem } from "../state/uploadQueue";
import { hostOf } from "./addr";

/** The path the viewer reads for this row: an install's package (here, on a server, or on the
 *  console as `ps5://`), or an upload's source when it is a package, image or game folder.
 *  Null for a link install or a plain file. */
export function queueItemViewPath(item: Pick<QueueItem, "sourceKind" | "sourcePath" | "addr" | "install">): string | null {
  const onConsole = (p: string) => `ps5://${hostOf(item.addr)}${p}`;
  const req = item.install;
  if (item.sourceKind === "install" && req) {
    switch (req.via) {
      case "stream":
        return typeof req.source === "string" ? req.source : null;
      case "library":
      case "console-path":
        return onConsole(req.path);
      case "external":
        return onConsole(req.pkg.path);
      default:
        return null;
    }
  }
  switch (item.sourceKind) {
    case "pkg":
    case "image":
    case "folder":
      return item.sourcePath || null;
    default:
      return null;
  }
}
