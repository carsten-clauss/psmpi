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

/* info keys used for compressor handling (see mpir_compr.h) */
const char compressor_info_key[] = MPIX_COMPRESSOR_INFO_KEY_STRING;
const char compressor_info_key_plugin[] = MPIX_COMPRESSOR_INFO_KEY_PLUGIN_STRING;
const char compressor_info_key_plugin_list[] = MPIX_COMPRESSOR_INFO_KEY_PLUGIN_LIST_STRING;
const char compressor_info_key_plugin_separator[] = MPIX_COMPRESSOR_INFO_KEY_PLUGIN_SEPARATOR;

/* symbol name for plugin function called to register a compressor (see mpir_compr.h) */
const char compressor_register_plugin_fn[] = MPIX_COMPRESSOR_REGISTER_PLUGIN_FN_STRING;

/* list of compressors registered by the user (see mpir_compr.h) */
MPL_atomic_ptr_t MPIR_Compressor_head;

int MPIR_Register_compressor_impl(const char *compressor_name,
                                  MPIX_Compressor_req_init_function * compressor_req_init_fn,
                                  MPIX_Compressor_req_free_function * compressor_req_free_fn,
                                  MPIX_Compressor_conversion_function * compressor_deflate_fn,
                                  MPIX_Compressor_conversion_function * compressor_inflate_fn,
                                  MPIX_Compressor_deregister_function * compressor_deregister_fn,
                                  MPIR_Info * info, void *extra_state)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Compressor *mpir_compressor;

    /* check compressor isn't already registered */
    mpir_compressor = MPL_atomic_load_ptr(&MPIR_Compressor_head);
    while (mpir_compressor) {
        MPIR_ERR_CHKANDJUMP(!strncmp
                            (compressor_name, mpir_compressor->name, MPIX_MAX_COMPRESSOR_STRING),
                            mpi_errno, MPI_ERR_ARG, "**compressorused");
        mpir_compressor = mpir_compressor->next;
    }
    /* Check NULL function pointers on both sides (only on one side is okay) */
    MPIR_ERR_CHKANDJUMP((!compressor_deflate_fn &&
                         !compressor_inflate_fn), mpi_errno, MPI_ERR_ARG, "**compressorinvalid");

    mpir_compressor = MPL_malloc(sizeof(MPIR_Compressor), MPL_MEM_OTHER);
    mpir_compressor->name = MPL_strdup(compressor_name);
    mpir_compressor->extra_state = extra_state;
    mpir_compressor->req_init_fn = compressor_req_init_fn;
    mpir_compressor->req_free_fn = compressor_req_free_fn;
    mpir_compressor->deflate_fn = compressor_deflate_fn;
    mpir_compressor->inflate_fn = compressor_inflate_fn;
    mpir_compressor->deregister_fn = compressor_deregister_fn;
    mpir_compressor->next = MPL_atomic_load_ptr(&MPIR_Compressor_head);

    MPL_atomic_release_store_ptr(&MPIR_Compressor_head, mpir_compressor);

  fn_exit:
    return mpi_errno;

  fn_fail:
    goto fn_exit;
}
