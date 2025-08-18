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

int MPIR_Compressor_lookup_list(char **compressor_name_list, int *length)
{
    MPIR_Compressor *mpir_compressor;

    MPIR_Assertp(compressor_name_list);
    MPIR_Assertp(length);

    *length = 0;

    mpir_compressor = MPL_atomic_load_ptr(&MPIR_Compressor_head);
    while (mpir_compressor) {
        *length += strnlen(mpir_compressor->name, MPIX_MAX_COMPRESSOR_STRING) + 1;
        mpir_compressor = mpir_compressor->next;
    }
    if (*length) {
        *compressor_name_list = MPL_malloc(*length, MPL_MEM_OTHER);
        (*compressor_name_list)[0] = '\0';
    } else {
        *compressor_name_list = NULL;
        return MPI_SUCCESS;
    }

    mpir_compressor = MPL_atomic_load_ptr(&MPIR_Compressor_head);
    while (mpir_compressor) {
        strcat(*compressor_name_list, mpir_compressor->name);
        if (mpir_compressor->next) {
            strcat(*compressor_name_list, ",");
        }
        mpir_compressor = mpir_compressor->next;
    }

    return MPI_SUCCESS;
}

int MPIR_Compressor_lookup_nth(int n, char **compressor_name)
{
    MPIR_Compressor *mpir_compressor;
    int i = 0;

    MPIR_Assertp(compressor_name);

    *compressor_name = NULL;

    mpir_compressor = MPL_atomic_load_ptr(&MPIR_Compressor_head);
    while (mpir_compressor) {
        if (i == n) {
            *compressor_name = mpir_compressor->name;
            break;
        }
        mpir_compressor = mpir_compressor->next;
        i++;
    }

    return MPI_SUCCESS;
}

int MPIR_Compressor_lookup_num(int *num)
{
    MPIR_Compressor *mpir_compressor;
    int i = 0;

    MPIR_Assertp(num);

    mpir_compressor = MPL_atomic_load_ptr(&MPIR_Compressor_head);
    while (mpir_compressor) {
        mpir_compressor = mpir_compressor->next;
        i++;
    }

    *num = i;

    return MPI_SUCCESS;
}

int MPIR_Compressor_set_info(MPIR_Info * info_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    char *compressor_string;
    int compressor_num = 0;
    char *list_string;
    int string_len;

    MPIR_Compressor_lookup_num(&compressor_num);

    if (compressor_num) {       /* add registered compressors as array-type info */

        MPIR_Info *info_merged;
        MPIR_Info **info_array = MPL_malloc(compressor_num * sizeof(MPIR_Info *), MPL_MEM_OTHER);

        for (int i = 0; i < compressor_num; i++) {

            mpi_errno = MPIR_Info_alloc(&info_array[i]);
            MPIR_ERR_CHECK(mpi_errno);

            MPIR_Compressor_lookup_nth(i, &compressor_string);

            mpi_errno = MPIR_Info_set_impl(info_array[i], "compressor", compressor_string);
            MPIR_ERR_CHECK(mpi_errno);
        }

        MPIR_Info_merge_from_array_impl(compressor_num, info_array, &info_merged);
        MPIR_Info_dup_key_impl(info_merged, "compressor", info_ptr);

        for (int i = 0; i < compressor_num; i++) {
            mpi_errno = MPIR_Info_free_impl(info_array[i]);
            MPIR_ERR_CHECK(mpi_errno);
        }
        MPL_free(info_array);

        mpi_errno = MPIR_Info_free_impl(info_merged);
        MPIR_ERR_CHECK(mpi_errno);
    }

    MPIR_Compressor_lookup_list(&list_string, &string_len);

    if (string_len) {   /* add registered compressors as string-coded list */
        mpi_errno = MPIR_Info_set_impl(info_ptr, "compressor_list", list_string);
        MPIR_ERR_CHECK(mpi_errno);
        MPL_free(list_string);
    }

  fn_exit:
    return mpi_errno;

  fn_fail:
    goto fn_exit;
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
