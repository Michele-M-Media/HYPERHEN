/**
 * Arrow-key movement for a hand-built `role="menu"` popover whose rows are
 * richer than components/Menu's label + icon (the sidebar's console picker).
 *
 * Given the pressed key, the index of the focused item (-1 when focus is not
 * on an item) and the item count, returns the index to focus next, or null
 * when the key is not a menu movement key.
 */
export function menuKeyTarget(
  key: string,
  current: number,
  count: number,
): number | null {
  if (count <= 0) return null;
  switch (key) {
    case "ArrowDown":
      return current < 0 ? 0 : (current + 1) % count;
    case "ArrowUp":
      return current < 0 ? count - 1 : (current - 1 + count) % count;
    case "Home":
      return 0;
    case "End":
      return count - 1;
    default:
      return null;
  }
}

/**
 * What a key does inside an open menu: "close" for Escape and Tab (Tab closes
 * rather than walking out of the menu into the page; focus then goes back to
 * whatever opened it), an index to focus for a movement key, or null to leave
 * the key to the focused item (Enter / Space press it).
 */
export function menuKeyAction(
  key: string,
  current: number,
  count: number,
): "close" | number | null {
  if (key === "Escape" || key === "Tab") return "close";
  return menuKeyTarget(key, current, count);
}
