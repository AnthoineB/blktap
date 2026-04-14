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

#include "unity.h"
#include <stdlib.h>

/* Header file for SUT */
#include "drivers/block-aio.h"

/* Mocks */
#include "mock_tapdisk-interface.h"
#include "mock_tapdisk-stats.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_tdaio_queue_read_computes_size_and_offset_correctly(void)
{
    // Initialisation
    td_driver_t driver;
    td_request_t treq;

    int expected_size;
    uint64_t expected_offset;
    struct aio_request aio;
    struct tdaio_state prv;

    driver.data = &prv;
    treq.secs = 10;
    driver.info.sector_size = 2048;
    treq.sec = (uint64_t) 23;

    prv.aio_free_count = 1;

    prv.aio_free_list[0] = &aio;

    // Expectations
    expected_size = treq.secs * SECTOR_SIZE;
    expected_offset = treq.sec * (uint64_t) SECTOR_SIZE;

    td_prep_read_Expect(
        &aio.tiocb,
        prv.fd,
        treq.buf,
        expected_size,
        expected_offset,
        tdaio_complete,
        &aio);

    td_queue_tiocb_Ignore();

    // Call to the method to test
    tdaio_queue_read(&driver, treq);
}
