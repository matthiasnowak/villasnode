/** The internal datastructure for a sample of simulation data.
 *
 * @author Steffen Vogel <stvogel@eonerc.rwth-aachen.de>
 * @copyright 2017, Institute for Automation of Complex Power Systems, EONERC
 * @license GNU General Public License (version 3)
 *
 * VILLASnode
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *********************************************************************************/

#include <ctype.h>
#include <string.h>

#include "io.h"
#include "plugin.h"
#include "utils.h"
#include "timing.h"
#include "sample.h"
#include "formats/villas.h"

size_t io_format_villas_sprint_single(char *buf, size_t len, struct sample *s, int flags)
{
	size_t off = snprintf(buf, len, "%llu", (unsigned long long) s->ts.origin.tv_sec);

	if (flags & IO_FORMAT_NANOSECONDS)
		off += snprintf(buf + off, len - off, ".%09llu", (unsigned long long) s->ts.origin.tv_nsec);

	if (flags & IO_FORMAT_OFFSET)
		off += snprintf(buf + off, len - off, "%+e", time_delta(&s->ts.origin, &s->ts.received));

	if (flags & IO_FORMAT_SEQUENCE)
		off += snprintf(buf + off, len - off, "(%u)", s->sequence);

	if (flags & IO_FORMAT_VALUES) {
		for (int i = 0; i < s->length; i++) {
			switch ((s->format >> i) & 0x1) {
				case SAMPLE_DATA_FORMAT_FLOAT:
					off += snprintf(buf + off, len - off, "\t%.6f", s->data[i].f);
					break;
				case SAMPLE_DATA_FORMAT_INT:
					off += snprintf(buf + off, len - off, "\t%d", s->data[i].i);
					break;
			}
		}
	}

	off += snprintf(buf + off, len - off, "\n");

	return off;
}

size_t io_format_villas_sscan_single(const char *buf, size_t len, struct sample *s, int *flags)
{
	char *end;
	const char *ptr = buf;

	int fl = 0;
	double offset = 0;

	/* Format: Seconds.NanoSeconds+Offset(SequenceNumber) Value1 Value2 ...
	 * RegEx: (\d+(?:\.\d+)?)([-+]\d+(?:\.\d+)?(?:e[+-]?\d+)?)?(?:\((\d+)\))?
	 *
	 * Please note that only the seconds and at least one value are mandatory
	 */

	/* Mandatory: seconds */
	s->ts.origin.tv_sec = (uint32_t) strtoul(ptr, &end, 10);
	if (ptr == end)
		return -2;

	/* Optional: nano seconds */
	if (*end == '.') {
		ptr = end + 1;

		s->ts.origin.tv_nsec = (uint32_t) strtoul(ptr, &end, 10);
		if (ptr != end)
			fl |= IO_FORMAT_NANOSECONDS;
		else
			return -3;
	}
	else
		s->ts.origin.tv_nsec = 0;

	/* Optional: offset / delay */
	if (*end == '+' || *end == '-') {
		ptr = end;

		offset = strtof(ptr, &end); /* offset is ignored for now */
		if (ptr != end)
			fl |= IO_FORMAT_OFFSET;
		else
			return -4;
	}

	/* Optional: sequence */
	if (*end == '(') {
		ptr = end + 1;

		s->sequence = strtoul(ptr, &end, 10);
		if (ptr != end)
			fl |= IO_FORMAT_SEQUENCE;
		else
			return -5;

		if (*end == ')')
			end++;
	}

	for (ptr  = end, s->length = 0;
	                 s->length < s->capacity;
	     ptr  = end, s->length++) {

		switch (s->format & (1 << s->length)) {
			case SAMPLE_DATA_FORMAT_FLOAT:
				s->data[s->length].f = strtod(ptr, &end);
				break;
			case SAMPLE_DATA_FORMAT_INT:
				s->data[s->length].i = strtol(ptr, &end, 10);
				break;
		}

		if (end == ptr) /* There are no valid FP values anymore */
			break;
	}

	if (s->length > 0)
		fl |= IO_FORMAT_VALUES;

	if (flags)
		*flags = fl;

	if (fl & IO_FORMAT_OFFSET) {
		struct timespec off = time_from_double(offset);
		s->ts.received = time_add(&s->ts.origin, &off);
	}
	else
		s->ts.received = time_now();

	return end - buf;
}

size_t io_format_villas_sprint(char *buf, size_t len, struct sample *smps[], size_t cnt, int flags)
{
	size_t off = 0;

	for (int i = 0; i < cnt && off < len; i++)
		off += io_format_villas_sprint_single(buf + off, len - off, smps[i], flags);

	return off;
}

size_t io_format_villas_sscan(char *buf, size_t len, struct sample *smps[], size_t cnt, int *flags)
{
	size_t off = 0;

	for (int i = 0; i < cnt && off < len; i++)
		off += io_format_villas_sscan_single(buf + off, len - off, smps[i], flags);

	return off;
}

int io_format_villas_fscan_single(FILE *f, struct sample *s, int *flags)
{
	char *ptr, line[4096];

skip:	if (fgets(line, sizeof(line), f) == NULL)
		return -1; /* An error occured */

	/* Skip whitespaces, empty and comment lines */
	for (ptr = line; isspace(*ptr); ptr++);
	if (*ptr == '\0' || *ptr == '#')
		goto skip;

	return io_format_villas_sscan_single(line, strlen(line), s, flags);
}

int io_format_villas_fprint(FILE *f, struct sample *smps[], size_t cnt, int flags)
{
	char line[4096];
	int ret, i;

	for (i = 0; i < cnt; i++) {
		ret = io_format_villas_sprint_single(line, sizeof(line), smps[i], flags);
		if (ret < 0)
			break;

		fputs(line, f);
	}

	return i;
}

int io_format_villas_fscan(FILE *f, struct sample *smps[], size_t cnt, int *flags)
{
	int ret, i;

	for (i = 0; i < cnt; i++) {
		ret = io_format_villas_fscan_single(f, smps[i], flags);
		if (ret < 0)
			return ret;
	}

	return i;
}

int io_format_villas_open(struct io *io, const char *uri, const char *mode)
{
	int ret;

	ret = io_stream_open(io, uri, mode);
	if (ret)
		return ret;

	FILE *f = io->mode == IO_MODE_ADVIO
			? io->advio.output->file
			: io->stdio.output;

	fprintf(f, "# %-20s\t\t%s\n", "sec.nsec+offset", "data[]");

	if (io->flags & IO_FLAG_FLUSH)
		io_flush(io);

	return 0;
}

static struct plugin p = {
	.name = "villas",
	.description = "VILLAS human readable format",
	.type = PLUGIN_TYPE_FORMAT,
	.io = {
		.open	= io_format_villas_open,
		.fprint	= io_format_villas_fprint,
		.fscan	= io_format_villas_fscan,
		.sprint	= io_format_villas_sprint,
		.sscan	= io_format_villas_sscan,
		.size = 0
	}
};

REGISTER_PLUGIN(&p);
