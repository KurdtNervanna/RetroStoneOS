/*
 * controller_rsos.c - the RetroStone2's built-in controls for sm64-port
 * (RetroStoneOS native games, ports/sm64 in the RetroStoneOS fork).
 *
 * The console's controls are three evdev devices: "RetroStone2 Buttons"
 * (face buttons, D-pad, Start/Select), "RetroStone2 Shoulder Buttons" (L1,
 * R1, L2, R2) and the optional analog stick ("analog-stick"). They are read
 * here together, straight from /dev/input; controller_sdl leaves them
 * alone (it would see three separate pads) and keeps USB pads.
 *
 * N64 mapping (same labels where the console has them):
 *   A -> A          B -> B           Start -> Start
 *   L1, L2, Z -> Z  R1 -> R
 *   X -> C-up       Y -> C-left      C -> C-right    R2 -> C-down
 *   D-pad -> the stick (when the analog stick is centred or not fitted)
 * Select is left to the menu: Select+Start held quits the game.
 */
#if defined(__linux__) && defined(TARGET_LINUX_KMSDRM)

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <ultra64.h>

#include "controller_api.h"
#include "controller_rsos.h"

#define MAX_DEVS 8
#define STICK_DEADZONE 0.15f    /* of the half range */

struct rsos_dev {
    int fd;
    bool has_stick;
    struct input_absinfo ax, ay;
};

static struct rsos_dev devs[MAX_DEVS];
static int ndevs;
static bool keys[KEY_MAX + 1];
static int abs_x, abs_y;

static void controller_rsos_init(void) {
    DIR *d = opendir("/dev/input");
    struct dirent *de;

    if (d == NULL) {
        return;
    }
    while ((de = readdir(d)) != NULL && ndevs < MAX_DEVS) {
        char path[300], name[128] = "";
        unsigned long absbits[(ABS_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = { 0 };
        int fd;

        if (strncmp(de->d_name, "event", 5) != 0) {
            continue;
        }
        snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        /* the buttons are "RetroStone2 ...", the stick (adc-joystick) is
         * named after its device tree node, as in the menu's board.ini */
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0 ||
            (strcmp(name, "analog-stick") != 0 &&
             (strncmp(name, "RetroStone2", 11) != 0 || strstr(name, "Brightness") != NULL))) {
            close(fd);
            continue;
        }
        devs[ndevs].fd = fd;
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);
        if ((absbits[ABS_X / (8 * sizeof(long))] >> (ABS_X % (8 * sizeof(long)))) & 1) {
            devs[ndevs].has_stick = ioctl(fd, EVIOCGABS(ABS_X), &devs[ndevs].ax) == 0 &&
                                    ioctl(fd, EVIOCGABS(ABS_Y), &devs[ndevs].ay) == 0 &&
                                    devs[ndevs].ax.maximum != devs[ndevs].ax.minimum &&
                                    devs[ndevs].ay.maximum != devs[ndevs].ay.minimum;
            if (devs[ndevs].has_stick) {
                abs_x = devs[ndevs].ax.value;
                abs_y = devs[ndevs].ay.value;
            }
        }
        fprintf(stderr, "controller_rsos: %s (%s)%s\n", path, name,
                devs[ndevs].has_stick ? ", analog stick" : "");
        ndevs++;
    }
    closedir(d);
}

static bool axis_in_range(int v, const struct input_absinfo *a, bool near_centre) {
    int lo = a->minimum < a->maximum ? a->minimum : a->maximum;
    int hi = a->minimum < a->maximum ? a->maximum : a->minimum;

    if (near_centre) {
        return abs(v - (lo + (hi - lo) / 2)) <= (hi - lo) / 4;
    }
    return v >= lo && v <= hi;
}

/*
 * Fitted or not, as the menu decides (input.c): a reading out of range means
 * no stick (the ADC inputs float above it); fitted again only once both axes
 * read near the centre (floating inputs can wander into the range near an
 * edge and would hold a direction).
 */
