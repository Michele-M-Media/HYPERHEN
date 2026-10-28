/** Is this destination the "exfat" folder where disk images are kept?
 *
 *  Picking it for a game folder reads like "make it an .exfat", but the
 *  destination only says where the upload goes: the folder arrives as a
 *  folder. The Upload screen warns when the two meet. */
export function isImageFolder(subpath: string): boolean {
  const last = subpath.replace(/^\/+|\/+$/g, "").split("/").pop() ?? "";
  return last.toLowerCase() === "exfat";
}
