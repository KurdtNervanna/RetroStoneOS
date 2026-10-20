/*
 * rsos-covers - game covers from the libretro thumbnail server
 * (thumbnails.libretro.com, the one RetroArch's thumbnail updater uses), for
 * Settings > Game lists > Download game covers.
 *
 *   rsos-covers [--progress FILE] [--missing FILE] [--max PX] REQUEST
 *
 * REQUEST (written by the menu, ui/storage.c): one game per line,
 *   <system folder>\t<the system's ROM folder>\t<ROM path>
 * for the games that have no image yet. Per system with a libretro name: the
 * server's Named_Boxarts listing once (about 1 MB), then per game the best
 * name: the ROM's file name itself (No-Intro names match exactly), else the
 * same title without its (tags), the ROM's own region first, then USA,
 * World, Europe, Japan. The cover is scaled to fit --max (400) and saved as
 * <ROM folder>/media/covers/<file name without extension>.jpg, which the menu
 * shows for games whose gamelist.xml gives no image (ui/games.c). Nothing
 * else is written: no gamelist.xml, no existing cover.
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "covers_img.h"
#include "http.h"

#define SERVER "https://thumbnails.libretro.com"
#define INDEX_MAX (8u * 1024 * 1024)
#define IMAGE_MAX (8u * 1024 * 1024)

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

/* The title without (tags) and [tags], lower case, letters and digits only:
 * "Super Mario World (USA) [!]" and "super mario world" -> "supermarioworld" */
static void norm_title(const char *s, char *out, size_t n)
{
	size_t o = 0;
	int depth = 0;

	for (; *s && o + 1 < n; s++) {
		if (*s == '(' || *s == '[')
			depth++;
		else if ((*s == ')' || *s == ']') && depth)
			depth--;
		else if (!depth && isalnum((unsigned char)*s))
			out[o++] = (char)tolower((unsigned char)*s);
	}
	out[o] = 0;
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
			char norm[512];

			*dot = 0;
			url_decode(p);
			if (ix->n == cap) {
				cap = cap ? cap * 2 : 4096;
				ix->e = realloc(ix->e, sizeof(*ix->e) * (size_t)cap);
				if (!ix->e)
					return -1;
			}
			norm_title(p, norm, sizeof(norm));
			ix->e[ix->n].name = strdup(p);
			ix->e[ix->n].norm = strdup(norm);
			ix->n++;
		}
		p = q + 1;
	}
	qsort(ix->e, (size_t)ix->n, sizeof(*ix->e), cmp_norm);
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

