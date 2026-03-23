/*
 * ParaStation
 *
 * Copyright (C) 2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "mpiimpl.h"

/* info keys used for collops handling (see mpir_compr.h) */
const char collops_info_key[] = MPIX_COLLOPS_INFO_KEY_STRING;
const char collops_info_key_plugin[] = MPIX_COLLOPS_INFO_KEY_PLUGIN_STRING;

/* symbol name for plugin function called to register a collops (see mpir_compr.h) */
const char collops_register_plugin_fn[] = MPIX_COLLOPS_REGISTER_PLUGIN_FN_STRING;

/* list of collopss registered by the user (see mpir_compr.h) */
MPL_atomic_ptr_t MPIR_Collops_head;

int MPIR_Register_collops_impl(const char *name, int collops,
                               MPIX_Collops_algorithm_function * algorithm_fn,
                               MPIX_Collops_comm_init_function * comm_init_fn,
                               MPIX_Collops_comm_free_function * comm_free_fn,
                               MPIX_Collops_deregister_function * deregister_fn,
                               MPIR_Info * info_ptr, void *extra_state)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Collops *mpir_collops;

    /* check collops isn't already registered */
    mpir_collops = MPL_atomic_load_ptr(&MPIR_Collops_head);
    while (mpir_collops) {
        MPIR_Assert(strncmp(name, mpir_collops->name, MPIX_MAX_COLLOPS_STRING));
        mpir_collops = mpir_collops->next;
    }

    MPIR_Assert(algorithm_fn);

    mpir_collops = MPL_malloc(sizeof(MPIR_Collops), MPL_MEM_OTHER);
    mpir_collops->name = MPL_strdup(name);
    mpir_collops->collops_mask = collops;
    mpir_collops->extra_state = extra_state;
    mpir_collops->algorithm_fn = algorithm_fn;
    mpir_collops->comm_init_fn = comm_init_fn;
    mpir_collops->comm_free_fn = comm_free_fn;
    mpir_collops->deregister_fn = deregister_fn;
    mpir_collops->next = MPL_atomic_load_ptr(&MPIR_Collops_head);

    MPL_atomic_release_store_ptr(&MPIR_Collops_head, mpir_collops);

    return mpi_errno;
}
