import { beforeEach, describe, expect, it, vi } from "vitest";

vi.mock("../api", async (importOriginal) => {
  const real = await importOriginal<typeof import("../api")>();
  return { ...real, call: vi.fn(), callRaw: vi.fn(), sendWithProgress: vi.fn() };
});

import { ApiError, call, callRaw, sendWithProgress } from "../api";
import {
  createFile,
  decodeEditable,
  loadOnOtherDevice,
  downloadItem,
  encodeText,
  onOtherDevice,
  saveBody,
  saveText,
  TEXT_SAVE_MAX,
  uploadFile,
  uploadFolders,
  uploadItems,
} from "./transfer";

const mCall = vi.mocked(call);
const mRaw = vi.mocked(callRaw);
const mSend = vi.mocked(sendWithProgress);

beforeEach(() => {
  mCall.mockReset();
  mRaw.mockReset();
  mSend.mockReset();
});

async function blobText(b: Blob): Promise<string> {
  return new TextDecoder().decode(await new Response(b).arrayBuffer());
}

describe("where the page is open", () => {
  it("offers upload/download only away from the console", () => {
    expect(onOtherDevice("192.168.1.99", "192.168.1.20")).toBe(true);
    expect(onOtherDevice("", "192.168.1.20")).toBe(true); // LAN access off: the address is ""
    expect(onOtherDevice("192.168.1.99", "127.0.0.1")).toBe(false);
    expect(onOtherDevice("192.168.1.99", "localhost")).toBe(false);
    expect(onOtherDevice("192.168.1.99", "")).toBe(false);
    // the console's own browser opened by its LAN address
    expect(onOtherDevice("192.168.1.99", "192.168.1.99")).toBe(false);
    // not known yet: nothing offered
    expect(onOtherDevice(null, "192.168.1.20")).toBe(false);
  });
  it("reads the console's address from settings; a failed read offers nothing", async () => {
    mCall.mockResolvedValueOnce({ addr: "192.168.1.99" });
    expect(await loadOnOtherDevice()).toBe(false); // the test runner's host is not another device
    expect(mCall).toHaveBeenCalledWith("/api/settings");
    mCall.mockRejectedValueOnce(new ApiError(0, "offline"));
    expect(await loadOnOtherDevice()).toBe(false);
  });
});

describe("text save", () => {
  it("keeps CRLF files CRLF and refuses NUL or more than 1 MiB", () => {
    expect(new TextDecoder().decode(encodeText("a\nb\n", true) as Uint8Array)).toBe("a\r\nb\r\n");
    expect(new TextDecoder().decode(encodeText("a\nb", false) as Uint8Array)).toBe("a\nb");
    expect(encodeText("a\u0000b", false)).toBe("binary");
    expect(encodeText("x".repeat(TEXT_SAVE_MAX + 1), false)).toBe("too_large");
    expect((encodeText("é".repeat(TEXT_SAVE_MAX / 2), false) as Uint8Array).length).toBe(TEXT_SAVE_MAX);
  });
  it("opens only valid UTF-8 without NUL, keeping a BOM", () => {
    const enc = new TextEncoder();
    expect(decodeEditable(enc.encode("hi\n").buffer as ArrayBuffer)).toBe("hi\n");
    expect(decodeEditable(new Uint8Array([0xef, 0xbb, 0xbf, 0x41]).buffer)).toBe("﻿A");
    expect(decodeEditable(new Uint8Array([0x41, 0xff, 0x42]).buffer)).toBeNull();
    expect(decodeEditable(new Uint8Array([0x41, 0x00]).buffer)).toBeNull();
  });
  it("sends one JSON line, then the bytes, with the stamp the editor read", async () => {
    mRaw.mockResolvedValue({ mtime: 5, size: 3 });
    const r = await saveText("/data/a.txt", new TextEncoder().encode("new"), { mtime: 1, size: 9 });
    expect(r).toEqual({ mtime: 5, size: 3 });
    const [route, body] = mRaw.mock.calls[0];
    expect(route).toBe("/api/fs/save-text");
    expect(await blobText(body)).toBe('{"path":"/data/a.txt","mtime":1,"size":9}\nnew');
    await saveText("/data/a.txt", new Uint8Array(0), { mtime: 1, size: 9, mtime_ns: 77 });
    expect(await blobText(mRaw.mock.calls[1][1])).toBe('{"path":"/data/a.txt","mtime":1,"size":9,"mtime_ns":77}\n');
  });
  it("creates an empty file with create", async () => {
    mRaw.mockResolvedValue({ mtime: 5, size: 0 });
    await createFile("/data/new.cfg");
    expect(await blobText(mRaw.mock.calls[0][1])).toBe('{"path":"/data/new.cfg","create":true}\n');
  });
  it("builds a body with a newline-free head", async () => {
    expect(await blobText(saveBody({ path: "/a\nb" }, new Uint8Array([0x41])))).toBe('{"path":"/a\\nb"}\nA');
  });
});

