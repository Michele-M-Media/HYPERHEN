/**
 * The app's mark: the same square logo the installer and the window use.
 * Decorative wherever it sits beside the name, so hidden from assistive tech.
 */
export function AppLogo({ size = 32, className = "" }: { size?: number; className?: string }) {
  return (
    <img
      src={`${import.meta.env.BASE_URL.replace(/\/+$/, "")}/logo-square.png`}
      alt=""
      width={size}
      height={size}
      className={`shrink-0 rounded-[28%] shadow-sm ${className}`}
      style={{ width: size, height: size }}
    />
  );
}
