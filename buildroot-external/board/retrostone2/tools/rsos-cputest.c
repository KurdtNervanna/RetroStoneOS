/*
 * rsos-cputest - a quick CPU check for the faster CPU trial (Settings > Power
 * > CPU speed; rsos-cpuspeed verify runs it once per new level):
 *
 *   rsos-cputest [seconds]      (20)
 *
 * Both cores compute the same mix of integer, multiply and floating point
 * work over a buffer that stays in the cache, again and again, at full load
 * (so the CPU runs at its top step): every result must equal the first one.
 * A CPU clocked beyond what its voltage carries miscalculates before it
 * freezes. Exit 0 when every round agreed, 1 on a mismatch (printed), 2 when
 * the threads cannot start.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define WORDS (16 * 1024)        /* 64 KiB per core */

static double g_seconds = 20;
static volatile int g_bad;
static uint64_t g_ref;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_have_ref;

static double now(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static uint64_t round_once(uint32_t *buf)
{
	uint32_t x = 0x2545f491u;
	uint64_t acc = 0;
	double f = 1.0;

	for (int i = 0; i < WORDS; i++) {
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		buf[i] = x;
	}
	for (int pass = 0; pass < 8; pass++) {
		for (int i = 0; i < WORDS; i++) {
			uint32_t v = buf[i] * 2654435761u + buf[(i * 7 + pass) & (WORDS - 1)];

			buf[i] = (v << 7) | (v >> 25);
			acc += (uint64_t)v * (uint64_t)(buf[(i + 1) & (WORDS - 1)] | 1u);
			f = f * 1.0000001 + (double)(v & 0xffff) * 1e-9;
		}
	}
	for (int i = 0; i < WORDS; i++)
		acc ^= (uint64_t)buf[i] << (i & 31);
	return acc ^ (uint64_t)(f * 1e6);
}

static void *worker(void *arg)
{
	uint32_t *buf = malloc(WORDS * sizeof(uint32_t));
	double end = now() + g_seconds;
	long rounds = 0;

	(void)arg;
	if (!buf) {
		g_bad = 1;
		return NULL;
	}
	while (!g_bad && now() < end) {
		uint64_t r = round_once(buf);

		pthread_mutex_lock(&g_mu);
		if (!g_have_ref) {
			g_ref = r;
			g_have_ref = 1;
		} else if (r != g_ref) {
			printf("rsos-cputest: round %ld: %016llx, expected %016llx\n", rounds,
			       (unsigned long long)r, (unsigned long long)g_ref);
			g_bad = 1;
		}
		pthread_mutex_unlock(&g_mu);
		rounds++;
	}
	free(buf);
	return (void *)rounds;
}

int main(int argc, char **argv)
{
	pthread_t t[2];
	void *r0 = NULL, *r1 = NULL;
	FILE *f;
	long khz = 0;

	if (argc > 1)
		g_seconds = atof(argv[1]);
	if (g_seconds <= 0 || g_seconds > 600)
		return 2;
	if (pthread_create(&t[0], NULL, worker, NULL) || pthread_create(&t[1], NULL, worker, NULL))
		return 2;
	/* the clock at full load, for the log */
	struct timespec half = { (time_t)(g_seconds / 2), 0 };
	nanosleep(&half, NULL);
	f = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "r");
	if (f) {
		if (fscanf(f, "%ld", &khz) != 1)
			khz = 0;
		fclose(f);
	}
	pthread_join(t[0], &r0);
	pthread_join(t[1], &r1);
	if (g_bad)
		return 1;
	printf("rsos-cputest: %.0f s, %ld + %ld rounds at %ld MHz: no error\n", g_seconds, (long)r0, (long)r1,
	       khz / 1000);
	return 0;
}