describe("upload", () => {
  const file = (name: string, rel = "") => {
    const f = new File(["abc"], name);
    if (rel) Object.defineProperty(f, "webkitRelativePath", { value: rel });
    return f;
  };
  it("keeps a folder pick's relative paths and lists its folders parents first", () => {
    const items = uploadItems([file("1.jpg", "Album/2024/1.jpg"), file("a.txt"), file("x", "Album/../x")]);
    expect(items.map((i) => i.rel)).toEqual(["Album/2024/1.jpg", "a.txt", "Album/x"]);
    expect(uploadFolders(items)).toEqual(["Album", "Album/2024"]);
  });
  it("begins, then streams the bytes with the id", async () => {
    mCall.mockResolvedValueOnce({ id: "ab".repeat(16) });
    mSend.mockResolvedValue({ size: 3 });
    const f = file("a.txt");
    await uploadFile("/data/a.txt", f, true);
    expect(mCall).toHaveBeenCalledWith("/api/upload/begin", { path: "/data/a.txt", size: 3, overwrite: true });
    expect(mSend.mock.calls[0][0]).toBe(`/api/upload/data?id=${"ab".repeat(16)}`);
    expect(mSend.mock.calls[0][1]).toBe(f);
  });
  it("forgets the reservation when the send fails", async () => {
    mCall.mockResolvedValueOnce({ id: "cd".repeat(16) }).mockResolvedValueOnce({});
    mSend.mockRejectedValue(new ApiError(0, "aborted"));
    await expect(uploadFile("/data/a.txt", file("a.txt"), false)).rejects.toMatchObject({ code: "aborted" });
    expect(mCall).toHaveBeenLastCalledWith("/api/upload/cancel", { id: "cd".repeat(16) });
  });
  it("does not send when begin refuses (no space, exists)", async () => {
    mCall.mockRejectedValueOnce(new ApiError(507, "no_space"));
    await expect(uploadFile("/data/a.txt", file("a.txt"), false)).rejects.toMatchObject({ code: "no_space" });
    expect(mSend).not.toHaveBeenCalled();
  });
});

describe("download", () => {
  it("clicks a download link from the helper", async () => {
    mCall.mockResolvedValueOnce({ url: "/dl/0123/Games.tar", name: "Games.tar" });
    const clicked: { href: string; download: string }[] = [];
    const a = {
      href: "",
      download: "",
      rel: "",
      style: {} as Record<string, string>,
      click() {
        clicked.push({ href: this.href, download: this.download });
      },
      remove: vi.fn(),
    };
    const doc = { createElement: () => a, body: { appendChild: vi.fn() } } as unknown as Document;
    expect(await downloadItem("/data/Games", doc)).toBe("Games.tar");
    expect(mCall).toHaveBeenCalledWith("/api/download/begin", { path: "/data/Games" });
    expect(clicked).toEqual([{ href: "/dl/0123/Games.tar", download: "Games.tar" }]);
    expect(a.remove).toHaveBeenCalled();
  });
  it("refuses a link that is not the helper's", async () => {
    mCall.mockResolvedValueOnce({ url: "https://elsewhere/x" });
    await expect(downloadItem("/data/x", {} as Document)).rejects.toBeInstanceOf(ApiError);
  });
});
