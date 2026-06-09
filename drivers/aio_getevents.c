/*
 * Copyright (c) 2026, Jens Axboe
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

#include "aio_getevents.h"

/* For user space reaping of completions */
struct aio_ring {
	unsigned id;		 /** kernel internal index number */
	unsigned nr;		 /** number of io_events */
	unsigned head;
	unsigned tail;

	unsigned magic;
	unsigned compat_features;
	unsigned incompat_features;
	unsigned header_length;	/** size of aio_ring */

	struct io_event events[0];
};

#define AIO_RING_MAGIC	0xa10a10a1

int user_io_getevents(io_context_t aio_ctx, unsigned int max, struct io_event *events)
{
	long i = 0;
	unsigned head;
	struct aio_ring *ring = (struct aio_ring*) aio_ctx;

	while (i < max) {
		head = ring->head;

		if (head == ring->tail) {
			/* There are no more completions */
			break;
		} else {
			/* There is another completion to reap */
			events[i] = ring->events[head];
			__atomic_store_n(&ring->head, (head + 1) % ring->nr, __ATOMIC_RELEASE);
			i++;
		}
	}

	return i;
}
