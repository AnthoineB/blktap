/*
 * Copyright (c) 2016, Citrix Systems, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "vhd.h"
#include "io-util.h"

/*
 * Positioned atomic I/O: repeatedly call the positioned read/write primitive (e.g. pread/pwrite)
 * until all n bytes at offset off have been transferred, retrying short transfers and
 * EINTR/EAGAIN. Stops early at EOF.
 * Returns the number of bytes transferred, or 0/-1 on error (errno set).
 */
ssize_t
vhd_atomic_pio(vhd_pio_t f, int fd, void *_s, size_t n, off_t off)
{
	char *s = _s;
	size_t pos = 0;
	ssize_t res;
	struct stat st;

	memset(&st, 0, sizeof(st));

	for (;;) {
		res = (f) (fd, s + pos, n - pos, off + pos);
		switch (res) {
		case -1:
			if (errno == EINTR || errno == EAGAIN)
				continue;
			else
				return 0;
			break;
		case 0:
			errno = EPIPE;
			return pos;
		}

		if (pos + res == n)
			return n;

		if (!st.st_size)
			if (fstat(fd, &st) == -1)
				return -1;

		if (off + pos + res == st.st_size)
			return pos + res;

		pos += (res & ~(VHD_SECTOR_SIZE - 1));
	}

	return -1;
}

/*
 * Sequential atomic I/O: repeatedly call the read/write primitive f on the file's implicit
 * offset until all n bytes have been transferred, retrying short transfers and EINTR/EAGAIN.
 * Returns the number of bytes transferred; 0 on error (errno set), or the partial
 * count on EOF (errno == EPIPE).
 */
size_t
vhd_atomic_io(vhd_io_t f, int fd, void *_s, size_t n)
{
	char *s = _s;
	size_t pos = 0;
	ssize_t res;

	while (n > pos) {
		res = (f) (fd, s + pos, n - pos);
		switch (res) {
		case -1:
			if (errno == EINTR || errno == EAGAIN)
				continue;
			else
				return 0;
			break;
		case 0:
			errno = EPIPE;
			return pos;
		default:
			pos += (size_t)res;
		}
	}

	return pos;
}
