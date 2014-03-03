/*
 * ParaStation
 *
 * Copyright (C) 2006-2009 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Author:	Jens Hauke <hauke@par-tec.com>
 */

#include <unistd.h>
#include <signal.h>
#include "mpidimpl.h"
#include "pmi.h"

static
int _getenv_i(const char *env_name, int _default)
{
	char *val = getenv(env_name);
	return val ? atoi(val) : _default;
}

static
void sig_finalize_timeout(int signo)
{
	if (_getenv_i("PSP_DEBUG", 0) > 0) {
		fprintf(stderr, "Warning: PSP_FINALIZE_TIMEOUT\n");
	}
	_exit(0);
}

int MPID_Finalize(void)
{
	MPIDI_STATE_DECL(MPID_STATE_MPID_FINALIZE);
	MPIDI_FUNC_ENTER(MPID_STATE_MPID_FINALIZE);
/* ToDo: */
/*	fprintf(stderr, "%d waitall\n", MPIDI_Process.my_pg_rank); */

	{
		int errflag = 0;
		int timeout;
		MPIU_THREADPRIV_DECL;
		MPIU_THREADPRIV_GET;

		MPIR_Barrier_impl(MPIR_Process.comm_world, &errflag);

		/* Finalize timeout: Default: 5sec.
		   Overwrite with PSP_FINALIZE_TIMEOUT.
		   Disable with PSP_FINALIZE_TIMEOUT=0 */
		timeout = _getenv_i("PSP_FINALIZE_TIMEOUT", 5);
		if (timeout > 0) {
			signal(SIGALRM, sig_finalize_timeout);
			alarm(timeout);
			MPIR_Barrier_impl(MPIR_Process.comm_world, &errflag);
		}
	}

/*	fprintf(stderr, "%d cleanup queue\n", MPIDI_Process.my_pg_rank); */
	MPID_req_queue_cleanup();

	MPID_PSP_rma_cleanup();

/*	fprintf(stderr, "%d PMI_Finalize\n", MPIDI_Process.my_pg_rank); */
	PMI_Finalize();

	MPIDI_FUNC_EXIT(MPID_STATE_MPID_FINALIZE);
	return MPI_SUCCESS;
}
