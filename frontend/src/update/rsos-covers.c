/*
 * rsos-covers - game covers from the libretro thumbnail server
 * (thumbnails.libretro.com, the one RetroArch's thumbnail updater uses), for
 * Settings > Game lists > Download game covers.
 *
 *   rsos-covers [--kind K] [--progress FILE] [--missing FILE] [--max PX] [--cache DIR]
 *               [--replace] REQUEST
 *   rsos-covers [--kind K] [--cache DIR] --suggest SYSTEM QUERY OUT
 *   rsos-covers [--kind K] [--max PX] --get SYSTEM NAME OUT
 *
 * --kind: "titles" (the server's Named_Titles: the game's title screen, 4:3,
 * like the pictures of the RetroStone games) or "boxarts" (Named_Boxarts,
 * the default). A game without one of the first kind gets the other one.
 *
 * REQUEST (written by the menu, ui/storage.c): one game per line,
 *   <system folder>\t<the system's ROM folder>\t<ROM path>
 * for the games that have no image yet. Per system with a libretro name: the
 * server's listing of that kind once (about 1 MB; kept for 7 days in
 * --cache DIR as <system folder>-<kind>.txt, one name per line), then per game the
 * best name: the ROM's file name itself (No-Intro names match exactly), else
 * the same title (words_of(): without its (tags), "Disc 2", "the", accents;
 * "&" = "and", VII = 7), the ROM's own region first, then USA, World, Europe,
 * Japan, the same disc first; else the shortest title that starts with it.
 * The cover is scaled to fit --max (400) and saved as <ROM folder>/media/
 * covers/<file name without extension>.jpg, which the menu shows for games
 * whose gamelist.xml gives no image (ui/games.c). Nothing else is written:
 * no gamelist.xml, and no existing cover unless --replace (a .jpg only: the
 * downloads; a .png is a picture of the player's own and wins anyway).
 *
 * --suggest (Choose cover, in the game's options): the 20 names closest to
 * QUERY (a ROM file name or words typed in the menu), best first, one per
 * line in OUT: "<kind> TAB <name>" (the other kind when the first has none).
 * --get: the picture NAME of SYSTEM, saved as OUT (a .jpg).
 *
 * Progress, for the menu's task page: "<percent> covers <done> <total>
 * <system>" in FILE; at the end "100 done <found> <missing> <failed>", or
 * "error <reason>" (network, clock, tls, nospace, request) when it stopped.
 * The games not found are listed in --missing ("<system>\t<file>").
 * Arcade systems (zip names, not titles) are skipped: they need a name list.
 *
 * Exit status 0 when it ran through (even if some covers were not found).
 */
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "covers_img.h"
#include "http.h"

#define SERVER "https://thumbnails.libretro.com"
#define INDEX_MAX (8u * 1024 * 1024)
#define IMAGE_MAX (8u * 1024 * 1024)
#define CACHE_DAYS 7
#define SUGGEST_MAX 20

static const struct {
	const char *folder, *libretro;
} g_systems[] = {
	{ "nes", "Nintendo - Nintendo Entertainment System" },
	{ "fds", "Nintendo - Family Computer Disk System" },
	{ "snes", "Nintendo - Super Nintendo Entertainment System" },
	{ "n64", "Nintendo - Nintendo 64" },
	{ "gb", "Nintendo - Game Boy" },
	{ "gbc", "Nintendo - Game Boy Color" },
	{ "gba", "Nintendo - Game Boy Advance" },
	{ "pokemini", "Nintendo - Pokemon Mini" },
	{ "mastersystem", "Sega - Master System - Mark III" },
	{ "megadrive", "Sega - Mega Drive - Genesis" },
	{ "gamegear", "Sega - Game Gear" },
	{ "sg1000", "Sega - SG-1000" },
	{ "sega32x", "Sega - 32X" },
	{ "segacd", "Sega - Mega-CD - Sega CD" },
	{ "pico", "Sega - PICO" },
	{ "psx", "Sony - PlayStation" },
	{ "pcengine", "NEC - PC Engine - TurboGrafx 16" },
	{ "pcenginecd", "NEC - PC Engine CD - TurboGrafx-CD" },
	{ "supergrafx", "NEC - PC Engine SuperGrafx" },
	{ "neocd", "SNK - Neo Geo CD" },
	{ "ngp", "SNK - Neo Geo Pocket" },
	{ "ngpc", "SNK - Neo Geo Pocket Color" },
	{ "atari2600", "Atari - 2600" },
	{ "atari7800", "Atari - 7800" },
	{ "atarilynx", "Atari - Lynx" },
	{ "wonderswan", "Bandai - WonderSwan" },
	{ "wonderswancolor", "Bandai - WonderSwan Color" },
	{ "coleco", "Coleco - ColecoVision" },
	{ "msx", "Microsoft - MSX" },
	{ "c64", "Commodore - 64" },
	{ "zxspectrum", "Sinclair - ZX Spectrum" },
	{ "amstradcpc", "Amstrad - CPC" },
	{ "dos", "DOS" },
	{ "scummvm", "ScummVM" },
	{ "doom", "DOOM" },
	{ "cavestory", "Cave Story" },
};

