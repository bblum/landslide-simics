/**
 * @file job.h
 * @brief job management
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_JOB_H
#define __ID_JOB_H

#include "pp.h"

struct job {
	struct pp_set config;
	int config_fd;
	char *config_filename;
	// TODO: socket for communication
};

#endif
