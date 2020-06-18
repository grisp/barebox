/*
 * show_progress.c - simple progress bar functions
 *
 * Copyright (c) 2010 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <common.h>
#include <fs.h>
#include <progress.h>
#include <asm-generic/div64.h>

#define HASHES_PER_LINE	65

static int printed;
static int progress_max;
static int shift;
static int spin;

void show_progress(loff_t now)
{
	char spinchr[] = "\\|/-";

	if (now < 0) {
		printf("%c\b", spinchr[spin++ % (sizeof(spinchr) - 1)]);
		return;
	}

	now = now >> shift;

	if (progress_max) {
		uint64_t tmp = now * HASHES_PER_LINE;
		do_div(tmp, progress_max);
		now = tmp;
	}

	while (printed < now) {
		if (!(printed % HASHES_PER_LINE) && printed)
			printf("\n\t");
		printf("#");
		printed++;
	}
}

void init_progression_bar(loff_t max)
{
	printed = 0;
	spin = 0;

	if (max == FILESIZE_MAX)
		max = 0;

	shift = fls64(max) - 32;

	if (shift > 0)
		max = max >> shift;
	else
		shift = 0;

	progress_max = max;
	if (progress_max)
		printf("\t[%*s]\r\t[", HASHES_PER_LINE, "");
	else
		printf("\t");
}
