/*
 * Copyright (c) 2016, Citrix Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2.1 only
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/file.h>

#include "tap-ctl.h"
#include "blktap.h"

int
tap_ctl_free(const int minor)
{
	char *path = NULL;
	int mfd = -1, fd, err;

	fd = open(BLKTAP2_NP_RUN_DIR, O_RDONLY);
	if (fd == -1) {
		err = -errno;
		EPRINTF("Failed to open runtime directory %d\n", errno);
		return err;
	}

	/* The only way this can fail is with an EINTR or ENOLCK*/
	err = flock(fd, LOCK_EX);
	if (err == -1) {
		err = -errno;
		EPRINTF("Failed to lock runtime directory %d\n", errno);
		goto out;
	}

	err = asprintf(&path, "%s/tapdisk-%d", BLKTAP2_NP_RUN_DIR, minor);
	if (err == -1) {
		err = -errno;
		goto out;
	}
	err = 0;

	/* Non-Blocking lock to check it's not in use */
	mfd = open(path, O_RDONLY);
	if (mfd == -1) {
		err = -errno;
		EPRINTF("Failed to open marker file %s, %d, err=%d\n",
			path, minor, errno);
		goto out;
	}

	err = flock(mfd, LOCK_EX | LOCK_NB);
	if (err == -1) {
		err = -errno;
		EPRINTF("Unable to lock marker file %s, err = %d\n",
			path, errno);
		goto out;
	}

	unlink(path);

out:
	if (path)
		free(path);

	if (mfd != -1) {
		flock(mfd, LOCK_UN);
		close(mfd);
	}

	flock(fd, LOCK_UN);
	close(fd);
	return err;
}
