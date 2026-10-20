/*
 * rsos-memtest - a quick DRAM check for the faster memory trial (Settings >
 * Power > Memory speed; rsos-boot-ok runs it before it confirms a trial):
 *
 *   rsos-memtest [MiB] [passes]      (96 MiB, 2 passes)
 *
 * Writes, then reads back, over one buffer: every word's own address, its
 * inverse, walking ones, and a pseudo-random sequence. A DRAM clocked beyond
 * what the board's chips and wiring take shows up as flipped bits. Exit 0
 * when everything read back, 1 on the first error (printed), 2 when the
 * buffer cannot be had.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

static uint32_t lfsr(uint32_t x)
{
	return (x >> 1) ^ (-(x & 1u) & 0xd0000001u);
}

static int check(volatile uint32_t *p, size_t n, int pattern, int pass)
{
	uint32_t r = 0x13579bdfu + (uint32_t)pass;
	size_t i;

	for (i = 0; i < n; i++) {
		uint32_t v;

		switch (pattern) {
		case 0: v = (uint32_t)(uintptr_t)&p[i]; break;
		case 1: v = ~(uint32_t)(uintptr_t)&p[i]; break;
		case 2: v = 1u << (i & 31); break;
		default: r = lfsr(r); v = r; break;
		}
		p[i] = v;
	}
	r = 0x13579bdfu + (uint32_t)pass;
	for (i = 0; i < n; i++) {
		uint32_t v, got = p[i];

		switch (pattern) {
		case 0: v = (uint32_t)(uintptr_t)&p[i]; break;
		case 1: v = ~(uint32_t)(uintptr_t)&p[i]; break;
		case 2: v = 1u << (i & 31); break;
		default: r = lfsr(r); v = r; break;
		}
		if (got != v) {
			printf("rsos-memtest: pass %d pattern %d word %zu: wrote %08x, read %08x\n",
			       pass, pattern, i, (unsigned)v, (unsigned)got);
			return 1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	size_t mib = argc > 1 ? (size_t)atoi(argv[1]) : 96, n;
	int passes = argc > 2 ? atoi(argv[2]) : 2;
	volatile uint32_t *p;

	if (mib < 1 || mib > 512 || passes < 1)
		return 2;
	n = mib * 1024 * 1024 / sizeof(uint32_t);
	p = mmap(NULL, n * sizeof(uint32_t), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		return 2;
	for (int pass = 0; pass < passes; pass++)
		for (int pattern = 0; pattern < 4; pattern++)
			if (check(p, n, pattern, pass))
				return 1;
	printf("rsos-memtest: %zu MiB, %d passes: no error\n", mib, passes);
	munmap((void *)p, n * sizeof(uint32_t));
	return 0;
}