static bool stick_fitted(const struct rsos_dev *sd) {
    static bool fitted;

    if (!axis_in_range(abs_x, &sd->ax, false) || !axis_in_range(abs_y, &sd->ay, false)) {
        if (fitted) {
            fprintf(stderr, "controller_rsos: analog stick reads %d, %d: not fitted\n", abs_x, abs_y);
        }
        fitted = false;
    } else if (!fitted && axis_in_range(abs_x, &sd->ax, true) && axis_in_range(abs_y, &sd->ay, true)) {
        fprintf(stderr, "controller_rsos: analog stick fitted (%d, %d)\n", abs_x, abs_y);
        fitted = true;
    }
    return fitted;
}

/* -1..1 from an axis, 0 in the dead zone */
static float stick_axis(int v, const struct input_absinfo *a) {
    int lo = a->minimum < a->maximum ? a->minimum : a->maximum;
    int hi = a->minimum < a->maximum ? a->maximum : a->minimum;
    float half = (hi - lo) / 2.0f, f;

    if (half <= 0) {
        return 0;
    }
    f = (v - (lo + half)) / half;
    if (f > -STICK_DEADZONE && f < STICK_DEADZONE) {
        return 0;
    }
    return f;
}

static void controller_rsos_read(OSContPad *pad) {
    const struct rsos_dev *sd = NULL;
    struct input_event ev;
    int dx, dy;

    for (int i = 0; i < ndevs; i++) {
        while (read(devs[i].fd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_KEY && ev.code <= KEY_MAX) {
                keys[ev.code] = ev.value != 0;
            } else if (ev.type == EV_ABS && ev.code == ABS_X) {
                abs_x = ev.value;
            } else if (ev.type == EV_ABS && ev.code == ABS_Y) {
                abs_y = ev.value;
            }
        }
        if (devs[i].has_stick) {
            sd = &devs[i];
        }
    }

    if (keys[BTN_START]) pad->button |= START_BUTTON;
    if (keys[BTN_EAST]) pad->button |= A_BUTTON;
    if (keys[BTN_SOUTH]) pad->button |= B_BUTTON;
    if (keys[BTN_TL] || keys[BTN_TL2] || keys[BTN_Z]) pad->button |= Z_TRIG;
    if (keys[BTN_TR]) pad->button |= R_TRIG;
    if (keys[BTN_NORTH]) pad->button |= U_CBUTTONS;
    if (keys[BTN_WEST]) pad->button |= L_CBUTTONS;
    if (keys[BTN_C]) pad->button |= R_CBUTTONS;
    if (keys[BTN_TR2]) pad->button |= D_CBUTTONS;

    /* the analog stick, when fitted and moved */
    if (sd != NULL && stick_fitted(sd)) {
        float fx = stick_axis(abs_x, &sd->ax), fy = stick_axis(abs_y, &sd->ay);

        if (fx != 0 || fy != 0) {
            int sx = (int)(fx * 80), sy = (int)(-fy * 80);

            if (abs(sx) > abs(pad->stick_x) || abs(sy) > abs(pad->stick_y)) {
                pad->stick_x = sx;
                pad->stick_y = sy;
            }
            return;
        }
    }
    /* else the D-pad steers: full tilt, 56 on a diagonal (80 / sqrt 2) */
    dx = (keys[BTN_DPAD_RIGHT] ? 1 : 0) - (keys[BTN_DPAD_LEFT] ? 1 : 0);
    dy = (keys[BTN_DPAD_UP] ? 1 : 0) - (keys[BTN_DPAD_DOWN] ? 1 : 0);
    if (dx != 0 || dy != 0) {
        int m = dx != 0 && dy != 0 ? 56 : 80;

        pad->stick_x = dx * m;
        pad->stick_y = dy * m;
    }
}

struct ControllerAPI controller_rsos = {
    controller_rsos_init,
    controller_rsos_read
};

#endif
