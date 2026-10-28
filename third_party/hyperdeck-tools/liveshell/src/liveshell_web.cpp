/*
 * liveshell_web.cpp - the LiveShell window: a small HTTP server on kWebPort (the home-screen tile
 * opens it in the browser view). The page is liveshell_page.html, embedded at build time. GPL-3.0.
 *
 *   GET /               the page
 *   GET /icon.png       app icon
 *   GET /thumb?i=N      <clip>.png next to the clip (a still the page grabbed once), 404 if none
 *   GET /video?i=N      the clip itself, with Range support (the page's live preview)
 *   GET /api/state      {"enabled","status","current","videos":[...]}
 *   GET /api/select?i=N play clip N (index into videos)
 *   GET /api/on|off     wallpaper on / off
 *   GET /api/dim?v=N    darken the video by N % (0..80) so the home screen stays readable
 *   GET /api/volume?v=N sound of clips that have it, N % (0..100)
 */
#include "liveshell.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

extern "C" {
extern const uint8_t liveshell_icon_png[];
extern const uint8_t liveshell_icon_png_end[];
extern const uint8_t liveshell_page_html[];
extern const uint8_t liveshell_page_html_end[];
extern const uint8_t liveshell_remote_html[];
extern const uint8_t liveshell_remote_html_end[];
}

__asm__(".section .rodata\n"
        ".global liveshell_page_html\n"
        ".global liveshell_page_html_end\n"
        ".align  16\n"
        "liveshell_page_html:\n"
        ".incbin \"" LIVESHELL_PAGE_HTML "\"\n"
        "liveshell_page_html_end:\n"
        ".previous\n");

// Page for phones / PCs on the LAN (upload, play, delete, on/off): liveshell_remote.html.
__asm__(".section .rodata\n"
        ".global liveshell_remote_html\n"
        ".global liveshell_remote_html_end\n"
        ".align  16\n"
        "liveshell_remote_html:\n"
        ".incbin \"" LIVESHELL_REMOTE_HTML "\"\n"
        "liveshell_remote_html_end:\n"
        ".previous\n");

