import { useEffect, useState } from "react";
import { Gamepad2 } from "lucide-react";
import { t } from "../../strings";
import { readUpTo } from "../Viewer";
import { GAME_ICON_MAX, readGameFolder, type GameFolder } from "./gameFolder";
import { gameFacts } from "./gameInfo";
import "./viewer.css";

/** The banner itself (props only: tests render it). */
export function GameBannerView({ game, icon }: { game: GameFolder; icon: string | null }) {
  const { info } = game;
  return (
    <div className="cw-gb" role="note" data-game-banner={info.titleId ?? ""}>
      {icon ? <img className="cw-gb-icon" src={icon} alt="" /> : <Gamepad2 className="cw-gb-icon is-none" aria-hidden />}
      <div className="cw-gb-text">
        <span className="cw-dim cw-gb-kind">{t(game.source === "param.sfo" ? "cw_files_game_ps4" : "cw_files_game_ps5")}</span>
        <strong className="cw-gb-title">{info.title ?? info.titleId ?? ""}</strong>
        <span className="cw-dim cw-gb-facts">{gameFacts(info)}</span>
        {info.contentId ? <span className="cw-dim cw-mono cw-gb-facts">{info.contentId}</span> : null}
      </div>
    </div>
  );
}

/**
 * Shown above a folder's list when it holds sce_sys (param.json or param.sfo):
 * the game's title, id and icon. Nothing shows while reading or when neither
 * file names a game; a failed read shows nothing either (the list is the point).
 */
export function GameBanner({ folder, sceSys }: { folder: string; sceSys: string }) {
  const [game, setGame] = useState<GameFolder | null>(null);
  const [icon, setIcon] = useState<string | null>(null);
  useEffect(() => {
    let live = true;
    let url: string | null = null;
    const stop = new AbortController();
    setGame(null);
    setIcon(null);
    readGameFolder(folder, sceSys, stop.signal).then(
      (g) => {
        if (!live || !g) return;
        setGame(g);
        readUpTo(g.iconPath, GAME_ICON_MAX, stop.signal).then(
          (blob) => {
            if (!live || !blob.size || blob.size >= GAME_ICON_MAX) return;
            url = URL.createObjectURL(new Blob([blob], { type: "image/png" }));
            setIcon(url);
          },
          () => undefined,
        );
      },
      () => undefined,
    );
    return () => {
      live = false;
      stop.abort();
      if (url) URL.revokeObjectURL(url);
    };
  }, [folder, sceSys]);
  return game ? <GameBannerView game={game} icon={icon} /> : null;
}
