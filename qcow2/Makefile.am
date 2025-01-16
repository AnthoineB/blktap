
SUBDIRS = lib

AM_CFLAGS  = -Wall
AM_CFLAGS += -Werror
AM_CFLAGS += $(if $(GCOV),-fprofile-dir=/tmp/coverage/blktap/qcow2 -fprofile-arcs -ftest-coverage)

AM_CPPFLAGS  = -D_GNU_SOURCE
AM_CPPFLAGS += -I$(top_srcdir)/include
AM_CPPFLAGS += $(GLIB_CFLAGS)

bin_PROGRAMS = qemu-img

qemu_img_LDADD  = lib/libqcow2.la

qemu_img_SOURCES = qemu-img.c
qemu_img_SOURCES += qemu-config.c
qemu_img_SOURCES += qemu-progress.c

clean-local:
	-rm -rf *.gc??