namespace liveshell {
namespace {

constexpr size_t kMaxThumb = 2 * 1024 * 1024;
constexpr long   kMinThumb = 3000; // smaller = a blank (black) frame: ignore it, grab again

std::string json_str(const std::string& s) {
	std::string out = "\"";
	for (const char c : s) {
		if (c == '"' || c == '\\') {
			out += '\\';
			out += c;
		} else if (c == '\n') {
			out += "\\n"; // games.txt lines: dropped before, so the picker never marked the chosen clip
		} else if (static_cast<unsigned char>(c) >= 0x20) {
			out += c;
		}
	}
	return out + "\"";
}

std::string state_json() {
	const State st = get_state();
	std::string j  = std::string("{\"enabled\":") + (st.enabled ? "true" : "false") + ",\"dim\":" + std::to_string(st.dim) + ",\"volume\":" + std::to_string(st.volume) +
	                ",\"blur\":" + std::to_string(st.blur) + ",\"looks\":[" + std::to_string(st.looks[0]) + "," + std::to_string(st.looks[1]) + "," + std::to_string(st.looks[2]) + "," + std::to_string(st.looks[3]) + "]" + ",\"shuffle\":" + (st.shuffle ? "true" : "false") + ",\"mainvideo\":" + (st.main_video ? "true" : "false") + ",\"gamevideos\":" + (st.game_videos ? "true" : "false") + ",\"gamepause\":" + (st.game_pause ? "true" : "false") + ",\"menuhide\":" + (st.menu_hide ? "true" : "false") + ",\"storeonly\":" + (st.store_only ? "true" : "false") + ",\"parallax\":" + (st.parallax ? "true" : "false") +
	                ",\"ip\":" + json_str(st.ip) + ",\"port\":" + std::to_string(kWebPort) + ",\"games\":" + json_str(games_text()) + ",\"game\":" + json_str(st.game) + ",\"status\":" + json_str(st.status) + ",\"current\":" + json_str(st.current) + ",\"videos\":[";
	for (size_t i = 0; i < st.videos.size(); ++i) {
		const Video& v = st.videos[i];
		j += (i ? ",{" : "{");
		struct stat fs {};
		const long long size = stat(v.path.c_str(), &fs) == 0 ? static_cast<long long>(fs.st_size) : 0;
		struct stat ts {};
		const std::string cover = v.path.substr(0, v.path.rfind('.')) + ".png";
		const bool        thumb = stat(cover.c_str(), &ts) == 0 && ts.st_size >= kMinThumb;
		j += "\"name\":" + json_str(v.name) + ",\"where\":" + json_str(v.where) + ",\"path\":" + json_str(v.path) +
		     ",\"size\":" + std::to_string(size) + ",\"thumb\":" + (thumb ? "true" : "false") + "}";
	}
	return j + "]}";
}

std::string result_json(bool ok, const std::string& error) {
	return ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":" + json_str(error) + "}";
}

void send_all(int fd, const void* data, size_t len) {
	const auto* p = static_cast<const uint8_t*>(data);
	while (len > 0) {
		const ssize_t n = send(fd, p, len, 0);
		if (n <= 0) {
			return;
		}
		p += n;
		len -= static_cast<size_t>(n);
	}
}

void reply(int fd, const char* status, const char* type, const void* body, size_t len) {
	char      head[256];
	const int n = snprintf(head, sizeof(head),
	                       "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
	                       "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
	                       status, type, len);
	send_all(fd, head, static_cast<size_t>(n));
	send_all(fd, body, len);
}

void reply_json(int fd, const std::string& json) {
	reply(fd, "200 OK", "application/json", json.data(), json.size());
}

void reply_404(int fd) {
	reply(fd, "404 Not Found", "text/plain", "not found", 9);
}

int index_arg(const std::string& query) {
	const size_t pos = query.find("i=");
	return pos == std::string::npos ? -1 : atoi(query.c_str() + pos + 2);
}

bool video_at(int i, Video& out) {
	const std::vector<Video> videos = list_videos();
	if (i < 0 || i >= static_cast<int>(videos.size())) {
		return false;
	}
	out = videos[static_cast<size_t>(i)];
	return true;
}

void serve_thumb(int fd, int i) {
	Video v;
	if (!video_at(i, v)) {
		reply_404(fd);
		return;
	}
	const std::string path = v.path.substr(0, v.path.rfind('.')) + ".png";
	struct stat       st {};
	if (stat(path.c_str(), &st) != 0 || st.st_size < kMinThumb || static_cast<size_t>(st.st_size) > kMaxThumb) {
		reply_404(fd);
		return;
	}
	std::string data(static_cast<size_t>(st.st_size), '\0');
	const int   f  = open(path.c_str(), O_RDONLY);
	const bool  ok = f >= 0 && read(f, &data[0], data.size()) == static_cast<ssize_t>(data.size());
	if (f >= 0) {
		close(f);
	}
	if (!ok) {
		reply_404(fd);
		return;
	}
	reply(fd, "200 OK", "image/png", data.data(), data.size());
}

// Video preview: WebKit only plays media from servers that answer Range requests (206).
void serve_video(int fd, int i, const char* request) {
	Video v;
	struct stat st {};
	if (!video_at(i, v) || stat(v.path.c_str(), &st) != 0 || st.st_size <= 0) {
		reply_404(fd);
		return;
	}
	const long long size  = static_cast<long long>(st.st_size);
	long long       first = 0, last = size - 1;
	bool            range = false;
	if (const char* r = strcasestr(request, "\nRange: bytes=")) {
		r += 14;
		char* end = nullptr;
		if (*r == '-') { // suffix: last N bytes
			const long long n = strtoll(r + 1, nullptr, 10);
			first             = n > 0 && n < size ? size - n : 0;
		} else {
			first = strtoll(r, &end, 10);
			if (end != nullptr && *end == '-' && end[1] >= '0' && end[1] <= '9') {
				last = strtoll(end + 1, nullptr, 10);
			}
		}
		if (first < 0 || first >= size) {
			char      head[160];
			const int n = snprintf(head, sizeof(head),
			                       "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */%lld\r\n"
			                       "Content-Length: 0\r\nConnection: close\r\n\r\n",
			                       size);
			send_all(fd, head, static_cast<size_t>(n));
			return;
		}
		if (last >= size) {
			last = size - 1;
		}
		range = true;
	}
	const int f = open(v.path.c_str(), O_RDONLY);
	if (f < 0) {
		reply_404(fd);
		return;
	}
	char      head[320];
	const int n = range ? snprintf(head, sizeof(head),
	                               "HTTP/1.1 206 Partial Content\r\nContent-Type: video/mp4\r\nAccept-Ranges: bytes\r\n"
	                               "Content-Range: bytes %lld-%lld/%lld\r\nContent-Length: %lld\r\nConnection: close\r\n\r\n",
	                               first, last, size, last - first + 1)
	                    : snprintf(head, sizeof(head),
	                               "HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\nAccept-Ranges: bytes\r\n"
	                               "Content-Length: %lld\r\nConnection: close\r\n\r\n",
	                               size);
	send_all(fd, head, static_cast<size_t>(n));
	lseek(f, static_cast<off_t>(first), SEEK_SET);
	static thread_local char buf[64 * 1024];
	long long left = last - first + 1;
	while (left > 0) {
		const size_t  want = left < static_cast<long long>(sizeof(buf)) ? static_cast<size_t>(left) : sizeof(buf);
		const ssize_t got  = read(f, buf, want);
		if (got <= 0 || send(fd, buf, static_cast<size_t>(got), 0) != got) {
			break; // end of file, or the browser closed the connection (normal when it seeks)
		}
		left -= got;
	}
	close(f);
}

// POST /thumb?i=N: the page grabbed a still from the clip; keep it as <clip>.png so it is grabbed once.
void save_thumb(int fd, int i, const char* headers, const char* body, size_t have) {
	Video       v;
	const char* cl   = strcasestr(headers, "\nContent-Length:");
	const long  size = cl != nullptr ? strtol(cl + 16, nullptr, 10) : 0;
	if (!video_at(i, v) || size < kMinThumb || static_cast<size_t>(size) > kMaxThumb) {
		reply_json(fd, result_json(false, "bad thumbnail"));
		return;
	}
	std::string data(body, have < static_cast<size_t>(size) ? have : static_cast<size_t>(size));
	char        chunk[16 * 1024];
	while (data.size() < static_cast<size_t>(size)) {
		const ssize_t got = recv(fd, chunk, sizeof(chunk), 0);
		if (got <= 0) {
			reply_json(fd, result_json(false, "upload cut"));
			return;
		}
		data.append(chunk, static_cast<size_t>(got));
	}
	data.resize(static_cast<size_t>(size));
	const std::string path = v.path.substr(0, v.path.rfind('.')) + ".png";
	const int         f    = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
	const bool ok = f >= 0 && write(f, data.data(), data.size()) == static_cast<ssize_t>(data.size());
	if (f >= 0) {
		close(f);
	}
	log("web: thumbnail %s (%zu bytes) %s", path.c_str(), data.size(), ok ? "saved" : "NOT saved");
	reply_json(fd, result_json(ok, "cannot write the thumbnail"));
}

std::string url_decode(const std::string& raw) {
	std::string out;
	for (size_t k = 0; k < raw.size(); ++k) {
		char c = raw[k];
		if (c == '%' && k + 2 < raw.size() && isxdigit(static_cast<unsigned char>(raw[k + 1])) &&
		    isxdigit(static_cast<unsigned char>(raw[k + 2]))) {
			c = static_cast<char>(strtol(raw.substr(k + 1, 2).c_str(), nullptr, 16));
			k += 2;
		} else if (c == '+') {
			c = ' ';
		}
		out += c;
	}
	return out;
}

// Upload from a phone / PC: POST /api/upload?name=<file>.mp4, raw body. Only plain file names,
// only .mp4, only into /data/LiveShell/video; written as .part and renamed when complete.
void save_upload(int fd, const std::string& query, const char* headers, const char* body, size_t have) {
	const size_t      at  = query.find("name=");
	const std::string raw = at == std::string::npos ? "" : url_decode(query.substr(at + 5, query.find('&', at) - (at + 5)));
	std::string       name;
	for (const char c : raw) {
		if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') {
			name += c;
		} else if (c == ' ') {
			name += '-';
		}
	}
	while (!name.empty() && name[0] == '.') {
		name.erase(0, 1);
	}
	if (name.size() > 120) {
		name = name.substr(name.size() - 120);
	}
	const bool      is_mp4 = name.size() > 4 && strcasecmp(name.c_str() + name.size() - 4, ".mp4") == 0;
	const char*     cl     = strcasestr(headers, "\nContent-Length:");
	const long long size   = cl != nullptr ? strtoll(cl + 16, nullptr, 10) : 0;
	if (!is_mp4 || size <= 0 || size > 8LL * 1024 * 1024 * 1024) {
		reply_json(fd, result_json(false, "Only .mp4 files can be uploaded"));
		return;
	}
	const std::string path = "/data/LiveShell/video/" + name;
	const std::string part = path + ".part";
	const int         f    = open(part.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (f < 0) {
		reply_json(fd, result_json(false, "Cannot write to /data/LiveShell/video"));
		return;
	}
	log("upload %s: %lld bytes", name.c_str(), size);
	long long    done  = 0;
	bool         ok    = true;
	const size_t first = have < static_cast<size_t>(size) ? have : static_cast<size_t>(size);
	if (first > 0) {
		ok   = write(f, body, first) == static_cast<ssize_t>(first);
		done = static_cast<long long>(first);
	}
	static thread_local char chunk[256 * 1024];
	while (ok && done < size) {
		const size_t  want = size - done < static_cast<long long>(sizeof(chunk)) ? static_cast<size_t>(size - done) : sizeof(chunk);
		const ssize_t got  = recv(fd, chunk, want, 0);
		if (got <= 0) {
			ok = false;
			break;
		}
		ok = write(f, chunk, static_cast<size_t>(got)) == got;
		done += got;
	}
	close(f);
	if (!ok || done != size || rename(part.c_str(), path.c_str()) != 0) {
		unlink(part.c_str());
		log("upload %s: FAILED at %lld of %lld bytes", name.c_str(), done, size);
		reply_json(fd, result_json(false, "Upload stopped before the end"));
		return;
	}
	log("upload %s: done", name.c_str());
	reply_json(fd, result_json(true, ""));
}

bool from_lan(int fd) {
	sockaddr_in peer {};
	socklen_t   len = sizeof(peer);
	return getpeername(fd, reinterpret_cast<sockaddr*>(&peer), &len) == 0 && peer.sin_addr.s_addr != htonl(INADDR_LOOPBACK);
}

void handle(int fd) {
	char          buf[4096];
	const ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
	if (n <= 0) {
		return;
	}
	buf[n] = 0;
	const bool post = strncmp(buf, "POST ", 5) == 0;
	if (!post && strncmp(buf, "GET ", 4) != 0) {
		reply(fd, "405 Method Not Allowed", "text/plain", "", 0);
		return;
	}
	const char*  head_end = strstr(buf, "\r\n\r\n");
	const char*  body     = head_end != nullptr ? head_end + 4 : buf + n;
	const size_t have     = static_cast<size_t>(buf + n - body);
	char* target = buf + (post ? 5 : 4);
	char* end    = strpbrk(target, " \r\n");
	const char* headers = "";
	if (end != nullptr) {
		*end    = 0;
		headers = end + 1;
	}
	std::string       path(target), query;
	const size_t      q = path.find('?');
	if (q != std::string::npos) {
		query = path.substr(q + 1);
		path.resize(q);
	}

	std::string error;
	if (post) {
		if (path == "/api/upload") {
			save_upload(fd, query, headers, body, have);
		} else if (path == "/thumb") {
			save_thumb(fd, index_arg(query), headers, body, have);
		} else {
			reply_404(fd);
		}
	} else if (path == "/api/log") {
		// Page-side problems (video preview, stills) end up in liveshell.log.
		const std::string raw = query.rfind("m=", 0) == 0 ? query.substr(2) : query;
		std::string       m;
		for (size_t k = 0; k < raw.size(); ++k) {
			char c = raw[k];
			if (c == '%' && k + 2 < raw.size() + 0 && isxdigit(static_cast<unsigned char>(raw[k + 1])) &&
			    isxdigit(static_cast<unsigned char>(raw[k + 2]))) {
				c = static_cast<char>(strtol(raw.substr(k + 1, 2).c_str(), nullptr, 16));
				k += 2;
			} else if (c == '+') {
				c = ' ';
			}
			m += static_cast<unsigned char>(c) < 0x20 ? '?' : c;
		}
		log("page: %s", m.substr(0, 300).c_str());
		reply_json(fd, result_json(true, error));
	} else if ((path == "/" || path == "/index.html") && from_lan(fd)) {
		// Opened from a phone / PC: the remote page (upload, play, delete, on/off).
		reply(fd, "200 OK", "text/html; charset=utf-8", liveshell_remote_html,
		      static_cast<size_t>(liveshell_remote_html_end - liveshell_remote_html));
	} else if (path == "/" || path == "/index.html" || path == "/ps5") {
		reply(fd, "200 OK", "text/html; charset=utf-8", liveshell_page_html,
		      static_cast<size_t>(liveshell_page_html_end - liveshell_page_html));
	} else if (path == "/icon.png") {
		reply(fd, "200 OK", "image/png", liveshell_icon_png,
		      static_cast<size_t>(liveshell_icon_png_end - liveshell_icon_png));
	} else if (path == "/video") {
		serve_video(fd, index_arg(query), headers);
	} else if (path == "/thumb") {
		serve_thumb(fd, index_arg(query));
	} else if (path == "/api/state") {
		reply_json(fd, state_json());
	} else if (path == "/api/select") {
		Video v;
		const bool ok = video_at(index_arg(query), v) ? select_video(v.path, error) : (error = "No such video", false);
		log("web: select %s -> %s", v.path.c_str(), ok ? "ok" : error.c_str());
		reply_json(fd, result_json(ok, error));
	} else if (path == "/api/step") {
		const size_t pos = query.find("d=");
		const int direction = pos == std::string::npos ? 1 : atoi(query.c_str() + pos + 2);
		const bool ok = step_video(direction < 0 ? -1 : 1, error);
		reply_json(fd, result_json(ok, error));
	} else if (path == "/api/dim") {
		const size_t pos = query.find("v=");
		set_dim(pos == std::string::npos ? 25 : atoi(query.c_str() + pos + 2));
		reply_json(fd, result_json(true, error));
	} else if (path == "/api/volume") {
		const size_t pos = query.find("v=");
		set_volume(pos == std::string::npos ? 100 : atoi(query.c_str() + pos + 2));
		reply_json(fd, result_json(true, error));
	} else if (path == "/api/look") {
		// /api/look?k=bottom&v=2
		const size_t k = query.find("k="), v = query.find("v=");
		const std::string key = k == std::string::npos ? "" : query.substr(k + 2, query.find('&', k) - (k + 2));
		set_look(key, v == std::string::npos ? 0 : atoi(query.c_str() + v + 2));
		reply_json(fd, result_json(true, error));
	} else if (path == "/api/games") {
		std::string j = "[";
		for (const Game& g : list_games()) {
			j += (j.size() > 1 ? "," : "");
			j += "{\"tid\":" + json_str(g.tid) + ",\"name\":" + json_str(g.name) + ",\"clip\":" + json_str(saved_game_clip(g.tid)) + "}";
		}
		reply_json(fd, j + "]");
	} else if (path == "/gameicon") {
		const size_t      t   = query.find("tid=");
		const std::string tid = t == std::string::npos ? "" : query.substr(t + 4, 9);
		const std::string png = "/user/appmeta/" + tid + "/icon0.png";
		struct stat       st {};
		std::string       data;
		if (is_title_id(tid.c_str()) && stat(png.c_str(), &st) == 0 && st.st_size > 0 && st.st_size < 4 * 1024 * 1024) {
			data.resize(static_cast<size_t>(st.st_size));
			const int f = open(png.c_str(), O_RDONLY);
			if (f >= 0) {
				if (read(f, &data[0], data.size()) != static_cast<ssize_t>(data.size())) {
					data.clear();
				}
				close(f);
			}
		}
		if (data.empty()) {
			reply_404(fd);
		} else {
			reply(fd, "200 OK", "image/png", data.data(), data.size());
		}
	} else if (path == "/api/gamebg") {
		// /api/gamebg?tid=PPSA01234&i=N  (i=-1: main wallpaper)
		const size_t t = query.find("tid="), k = query.find("i=");
		std::string tid = t == std::string::npos ? "" : query.substr(t + 4, query.find('&', t) - (t + 4));
		tid.erase(std::remove_if(tid.begin(), tid.end(), [](char c) { return !isalnum(static_cast<unsigned char>(c)); }), tid.end());
		Video v;
		const int i = k == std::string::npos ? -1 : atoi(query.c_str() + k + 2);
		const bool ok = !tid.empty() && (i < 0 || video_at(i, v));
		if (ok) {
			set_game_background(tid, i < 0 ? std::string() : v.path);
		}
		reply_json(fd, result_json(ok, "Unknown game or video"));
	} else if (path == "/api/blur") {
		const size_t pos = query.find("v=");
		set_blur(pos == std::string::npos ? 0 : atoi(query.c_str() + pos + 2));
		reply_json(fd, result_json(true, error));
	} else if (path == "/api/set") {
		// /api/set?k=shuffle&v=1
		const size_t k = query.find("k="), v = query.find("v=");
		const std::string key = k == std::string::npos ? "" : query.substr(k + 2, query.find('&', k) - (k + 2));
		const bool ok = set_option(key, v != std::string::npos && query[v + 2] == '1', error);
		reply_json(fd, result_json(ok, error));
	} else if (path == "/api/delete") {
		Video v;
		const bool ok = video_at(index_arg(query), v) ? delete_video(v.path, error) : (error = "No such video", false);
		reply_json(fd, result_json(ok, error));
	} else if (path == "/api/delete-current") {
		const bool ok = delete_current_video(error);
		reply_json(fd, result_json(ok, error));
	} else if (path == "/api/focusbg") {
		const size_t mode = query.find("mode=");
		const bool use_current = mode != std::string::npos &&
		                         query.compare(mode + 5, 7, "current") == 0;
		const bool use_main = mode != std::string::npos &&
		                      query.compare(mode + 5, 4, "main") == 0;
		const bool ok = (use_current || use_main)
		                    ? set_focused_game_background(use_current, error)
		                    : (error = "Unknown profile mode", false);
		reply_json(fd, result_json(ok, error));
	} else if (path == "/api/reset") {
		const bool ok = reset_settings(error);
		reply_json(fd, result_json(ok, error));
	} else if (path == "/api/shutdown") {
		reply_json(fd, result_json(true, error));
		request_shutdown();
	} else if (path == "/api/on" || path == "/api/off") {
		const bool on = path == "/api/on";
		const bool ok = set_enabled(on, error);
		log("web: %s -> %s", on ? "on" : "off", ok ? "ok" : error.c_str());
		reply_json(fd, result_json(ok, error));
	} else {
		reply_404(fd);
	}
}

void* connection_thread(void* arg) {
	const int c = static_cast<int>(reinterpret_cast<intptr_t>(arg));
	handle(c);
	close(c);
	return nullptr;
}

int open_web_port() {
	const int s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0) {
		log("web: socket failed (errno %d)", errno);
		return -1;
	}
	int on = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	sockaddr_in addr     = {};
	addr.sin_family      = AF_INET;
	addr.sin_port        = htons(kWebPort);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(s, 8) != 0) {
		log("web: port %d unavailable (errno %d)", kWebPort, errno);
		close(s);
		return -1;
	}
	timeval tv {1, 0}; // accept wakes up every second: rest mode / reopen checks
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	log("web: window on port %d", kWebPort);
	return s;
}

