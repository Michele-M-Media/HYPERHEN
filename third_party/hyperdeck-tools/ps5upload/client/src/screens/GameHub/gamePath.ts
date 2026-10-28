import type { InstalledTitle, LibraryEntry } from "../../api/ps5";

const IMAGE_EXT = /\.(exfat|ffpkg|ffpfs|ffpfsc|ffpkgc)$/i;

function base(path: string): string {
  return path.replace(/\/+$/, "").split("/").pop() ?? "";
}

/** The path the game page shows for a title: where the console actually runs it from (#452).
 *
 *  A registered title's `source` is what the console launches. For an image-backed title that
 *  is a mount point (ShadowMount+ mounts `<image stem>_<hash>` under /mnt/shadowmnt/, ps5upload
 *  under /mnt/ps5upload/), so the image file behind it is shown instead when the library scan
 *  found it: by the mount map first, then by the stem ShadowMount+ names its mount after. The
 *  library's own row comes last: the scan keeps one copy per title, and that can be a stale
 *  folder in /data/homebrew while the game runs from an image on an external drive. */
export function shownGamePath(
  libraryPath: string | undefined,
  app: Pick<InstalledTitle, "source" | "imageBacked"> | undefined,
  entries: LibraryEntry[] | null | undefined,
  mountMap?: Map<string, string>,
): string {
  const source = app?.source?.replace(/\/+$/, "") ?? "";
  if (!source) return libraryPath ?? "";
  if (!app?.imageBacked) return source;
  for (const [image, mount] of mountMap ?? []) {
    if (mount.replace(/\/+$/, "") === source) return image;
  }
  const stem = base(source).replace(/_[0-9a-f]{6,}$/i, "");
  const image = (entries ?? []).find(
    (e) => e.kind === "image" && base(e.path).replace(IMAGE_EXT, "") === stem,
  );
  return image?.path ?? source;
}
