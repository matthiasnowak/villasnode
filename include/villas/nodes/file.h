/** Node type: File
 *
 * This file implements the file type for nodes.
 *
 * @file
 * @author Steffen Vogel <stvogel@eonerc.rwth-aachen.de>
 * @copyright 2017, Institute for Automation of Complex Power Systems, EONERC
 *********************************************************************************/

/**
 * @addtogroup file File-IO node type
 * @ingroup node
 * @{
 */

#pragma once

#include "advio.h"
#include "node.h"

#define FILE_MAX_PATHLEN	512

enum {
	FILE_READ,
	FILE_WRITE
};

struct file {
	struct file_direction {
		AFILE *handle;		/**< libc: stdio file handle. */

		const char *mode;	/**< libc: fopen() mode. */
		const char *fmt;	/**< Format string for file name. */

		char *uri;		/**< Real file name. */
	} read, write;
	
	int rewind;			/**< Should we rewind the file when we reach EOF? */

	enum read_epoch_mode {
		EPOCH_DIRECT,
		EPOCH_WAIT,
		EPOCH_RELATIVE,
		EPOCH_ABSOLUTE,
		EPOCH_ORIGINAL
	} read_epoch_mode;		/**< Specifies how file::offset is calculated. */

	struct timespec read_first;	/**< The first timestamp in the file file::{read,write}::uri */
	struct timespec read_epoch;	/**< The epoch timestamp from the configuration. */
	struct timespec read_offset;	/**< An offset between the timestamp in the input file and the current time */

	int read_timer;			/**< Timer file descriptor. Blocks until 1 / rate seconds are elapsed. */
	double read_rate;		/**< The read rate. */
};

/** @see node_type::print */
char * file_print(struct node *n);

/** @see node_type::parse */
int file_parse(struct node *n, config_setting_t *cfg);

/** @see node_type::open */
int file_start(struct node *n);

/** @see node_type::close */
int file_stop(struct node *n);

/** @see node_type::read */
int file_read(struct node *n, struct sample *smps[], unsigned cnt);

/** @see node_type::write */
int file_write(struct node *n, struct sample *smps[], unsigned cnt);

/** @} */