void* server_thread(void*) {
	// Rest mode kills listening sockets (user report: the window was dead after waking the PS5):
	// when accept keeps failing, open the port again.
	int s = -1, fails = 0, epoch = net_epoch();
	for (;;) {
		if (s >= 0 && epoch != net_epoch()) {
			log("web: reopening the window port after rest mode");
			close(s);
			s = -1;
		}
		if (s < 0) {
			epoch = net_epoch();
			s     = open_web_port();
			if (s < 0) {
				sleep(2);
				continue;
			}
			fails = 0;
		}
		const int c = accept(s, nullptr, nullptr);
		if (c < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				continue; // the 1 s timeout
			}
			if (++fails >= 30) { // ~3 s of errors
				log("web: port broken (errno %d), reopening", errno);
				close(s);
				s = -1;
			}
			usleep(100 * 1000);
			continue;
		}
		fails = 0;
		timeval tv {10, 0};
		setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
		// One thread per connection: a video stream must not hold up the page's API calls.
		pthread_t t;
		if (pthread_create(&t, nullptr, connection_thread, reinterpret_cast<void*>(static_cast<intptr_t>(c))) == 0) {
			pthread_detach(t);
		} else {
			handle(c);
			close(c);
		}
	}
	return nullptr;
}

} // namespace

void start_web_server() {
	pthread_t t;
	if (pthread_create(&t, nullptr, server_thread, nullptr) == 0) {
		pthread_detach(t);
	} else {
		log("web: thread start failed");
	}
}

} // namespace liveshell
