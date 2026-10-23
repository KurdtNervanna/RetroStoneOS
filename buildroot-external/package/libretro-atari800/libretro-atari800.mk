################################################################################
#
# libretro-atari800
#
################################################################################

# libretro/libretro-atari800 master, 2026-09-01: Atari800 7.0.0 (the
# repository has no release tags). No submodules: zlib and libretro-common
# are in the tree.
LIBRETRO_ATARI800_VERSION = 4e7fbc73765c1a9670c7506616046ad1d4ccda51
LIBRETRO_ATARI800_SITE = $(call github,libretro,libretro-atari800,$(LIBRETRO_ATARI800_VERSION))
# The Altirra OS, 5200 OS and BASIC replacement ROMs compiled into the core
# (atari800/src/roms/altirra*.c) carry Avery Lee's all-permissive notice.
LIBRETRO_ATARI800_LICENSE = GPL-2.0+ (Atari800, libretro port), all-permissive (Altirra ROMs), Zlib (zlib)
LIBRETRO_ATARI800_LICENSE_FILES = atari800/COPYING

# platform=unix everywhere: the Makefile's "rpi" branch only repeats the
# shared-library flags of "unix" and has no CPU flags; they come from the
# toolchain wrapper. -O2. Plain C, no asm, no dynarec. GIT_VERSION= keeps
# "git rev-parse" (of whatever tree the build directory is in) out of the
# version string. download.c (the ROM download of the standalone emulator)
# is not in the libretro source list.
LIBRETRO_ATARI800_PLATFORM = $(call rsos-libretro-select,default=unix)

LIBRETRO_ATARI800_MAKE_OPTS = \
	platform=$(LIBRETRO_ATARI800_PLATFORM) \
	GIT_VERSION= \
	CC="$(TARGET_CC)" \
	CXX="$(TARGET_CXX)" \
	AR="$(TARGET_AR)"

define LIBRETRO_ATARI800_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(TARGET_CONFIGURE_OPTS) \
		$(MAKE) -C $(@D) -f Makefile $(LIBRETRO_ATARI800_MAKE_OPTS)
endef

define LIBRETRO_ATARI800_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/atari800_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/atari800_libretro.so
	$(INSTALL) -D -m 0644 $(LIBRETRO_ATARI800_PKGDIR)/atari800.ini \
		$(TARGET_DIR)/usr/share/rsos/cores/atari800.ini
endef

$(eval $(generic-package))