static const char *g_progress;
static volatile int g_cancel;

/* the server's folders: [0] what --kind asks for, [1] the fallback */
enum { K_TITLES, K_BOXARTS };
static const char *const g_kind_name[2] = { "titles", "boxarts" };
static const char *const g_kind_dir[2] = { "Named_Titles", "Named_Boxarts" };

static int mkdir_p(const char *dir);
static void http_opts_init(struct http_opts *o);

static void on_term(int sig)
{
	(void)sig;
	g_cancel = 1;
}

static void progress(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void progress(const char *fmt, ...)
{
	char tmp[1100];
	va_list ap;
	FILE *f;

	if (!g_progress)
		return;
	snprintf(tmp, sizeof(tmp), "%s.tmp", g_progress);
	f = fopen(tmp, "w");
	if (!f)
		return;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fputc('\n', f);
	fclose(f);
	rename(tmp, g_progress);
}

/* ------------------------------------------------------------- strings */
struct membuf {
	char *b;
	size_t len, cap, max;
};

static int mem_data(const void *data, size_t n, void *user)
{
	struct membuf *m = user;

	if (g_cancel || m->len + n > m->max)
		return -1;
	if (m->len + n + 1 > m->cap) {
		size_t cap = m->cap ? m->cap * 2 : 65536;
		char *nb;

		while (cap < m->len + n + 1)
			cap *= 2;
		nb = realloc(m->b, cap);
		if (!nb)
			return -1;
		m->b = nb;
		m->cap = cap;
	}
	memcpy(m->b + m->len, data, n);
	m->len += n;
	m->b[m->len] = 0;
	return 0;
}

static int hexval(int c)
{
	return isdigit(c) ? c - '0' : tolower(c) - 'a' + 10;
}

static void url_decode(char *s)
{
	char *o = s;

	for (; *s; s++) {
		if (s[0] == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
			*o++ = (char)(hexval((unsigned char)s[1]) * 16 + hexval((unsigned char)s[2]));
			s += 2;
		} else {
			*o++ = *s;
		}
	}
	*o = 0;
}

static void url_encode(const char *s, char *out, size_t n)
{
	size_t o = 0;

	for (; *s && o + 4 < n; s++) {
		unsigned char c = (unsigned char)*s;

		if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~')
			out[o++] = (char)c;
		else
			o += (size_t)snprintf(out + o, n - o, "%%%02X", c);
	}
	out[o] = 0;
}

/* libretro's thumbnail names: &*:`<>?\| and / replaced by _ (RetroArch) */
static void thumb_name(const char *s, char *out, size_t n)
{
	size_t o = 0;

	for (; *s && o + 1 < n; s++)
		out[o++] = strchr("&*/:`<>?\\|\"", *s) ? '_' : *s;
	out[o] = 0;
}

/* ------------------------------------------------------------- matching
 * The words of a title, outside (tags) and [tags]: lower case letters and
 * digits, accents folded ("Pokémon" = "Pokemon"), "&" = "and", apostrophes
 * dropped ("Don't" = "dont"), II to IX and V as digits ("Final Fantasy VII" =
 * "Final Fantasy 7"), without "the" ("The Legend of Zelda" = "Legend of
 * Zelda, The") and without a disc number ("Disc 2", "Disk 2 of 3", "CD2",
 * "(Disc 2)"): *disc gets it (0: none). */
#define MAX_WORDS 24

struct words {
	int n;
	char w[MAX_WORDS][32];
};

/* U+00C0..U+00FF (UTF-8 C3 80..C3 BF) without their accents, 0: not a letter */
static const char g_latin1[65] = "AAAAAAACEEEEIIIIDNOOOOO" "\0" "OUUUUYTsaaaaaaaceeeeiiiidnooooo" "\0" "ouuuuyty";

static void words_add(struct words *ws, const char *t)
{
	size_t l = strlen(t);

	if (!l || ws->n >= MAX_WORDS)
		return;
	if (l >= sizeof(ws->w[0]))
		l = sizeof(ws->w[0]) - 1;
	memcpy(ws->w[ws->n], t, l);
	ws->w[ws->n][l] = 0;
	ws->n++;
}

static bool all_digits(const char *w)
{
	if (!*w)
		return false;
	for (; *w; w++)
		if (!isdigit((unsigned char)*w))
			return false;
	return true;
}

/* "disc2", "disk2", "cd2" -> 2, else 0 */
static int glued_disc(const char *w)
{
	size_t k = !strncmp(w, "disc", 4) || !strncmp(w, "disk", 4) ? 4 : !strncmp(w, "cd", 2) ? 2 : 0;

	return k && all_digits(w + k) ? atoi(w + k) : 0;
}

static void words_of(const char *s, struct words *ws, int *disc)
{
	static const char *const roman[] = { "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix" };
	static const char *const arabic[] = { "2", "3", "4", "5", "6", "7", "8", "9" };
	char clean[512], *save = NULL, *t;
	struct words raw = { 0 };
	size_t o = 0;
	int depth = 0, d = 0;

	for (; *s && o + 6 < sizeof(clean); s++) {
		unsigned char c = (unsigned char)*s;

		if (c == '(' || c == '[') {
			if ((!strncasecmp(s + 1, "Disc ", 5) || !strncasecmp(s + 1, "Disk ", 5)) &&
			    isdigit((unsigned char)s[6]))
				d = atoi(s + 6);
			depth++;
		} else if ((c == ')' || c == ']') && depth) {
			depth--;
		} else if (depth) {
			continue;
		} else if (c == 0xc3 && (unsigned char)s[1] >= 0x80 && (unsigned char)s[1] <= 0xbf) {
			char f = g_latin1[(unsigned char)s[1] - 0x80];

			s++;
			clean[o++] = f ? (char)tolower((unsigned char)f) : ' ';
		} else if (c == 0xe2 && (unsigned char)s[1] == 0x80 && (unsigned char)s[2] == 0x99) {
			s += 2;                       /* ’ like ' */
		} else if (c == '\'') {
			continue;
		} else if (c == '&') {
			memcpy(clean + o, " and ", 5);
			o += 5;
		} else {
			clean[o++] = isalnum(c) ? (char)tolower(c) : ' ';
		}
	}
	clean[o] = 0;
	for (t = strtok_r(clean, " ", &save); t; t = strtok_r(NULL, " ", &save))
		words_add(&raw, t);
	ws->n = 0;
	for (int i = 0; i < raw.n; i++) {
		const char *w = raw.w[i];
		int g;

		if (!strcmp(w, "the"))
			continue;
		if ((!strcmp(w, "disc") || !strcmp(w, "disk") || !strcmp(w, "cd")) && i + 1 < raw.n &&
		    all_digits(raw.w[i + 1])) {
			d = atoi(raw.w[++i]);
			if (i + 2 < raw.n && !strcmp(raw.w[i + 1], "of") && all_digits(raw.w[i + 2]))
				i += 2;
			continue;
		}
		if ((g = glued_disc(w)) > 0) {
			d = g;
			continue;
		}
		for (size_t r = 0; r < sizeof(roman) / sizeof(roman[0]); r++)
			if (!strcmp(w, roman[r])) {
				w = arabic[r];
				break;
			}
		words_add(ws, w);
	}
	if (disc)
		*disc = d;
}

/* The matching key: the words of words_of() together */
static void norm_title(const char *s, char *out, size_t n, int *disc)
{
	struct words ws;
	size_t o = 0;

	words_of(s, &ws, disc);
	out[0] = 0;
	for (int i = 0; i < ws.n && o + 1 < n; i++)
		o += (size_t)snprintf(out + o, n - o, "%s", ws.w[i]);
}

static void region_of(const char *s, char *out, size_t n)
{
	const char *p = strchr(s, '(');
	const char *q = p ? strchr(p, ')') : NULL;

	out[0] = 0;
	if (p && q && (size_t)(q - p) < n) {
		memcpy(out, p + 1, (size_t)(q - p - 1));
		out[q - p - 1] = 0;
	}
}

/* ------------------------------------------------------------- the index */
struct entry {
	char *name;                  /* without .png */
	char *norm;
	int disc;                    /* (Disc N), 0: none */
};

struct index {
	struct entry *e;
	int n;
};

static int cmp_norm(const void *a, const void *b)
{
	const struct entry *x = a, *y = b;
	int c = strcmp(x->norm, y->norm);

	return c ? c : strcmp(x->name, y->name);
}

static void index_free(struct index *ix)
{
	for (int i = 0; i < ix->n; i++) {
		free(ix->e[i].name);
		free(ix->e[i].norm);
	}
	free(ix->e);
	ix->e = NULL;
	ix->n = 0;
}

static int index_add(struct index *ix, const char *name, int *cap)
{
	char norm[512];

	if (ix->n == *cap) {
		struct entry *ne;

		*cap = *cap ? *cap * 2 : 4096;
		ne = realloc(ix->e, sizeof(*ix->e) * (size_t)*cap);
		if (!ne)
			return -1;
		ix->e = ne;
	}
	norm_title(name, norm, sizeof(norm), &ix->e[ix->n].disc);
	ix->e[ix->n].name = strdup(name);
	ix->e[ix->n].norm = strdup(norm);
	if (!ix->e[ix->n].name || !ix->e[ix->n].norm)
		return -1;
	ix->n++;
	return 0;
}

/* Apache's listing: <a href="Name%20(USA).png"> */
static int index_parse(struct index *ix, char *html)
{
	int cap = 0;
	char *p = html;

	while ((p = strstr(p, "href=\""))) {
		char *q, *dot;

		p += 6;
		q = strchr(p, '"');
		if (!q)
			break;
		*q = 0;
		dot = q - 4;
		if (dot > p && !strcasecmp(dot, ".png") && p[0] != '/' && p[0] != '?') {
			*dot = 0;
			url_decode(p);
			if (index_add(ix, p, &cap) < 0)
				return -1;
		}
		p = q + 1;
	}
	qsort(ix->e, (size_t)ix->n, sizeof(*ix->e), cmp_norm);
	return ix->n;
}

/* The listing of a system: from the cache when it is recent, else from the
 * server (two tries), then cached. The count of names, or -1 (*e, err). */
static int index_load(struct index *ix, const char *sys, const char *lr, int kind, const char *cache_dir,
		      enum rsu_err *e, char *err, size_t errn)
{
	char path[1100], line[1024], enc[600], url[1024];
	struct membuf m = { .max = INDEX_MAX };
	struct http_opts o;
	struct http_resp r;
	struct stat st;
	FILE *f;
	int cap = 0;

	*e = RSU_OK;
	if (cache_dir) {
		snprintf(path, sizeof(path), "%s/%s-%s.txt", cache_dir, sys, g_kind_name[kind]);
		if (stat(path, &st) == 0 && time(NULL) - st.st_mtime < CACHE_DAYS * 86400 && (f = fopen(path, "r"))) {
			while (fgets(line, sizeof(line), f)) {
				line[strcspn(line, "\r\n")] = 0;
				if (line[0] && index_add(ix, line, &cap) < 0)
					break;
			}
			fclose(f);
			if (ix->n > 0) {
				qsort(ix->e, (size_t)ix->n, sizeof(*ix->e), cmp_norm);
				fprintf(stderr, "rsos-covers: %s: %d %s from the cache\n", sys, ix->n, g_kind_name[kind]);
				return ix->n;
			}
			index_free(ix);
		}
	}
	url_encode(lr, enc, sizeof(enc));
	snprintf(url, sizeof(url), SERVER "/%s/%s/", enc, g_kind_dir[kind]);
	http_opts_init(&o);
	for (int attempt = 0; attempt < 2 && !g_cancel; attempt++) {
		m.len = 0;
		*e = http_get(&o, url, 0, -1, NULL, mem_data, &m, &r, err, errn);
		if (!*e)
			break;
		fprintf(stderr, "rsos-covers: %s: listing: %s%s\n", sys, err, attempt ? "" : ", again");
	}
	if (*e || !m.b || index_parse(ix, m.b) <= 0) {
		if (!*e) {
			*e = RSU_E_NETWORK;
			snprintf(err, errn, "empty listing");
		}
		free(m.b);
		index_free(ix);
		return -1;
	}
	free(m.b);
	if (cache_dir && mkdir_p(cache_dir) == 0) {
		char tmp[1200];

		snprintf(tmp, sizeof(tmp), "%s.tmp", path);
		if ((f = fopen(tmp, "w"))) {
			for (int i = 0; i < ix->n; i++)
				fprintf(f, "%s\n", ix->e[i].name);
			if (fclose(f) == 0)
				rename(tmp, path);
			else
				unlink(tmp);
		}
	}
	return ix->n;
}

static int region_rank(const char *name, const char *want)
{
	static const char *const pref[] = { "USA", "World", "Europe", "Japan" };
	char r[128];

	region_of(name, r, sizeof(r));
	if (want[0] && !strcmp(r, want))
		return 0;
	for (int i = 0; i < 4; i++)
		if (strstr(r, pref[i]))
			return 1 + i;
	return 9;
}

/* Lower is better: the region (the ROM's own, then USA, World, Europe,
 * Japan), the same disc (or disc 1 for a whole game), plain releases before
 * (Beta), (Proto), (Sample), (Demo). */
static int entry_rank(const struct entry *e, const char *want_region, int want_disc)
{
	int rank = region_rank(e->name, want_region) * 4;

	if (want_disc ? e->disc != want_disc : e->disc > 1)
		rank += 1;
	if (strstr(e->name, "(Beta") || strstr(e->name, "(Proto") || strstr(e->name, "(Sample") ||
	    strstr(e->name, "(Demo"))
		rank += 80;
	return rank;
}

/* The first entry whose key is not below norm */
static int index_lower(const struct index *ix, const char *norm)
{
	int lo = 0, hi = ix->n;

	while (lo < hi) {
		int mid = (lo + hi) / 2;

		if (strcmp(ix->e[mid].norm, norm) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

/* The best cover name for a ROM file stem, NULL if none. */
static const char *index_match(const struct index *ix, const char *stem)
{
	char tn[512], norm[512], want[128];
	const char *best = NULL;
	int best_rank = 9999, lo, disc;

	thumb_name(stem, tn, sizeof(tn));
	norm_title(stem, norm, sizeof(norm), &disc);
	region_of(stem, want, sizeof(want));
	if (!norm[0])
		return NULL;
	lo = index_lower(ix, norm);
	for (int i = lo; i < ix->n && !strcmp(ix->e[i].norm, norm); i++) {
		int rank;

		if (!strcmp(ix->e[i].name, tn))
			return ix->e[i].name;             /* the exact No-Intro name */
		rank = entry_rank(&ix->e[i], want, disc);
		if (rank < best_rank) {
			best_rank = rank;
			best = ix->e[i].name;
		}
	}
	if (best)
		return best;
	/* a shortened title ("Donkey Kong Country 2" for "Donkey Kong Country 2
	 * - Diddy's Kong Quest"): the shortest title that starts with it, for
	 * titles long enough not to be ambiguous */
	if (strlen(norm) >= 8) {
		size_t nl = strlen(norm), best_len = (size_t)-1;

		for (int i = lo; i < ix->n && !strncmp(ix->e[i].norm, norm, nl); i++) {
			size_t len = strlen(ix->e[i].norm);
			int rank = entry_rank(&ix->e[i], want, disc);

			if (len < best_len || (len == best_len && rank < best_rank)) {
				best_len = len;
				best_rank = rank;
				best = ix->e[i].name;
			}
		}
	}
	return best;
}

/* How close an entry is to the query, 0-1100: the share of words both have
 * (Dice: 2 x common / all; a word of 4 letters or more that starts the
 * other counts half: "zelda" and "zeldas"), 1100 for the same key, a bonus
 * when the entry's key starts with the query's. */
static int similarity(const struct words *q, const char *qnorm, const struct entry *e)
{
	struct words ew;
	bool used[MAX_WORDS] = { false };
	int c2 = 0, score;

	if (!strcmp(qnorm, e->norm))
		return 1100;
	words_of(e->name, &ew, NULL);
	if (!q->n || !ew.n)
		return 0;
	for (int i = 0; i < q->n; i++) {
		int hit = -1, pts = 0;
		size_t lq = strlen(q->w[i]);

		for (int j = 0; j < ew.n && pts < 2; j++) {
			size_t le = strlen(ew.w[j]);

			if (used[j])
				continue;
			if (!strcmp(q->w[i], ew.w[j])) {
				hit = j;
				pts = 2;
			} else if (!pts && lq >= 4 && le >= 4 && !strncmp(q->w[i], ew.w[j], lq < le ? lq : le)) {
				hit = j;
				pts = 1;
			}
		}
		if (hit >= 0) {
			used[hit] = true;
			c2 += pts;
		}
	}
	score = 1000 * c2 / (q->n + ew.n);
	if (score && qnorm[0] && !strncmp(e->norm, qnorm, strlen(qnorm)))
		score += 50;
	return score;
}

/* --suggest: the SUGGEST_MAX best names for query into out */
static int suggest(const struct index *ix, int kind, const char *query, const char *out)
{
	int best[SUGGEST_MAX], score[SUGGEST_MAX], rank[SUGGEST_MAX], nb = 0, disc;
	char qnorm[512], want[128], tmp[1200];
	struct words q;
	FILE *f;

	words_of(query, &q, &disc);
	norm_title(query, qnorm, sizeof(qnorm), NULL);
	region_of(query, want, sizeof(want));
	for (int i = 0; i < ix->n; i++) {
		int sc = similarity(&q, qnorm, &ix->e[i]), rk, k;

		if (sc < 200)
			continue;
		rk = entry_rank(&ix->e[i], want, disc);
		/* the place in the sorted list: higher score, then better rank, then the shorter name */
		for (k = nb; k > 0; k--) {
			const struct entry *o = &ix->e[best[k - 1]];

			if (score[k - 1] > sc || (score[k - 1] == sc && (rank[k - 1] < rk ||
			    (rank[k - 1] == rk && strlen(o->name) <= strlen(ix->e[i].name)))))
				break;
		}
		if (k >= SUGGEST_MAX)
			continue;
		if (nb < SUGGEST_MAX)
			nb++;
		memmove(best + k + 1, best + k, sizeof(int) * (size_t)(nb - 1 - k));
		memmove(score + k + 1, score + k, sizeof(int) * (size_t)(nb - 1 - k));
		memmove(rank + k + 1, rank + k, sizeof(int) * (size_t)(nb - 1 - k));
		best[k] = i;
		score[k] = sc;
		rank[k] = rk;
	}
	snprintf(tmp, sizeof(tmp), "%s.tmp", out);
	f = fopen(tmp, "w");
	if (!f)
		return -1;
	for (int k = 0; k < nb; k++)
		fprintf(f, "%s\t%s\n", g_kind_name[kind], ix->e[best[k]].name);
	if (fclose(f) != 0 || rename(tmp, out) != 0) {
		unlink(tmp);
		return -1;
	}
	return nb;
}

/* The box art NAME of a system, scaled to fit max, as a JPEG at out (a
 * .tmp beside it, then renamed). 0, or -1 (*ep, err). */
static int fetch_cover(const char *lr, int kind, const char *name, const char *out, int max, enum rsu_err *ep,
		       char *err, size_t errn)
{
	struct http_opts o;
	struct http_resp r;
	struct membuf m = { .max = IMAGE_MAX };
	enum rsu_err e = RSU_OK;
	char enc[1600], lre[600], url[2400], tmp[1800];
	int rc = -1;

	url_encode(name, enc, sizeof(enc));
	url_encode(lr, lre, sizeof(lre));
	snprintf(url, sizeof(url), SERVER "/%s/%s/%s.png", lre, g_kind_dir[kind], enc);
	http_opts_init(&o);
	/* the server is slow at times: one more try */
	for (int attempt = 0; attempt < 2 && !g_cancel; attempt++) {
		m.len = 0;
		e = http_get(&o, url, 0, -1, NULL, mem_data, &m, &r, err, errn);
		if (!e)
			break;
	}
	snprintf(tmp, sizeof(tmp), "%s.tmp", out);
	if (e || !m.b || covers_img_write(m.b, m.len, max, 85, tmp) < 0) {
		if (!e)
			snprintf(err, errn, "not an image");
		unlink(tmp);
	} else if (rename(tmp, out) == 0) {
		rc = 0;
	} else {
		snprintf(err, errn, "%s", strerror(errno));
		unlink(tmp);
	}
	free(m.b);
	*ep = e;
	return rc;
}

/* ----------------------------------------------------------------- main */
struct game {
	char *system, *root, *path;
};

static const char *libretro_name(const char *folder)
{
	for (size_t i = 0; i < sizeof(g_systems) / sizeof(g_systems[0]); i++)
		if (!strcmp(g_systems[i].folder, folder))
			return g_systems[i].libretro;
	return NULL;
}

static int mkdir_p(const char *dir)
{
	char p[1100];

	snprintf(p, sizeof(p), "%s", dir);
	for (char *s = p + 1; *s; s++)
		if (*s == '/') {
			*s = 0;
			mkdir(p, 0755);
			*s = '/';
		}
	return mkdir(p, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static void stem_of(const char *path, char *out, size_t n)
{
	const char *b = strrchr(path, '/');
	char *dot;

	snprintf(out, n, "%s", b ? b + 1 : path);
	dot = strrchr(out, '.');
	if (dot && dot != out)
		*dot = 0;
}

static const char *reason(enum rsu_err e)
{
	switch (e) {
	case RSU_E_CLOCK:
		return "clock";
	case RSU_E_TLS:
	case RSU_E_NOTLS:
		return "tls";
	default:
		return "network";
	}
}

static void http_opts_init(struct http_opts *o)
{
	memset(o, 0, sizeof(*o));
	o->timeout_ms = 60000;               /* the server can stall for a while */
	o->max_redirects = 5;
	o->user_agent = "RetroStoneOS-covers";
	o->cancel = &g_cancel;
}

/* --suggest and --get: one system, for the menu's Choose cover. Prints "ok
 * <count>" or "error <reason>" (network, clock, tls, system, write). */
static int one_system(const char *mode, int kind, const char *sys, const char *arg, const char *out, int max,
		      const char *cache_dir)
{
	const char *lr = libretro_name(sys);
	char err[512] = "", dir[1100], *slash;
	enum rsu_err e = RSU_OK;
	struct index ix = { 0 };
	int n;

	if (!lr) {
		printf("error system\n");
		return 1;
	}
	if (!strcmp(mode, "--get")) {
		snprintf(dir, sizeof(dir), "%s", out);
		slash = strrchr(dir, '/');
		if (slash) {
			*slash = 0;
			mkdir_p(dir);
		}
		if (fetch_cover(lr, kind, arg, out, max, &e, err, sizeof(err)) < 0) {
			fprintf(stderr, "rsos-covers: %s: %s\n", arg, err);
			printf("error %s\n", e ? reason(e) : "write");
			return 1;
		}
		printf("ok 1\n");
		return 0;
	}
	/* the kind asked for, else the other one */
	for (int k = 0; k < 2; k++, kind = !kind) {
		if (index_load(&ix, sys, lr, kind, cache_dir, &e, err, sizeof(err)) < 0) {
			fprintf(stderr, "rsos-covers: %s: %s\n", sys, err);
			printf("error %s\n", reason(e));
			return 1;
		}
		n = suggest(&ix, kind, arg, out);
		index_free(&ix);
		if (n != 0)
			break;
	}
	if (n < 0) {
		printf("error write\n");
		return 1;
	}
	printf("ok %d\n", n);
	return 0;
}

int main(int argc, char **argv)
{
	const char *missing_path = NULL, *req = NULL, *cache_dir = NULL, *mode = NULL;
	const char *margs[3] = { NULL, NULL, NULL };
	int kind = K_BOXARTS, replace = 0;
	int max = 400, ngames = 0, cap = 0, found = 0, missing = 0, failed = 0, done = 0, net_ok = 0;
	struct game *games = NULL;
	FILE *f, *mf = NULL;
	char line[4096];
	enum rsu_err last = RSU_OK;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--progress") && i + 1 < argc)
			g_progress = argv[++i];
		else if (!strcmp(argv[i], "--missing") && i + 1 < argc)
			missing_path = argv[++i];
		else if (!strcmp(argv[i], "--max") && i + 1 < argc)
			max = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--cache") && i + 1 < argc)
			cache_dir = argv[++i];
		else if (!strcmp(argv[i], "--kind") && i + 1 < argc)
			kind = !strcmp(argv[++i], "titles") ? K_TITLES : K_BOXARTS;
		else if (!strcmp(argv[i], "--replace"))
			replace = 1;
		else if ((!strcmp(argv[i], "--suggest") || !strcmp(argv[i], "--get")) && i + 3 < argc) {
			mode = argv[i];
			margs[0] = argv[++i];
			margs[1] = argv[++i];
			margs[2] = argv[++i];
		} else
			req = argv[i];
	}
	if ((!req && !mode) || max < 64) {
		fprintf(stderr, "usage: rsos-covers [--kind titles|boxarts] [--progress FILE] [--missing FILE] [--max PX]\n"
			"                   [--cache DIR] [--replace] REQUEST\n"
			"       rsos-covers [--kind K] [--cache DIR] --suggest SYSTEM QUERY OUT\n"
			"       rsos-covers [--kind K] [--max PX] --get SYSTEM NAME OUT\n");
		return 2;
	}
	signal(SIGTERM, on_term);
	signal(SIGINT, on_term);
	signal(SIGPIPE, SIG_IGN);
	if (mode)
		return one_system(mode, kind, margs[0], margs[1], margs[2], max, cache_dir);
	/* it may run in the background while a game plays: below the emulator */
	if (nice(10) < 0)
		fprintf(stderr, "rsos-covers: nice: %s\n", strerror(errno));

	f = fopen(req, "r");
	if (!f) {
		progress("error request");
		fprintf(stderr, "rsos-covers: %s: %s\n", req, strerror(errno));
		return 1;
	}
	while (fgets(line, sizeof(line), f)) {
		char *a = line, *b = strchr(a, '\t'), *c = b ? strchr(b + 1, '\t') : NULL;

		line[strcspn(line, "\r\n")] = 0;
		if (!b || !c)
			continue;
		*b++ = 0;
		*c++ = 0;
		if (ngames == cap) {
			cap = cap ? cap * 2 : 256;
			games = realloc(games, sizeof(*games) * (size_t)cap);
			if (!games)
				return 1;
		}
		games[ngames].system = strdup(a);
		games[ngames].root = strdup(b);
		games[ngames].path = strdup(c);
		ngames++;
	}
	fclose(f);
	if (missing_path)
		mf = fopen(missing_path, "w");
	progress("0 covers 0 %d -", ngames);

	/* the request is grouped by system (the menu writes it so) */
	for (int i = 0; i < ngames && !g_cancel;) {
		const char *sys = games[i].system, *lr = libretro_name(sys);
		struct index ix = { 0 }, fb = { 0 };
		int j = i, fb_tried = 0;

		while (j < ngames && !strcmp(games[j].system, sys))
			j++;
		if (!lr) {
			fprintf(stderr, "rsos-covers: %s: no libretro thumbnails (arcade names need a name list)\n", sys);
			for (int k = i; k < j; k++, done++) {
				missing++;
				if (mf)
					fprintf(mf, "%s\t%s\t(not supported for this system)\n", sys, games[k].path);
			}
			i = j;
			continue;
		}
		{
			char err[512];
			enum rsu_err e;

			progress("%d covers %d %d %s", ngames ? done * 100 / ngames : 0, done, ngames, sys);
			if (index_load(&ix, sys, lr, kind, cache_dir, &e, err, sizeof(err)) < 0) {
				fprintf(stderr, "rsos-covers: %s: listing: %s\n", sys, err);
				last = e;
				for (int k = i; k < j; k++, done++)
					failed++;
				i = j;
				continue;
			}
			net_ok = 1;
			fprintf(stderr, "rsos-covers: %s: %d covers on the server, %d games to match\n", sys, ix.n, j - i);
		}
		for (int k = i; k < j && !g_cancel; k++, done++) {
			char stem[512], dir[1100], out[1700], err[512];
			const char *name;
			struct statvfs vs;
			enum rsu_err e;
			int got = kind;

			stem_of(games[k].path, stem, sizeof(stem));
			name = index_match(&ix, stem);
			progress("%d covers %d %d %s", ngames ? done * 100 / ngames : 0, done, ngames, sys);
			/* not there in this kind: the other one (its listing once) */
			if (!name) {
				if (!fb_tried) {
					fb_tried = 1;
					if (index_load(&fb, sys, lr, !kind, cache_dir, &e, err, sizeof(err)) < 0)
						fprintf(stderr, "rsos-covers: %s: %s listing: %s\n", sys,
							g_kind_name[!kind], err);
				}
				if ((name = index_match(&fb, stem)))
					got = !kind;
			}
			if (!name) {
				missing++;
				if (mf)
					fprintf(mf, "%s\t%s\n", sys, games[k].path);
				continue;
			}
			snprintf(dir, sizeof(dir), "%s/media/covers", games[k].root);
			snprintf(out, sizeof(out), "%s/%s.jpg", dir, stem);
			if (!replace && access(out, F_OK) == 0) {
				found++;                      /* there already */
				continue;
			}
			if (mkdir_p(dir) < 0 || (statvfs(dir, &vs) == 0 && (uint64_t)vs.f_bavail * vs.f_frsize < 8u << 20)) {
				progress("error nospace");
				fprintf(stderr, "rsos-covers: %s: no room\n", dir);
				index_free(&ix);
				index_free(&fb);
				if (mf)
					fclose(mf);
				return 1;
			}
			if (fetch_cover(lr, got, name, out, max, &e, err, sizeof(err)) < 0) {
				if (e)
					last = e;
				fprintf(stderr, "rsos-covers: %s: %s\n", name, err);
				failed++;
			} else {
				found++;
			}
		}
		index_free(&ix);
		index_free(&fb);
		i = j;
	}
	if (mf)
		fclose(mf);
	if (!net_ok && failed) {
		progress("error %s", reason(last));
		fprintf(stderr, "rsos-covers: nothing could be downloaded (%s)\n", reason(last));
		return 1;
	}
	progress("100 done %d %d %d", found, missing, failed);
	printf("found %d missing %d failed %d\n", found, missing, failed);
	return 0;
}