/* The best cover name for a ROM file stem, NULL if none. */
static const char *index_match(const struct index *ix, const char *stem)
{
	char tn[512], norm[512], want[128];
	const char *best = NULL;
	int best_rank = 99, lo = 0, hi = ix->n;

	thumb_name(stem, tn, sizeof(tn));
	norm_title(stem, norm, sizeof(norm));
	region_of(stem, want, sizeof(want));
	if (!norm[0])
		return NULL;
	/* first entry with this title */
	while (lo < hi) {
		int mid = (lo + hi) / 2;

		if (strcmp(ix->e[mid].norm, norm) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	for (int i = lo; i < ix->n && !strcmp(ix->e[i].norm, norm); i++) {
		int rank;

		if (!strcmp(ix->e[i].name, tn))
			return ix->e[i].name;             /* the exact No-Intro name */
		rank = region_rank(ix->e[i].name, want);
		/* plain releases before (Beta), (Proto), (Sample)... */
		if (strstr(ix->e[i].name, "(Beta") || strstr(ix->e[i].name, "(Proto") ||
		    strstr(ix->e[i].name, "(Sample") || strstr(ix->e[i].name, "(Demo"))
			rank += 20;
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
			int rank = region_rank(ix->e[i].name, want);

			if (strstr(ix->e[i].name, "(Beta") || strstr(ix->e[i].name, "(Proto") ||
			    strstr(ix->e[i].name, "(Sample") || strstr(ix->e[i].name, "(Demo"))
				rank += 20;
			if (len < best_len || (len == best_len && rank < best_rank)) {
				best_len = len;
				best_rank = rank;
				best = ix->e[i].name;
			}
		}
	}
	return best;
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

int main(int argc, char **argv)
{
	const char *missing_path = NULL, *req = NULL;
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
		else
			req = argv[i];
	}
	if (!req || max < 64) {
		fprintf(stderr, "usage: rsos-covers [--progress FILE] [--missing FILE] [--max PX] REQUEST\n");
		return 2;
	}
	signal(SIGTERM, on_term);
	signal(SIGINT, on_term);
	signal(SIGPIPE, SIG_IGN);
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
		struct index ix = { 0 };
		int j = i;

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
			char enc[600], url[1024], err[512];
			struct http_opts o;
			struct http_resp r;
			struct membuf m = { .max = INDEX_MAX };
			enum rsu_err e;

			url_encode(lr, enc, sizeof(enc));
			snprintf(url, sizeof(url), SERVER "/%s/Named_Boxarts/", enc);
			progress("%d covers %d %d %s", ngames ? done * 100 / ngames : 0, done, ngames, sys);
			http_opts_init(&o);
			for (int attempt = 0; attempt < 2 && !g_cancel; attempt++) {
				m.len = 0;
				e = http_get(&o, url, 0, -1, NULL, mem_data, &m, &r, err, sizeof(err));
				if (!e)
					break;
				fprintf(stderr, "rsos-covers: %s: listing: %s%s\n", sys, err, attempt ? "" : ", again");
			}
			if (e || !m.b || index_parse(&ix, m.b) <= 0) {
				fprintf(stderr, "rsos-covers: %s: listing: %s\n", sys, e ? err : "empty");
				last = e ? e : RSU_E_NETWORK;
				free(m.b);
				for (int k = i; k < j; k++, done++)
					failed++;
				i = j;
				continue;
			}
			free(m.b);
			net_ok = 1;
			fprintf(stderr, "rsos-covers: %s: %d covers on the server, %d games to match\n", sys, ix.n, j - i);
		}
		for (int k = i; k < j && !g_cancel; k++, done++) {
			char stem[512], dir[1100], out[1700], enc[1600], url[2400], err[512];
			const char *name;
			struct statvfs vs;

			stem_of(games[k].path, stem, sizeof(stem));
			name = index_match(&ix, stem);
			progress("%d covers %d %d %s", ngames ? done * 100 / ngames : 0, done, ngames, sys);
			if (!name) {
				missing++;
				if (mf)
					fprintf(mf, "%s\t%s\n", sys, games[k].path);
				continue;
			}
			snprintf(dir, sizeof(dir), "%s/media/covers", games[k].root);
			snprintf(out, sizeof(out), "%s/%s.jpg", dir, stem);
			if (access(out, F_OK) == 0) {
				found++;                      /* there already */
				continue;
			}
			if (mkdir_p(dir) < 0 || (statvfs(dir, &vs) == 0 && (uint64_t)vs.f_bavail * vs.f_frsize < 8u << 20)) {
				progress("error nospace");
				fprintf(stderr, "rsos-covers: %s: no room\n", dir);
				index_free(&ix);
				if (mf)
					fclose(mf);
				return 1;
			}
			{
				struct http_opts o;
				struct http_resp r;
				struct membuf m = { .max = IMAGE_MAX };
				enum rsu_err e;
				char lre[600], tmp[1800];

				url_encode(name, enc, sizeof(enc));
				url_encode(lr, lre, sizeof(lre));
				snprintf(url, sizeof(url), SERVER "/%s/Named_Boxarts/%s.png", lre, enc);
				http_opts_init(&o);
				/* the server is slow at times: one more try */
				for (int attempt = 0; attempt < 2 && !g_cancel; attempt++) {
					m.len = 0;
					e = http_get(&o, url, 0, -1, NULL, mem_data, &m, &r, err, sizeof(err));
					if (!e)
						break;
				}
				snprintf(tmp, sizeof(tmp), "%s.tmp", out);
				if (e || !m.b || covers_img_write(m.b, m.len, max, 85, tmp) < 0) {
					if (e)
						last = e;
					fprintf(stderr, "rsos-covers: %s: %s\n", name, e ? err : "not an image");
					failed++;
					unlink(tmp);
				} else if (rename(tmp, out) == 0) {
					found++;
				} else {
					failed++;
					unlink(tmp);
				}
				free(m.b);
			}
		}
		index_free(&ix);
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
