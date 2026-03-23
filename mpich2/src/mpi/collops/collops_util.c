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

int MPIR_Collops_lookup(const char *collops_name, MPIR_Collops ** collops_pptr)
{
    MPIR_Collops *mpir_collops;

    MPIR_Assertp(collops_name);
    MPIR_Assertp(collops_pptr);

    *collops_pptr = NULL;

    mpir_collops = MPL_atomic_load_ptr(&MPIR_Collops_head);
    while (mpir_collops) {
        if (!strncmp(collops_name, mpir_collops->name, MPIX_MAX_COLLOPS_STRING)) {
            *collops_pptr = mpir_collops;
            break;
        }
        mpir_collops = mpir_collops->next;
    }

    return MPI_SUCCESS;
}

int MPIR_Collops_lookup_list(char **collops_name_list, int *length)
{
    MPIR_Collops *mpir_collops;

    MPIR_Assertp(collops_name_list);
    MPIR_Assertp(length);

    *length = 0;

    if (!collops_name_list) {
        /* This path serves for freeing recent_name_list at the end. */
        return MPI_SUCCESS;
    }

    mpir_collops = MPL_atomic_load_ptr(&MPIR_Collops_head);
    while (mpir_collops) {
        *length += strnlen(mpir_collops->name, MPIX_MAX_COLLOPS_STRING) + 1;
        mpir_collops = mpir_collops->next;
    }
    if (*length) {
        *collops_name_list = MPL_malloc(*length, MPL_MEM_OTHER);
        (*collops_name_list)[0] = '\0';
    } else {
        *collops_name_list = NULL;
        return MPI_SUCCESS;
    }

    mpir_collops = MPL_atomic_load_ptr(&MPIR_Collops_head);
    while (mpir_collops) {
        strcat(*collops_name_list, mpir_collops->name);
        if (mpir_collops->next) {
            strcat(*collops_name_list, ",");
        }
        mpir_collops = mpir_collops->next;
    }

    return MPI_SUCCESS;
}

int MPIR_Collops_lookup_nth(int n, char **collops_name)
{
    MPIR_Collops *mpir_collops;
    int i = 0;

    MPIR_Assertp(collops_name);

    *collops_name = NULL;

    mpir_collops = MPL_atomic_load_ptr(&MPIR_Collops_head);
    while (mpir_collops) {
        if (i == n) {
            *collops_name = mpir_collops->name;
            break;
        }
        mpir_collops = mpir_collops->next;
        i++;
    }

    return MPI_SUCCESS;
}

int MPIR_Collops_lookup_num(int *num)
{
    MPIR_Collops *mpir_collops;
    int i = 0;

    MPIR_Assertp(num);

    mpir_collops = MPL_atomic_load_ptr(&MPIR_Collops_head);
    while (mpir_collops) {
        mpir_collops = mpir_collops->next;
        i++;
    }

    *num = i;

    return MPI_SUCCESS;
}

int MPIR_Collops_set_info(MPIR_Info * info_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    char *collops_string;
    int collops_num = 0;
    char *list_string;
    int string_len;

    MPIR_Collops_lookup_num(&collops_num);

    if (collops_num) {  /* add registered collopss as array-type info */

        MPIR_Info *info_merged;
        MPIR_Info **info_array = MPL_malloc(collops_num * sizeof(MPIR_Info *), MPL_MEM_OTHER);

        for (int i = 0; i < collops_num; i++) {

            mpi_errno = MPIR_Info_alloc(&info_array[i]);
            MPIR_ERR_CHECK(mpi_errno);

            MPIR_Collops_lookup_nth(i, &collops_string);

            mpi_errno = MPIR_Info_set_impl(info_array[i], "collops", collops_string);
            MPIR_ERR_CHECK(mpi_errno);
        }

        MPIR_Info_merge_from_array_impl(collops_num, info_array, &info_merged);
        MPIR_Info_dup_key_impl(info_merged, "collops", info_ptr);

        for (int i = 0; i < collops_num; i++) {
            mpi_errno = MPIR_Info_free_impl(info_array[i]);
            MPIR_ERR_CHECK(mpi_errno);
        }
        MPL_free(info_array);

        mpi_errno = MPIR_Info_free_impl(info_merged);
        MPIR_ERR_CHECK(mpi_errno);
    }

    MPIR_Collops_lookup_list(&list_string, &string_len);

    if (string_len) {   /* add registered collopss as string-coded list */
        mpi_errno = MPIR_Info_set_impl(info_ptr, "collops_list", list_string);
        MPIR_ERR_CHECK(mpi_errno);
        MPL_free(list_string);
    }

  fn_exit:
    return mpi_errno;

  fn_fail:
    goto fn_exit;
}

extern int mpir_progress_hook_registered;
extern int mpir_collops_progress_hook_id;

int MPIR_Collops_deregister_all(void)
{
    /* free the memory allocated for a collops, if any */
    MPIR_Collops *collops_ptr, *collops_next;

    collops_ptr = MPL_atomic_load_ptr(&MPIR_Collops_head);
    while (collops_ptr) {
        collops_next = collops_ptr->next;
        if (collops_ptr->deregister_fn) {
            collops_ptr->deregister_fn(collops_ptr->extra_state);
        }
        MPL_free(collops_ptr->name);
        MPL_free(collops_ptr);
        collops_ptr = collops_next;
    }

    if (mpir_progress_hook_registered) {
        MPIR_Assert(mpir_collops_progress_hook_id > -1);
        MPIR_Progress_hook_deactivate(mpir_collops_progress_hook_id);
        MPIR_Progress_hook_deregister(mpir_collops_progress_hook_id);
        mpir_progress_hook_registered = 0;
    }

    return MPI_SUCCESS;
}
