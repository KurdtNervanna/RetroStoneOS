/* Exercise the real polling loop with a pipe as evdev and a state-query
 * mock. EVIOCGKEY flushes queued kernel key events, but cannot flush old
 * events already copied into input_poll's read buffer. No hardware needed. */
#include <stdarg.h>
#include <sys/ioctl.h>

static int state_ioctl(int fd, unsigned long request, ...);
#define ioctl state_ioctl
#include "../src/input/input.c"
#undef ioctl

static bool kernel_held;
static int queries, failures;

static int state_ioctl(int fd, unsigned long request, ...)
{
	va_list ap;
	unsigned long *keys;

	(void)fd;
	if (request != EVIOCGKEY(sizeof(unsigned long) * NBITS(KEY_CNT))) {
		errno = ENOTTY;
		return -1;
	}
	va_start(ap, request);
	keys = va_arg(ap, unsigned long *);
	va_end(ap);
	memset(keys, 0, sizeof(unsigned long) * NBITS(KEY_CNT));
	if (kernel_held)
		keys[BTN_EAST / BITS_PER_LONG_] |= 1ul << (BTN_EAST % BITS_PER_LONG_);
	queries++;
	return 0;
}

#define CHECK(c, label) do { printf("  %s: %s\n", (c) ? "ok" : "FAIL", label); \
	if (!(c)) failures++; } while (0)

static void overflow(bool held)
{
	struct input in = { 0 };
	struct dev d = { 0 }, shoulder = { 0 };
	struct input_event events[] = {
		{ .type = EV_SYN, .code = SYN_DROPPED },
		{ .type = EV_KEY, .code = BTN_SOUTH, .value = 1 }, /* incomplete frame */
		{ .type = EV_SYN, .code = SYN_REPORT },
		{ .type = EV_KEY, .code = BTN_EAST, .value = !held }, /* older than ioctl */
		{ .type = EV_SYN, .code = SYN_REPORT },
	};
	struct epoll_event watch = { .events = EPOLLIN, .data.u32 = 0 };
	int fds[2];

	if (pipe2(fds, O_NONBLOCK | O_CLOEXEC) < 0 || (in.epfd = epoll_create1(EPOLL_CLOEXEC)) < 0)
		exit(2);
	input_config_defaults(&in.cfg);
	in.cap_slot = -1;
	in.last_src = -3;
	in.builtin_present = true;
	for (int p = 0; p < INPUT_MAX_PORTS; p++) in.port_dev[p] = -2;
	in.port_dev[0] = -1;
	for (int b = 0; b < IN_NUM_BUTTONS; b++) in.remap[b] = (uint8_t)b;
	map_reset(&d);
	d.fd = fds[0];
	d.pad = &in.builtin;
	d.builtin = true;
	d.key_map[BTN_EAST] = IN_A;
	d.key_map[BTN_SOUTH] = IN_B;
	in.devs[0] = &d;
	/* A separate built-in device (the shoulder buttons) shares the pad. */
	shoulder.pad = &in.builtin;
	shoulder.builtin = true;
	in.devs[1] = &shoulder;
	in.builtin.cnt[IN_L] = 1;
	epoll_ctl(in.epfd, EPOLL_CTL_ADD, fds[0], &watch);
	kernel_held = held;
	queries = 0;
	if (write(fds[1], events, sizeof(events)) != sizeof(events)) exit(2);
	input_poll(&in);
	CHECK(queries == 1, "overflow queries the kernel's state");
	CHECK(!!(input_port_buttons(&in, 0) & BTN_MASK(IN_A)) == held,
	      held ? "an old release cannot cancel a real hold" : "an old press cannot stick a released button");
	CHECK(!(input_port_buttons(&in, 0) & BTN_MASK(IN_B)), "incomplete events are discarded");
	CHECK(input_port_buttons(&in, 0) & BTN_MASK(IN_L), "the other built-in device keeps its held button");
	CHECK(!!(in.nav_state & BTN_MASK(IN_A)) == held, "menu and game agree after recovery");
	/* Events arriving after the state query must still be processed. */
	events[0] = (struct input_event){ .type = EV_KEY, .code = BTN_EAST, .value = !held };
	events[1] = (struct input_event){ .type = EV_SYN, .code = SYN_REPORT };
	if (write(fds[1], events, 2 * sizeof(events[0])) != 2 * sizeof(events[0])) exit(2);
	input_poll(&in);
	CHECK(!!(input_port_buttons(&in, 0) & BTN_MASK(IN_A)) != held, "fresh input still works after recovery");
	close(fds[0]); close(fds[1]); close(in.epfd);
}

int main(void)
{
	puts("input: overflow recovery with stale events in the read buffer");
	overflow(false);
	overflow(true);
	return failures ? 1 : 0;
}
