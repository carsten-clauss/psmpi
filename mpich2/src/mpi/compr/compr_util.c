/*
 * ParaStation
 *
 * Copyright (C) 2025-2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "mpiimpl.h"

int MPIR_Compressor_lookup(const char *compressor_name, MPIR_Compressor ** compressor_pptr)
{
    MPIR_Compressor *mpir_compressor;

    MPIR_Assertp(compressor_name);
    MPIR_Assertp(compressor_pptr);

    *compressor_pptr = NULL;

    mpir_compressor = MPL_atomic_load_ptr(&MPIR_Compressor_head);
    while (mpir_compressor) {
        if (!strncmp(compressor_name, mpir_compressor->name, MPIX_MAX_COMPRESSOR_STRING)) {
            *compressor_pptr = mpir_compressor;
            break;
        }
        mpir_compressor = mpir_compressor->next;
    }

    return MPI_SUCCESS;
}

int MPIR_Compressor_deregister_all(void)
{
    /* free the memory allocated for a compressor, if any */
    MPIR_Compressor *compressor_ptr, *compressor_next;

    compressor_ptr = MPL_atomic_load_ptr(&MPIR_Compressor_head);
    while (compressor_ptr) {
        compressor_next = compressor_ptr->next;
        if (compressor_ptr->deregister_fn) {
            compressor_ptr->deregister_fn(compressor_ptr->extra_state);
        }
        MPL_free(compressor_ptr->name);
        MPL_free(compressor_ptr);
        compressor_ptr = compressor_next;
    }

    return MPI_SUCCESS;
}
