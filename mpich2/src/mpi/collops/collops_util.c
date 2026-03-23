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
#include <dlfcn.h>

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

int MPIR_Collops_check_info(MPIR_Comm * comm)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Assert(comm);
    MPIR_Info *info = comm->info_ptr;
    MPIR_Assert(info);

    if (!MPIR_CVAR_COLLOPS_SUPPORT) {
        goto fn_exit;
    }

    int info_flag = 0;
    char info_collops_name[MPI_MAX_INFO_VAL + 1];
    mpi_errno =
        MPIR_Info_get_impl(info, collops_info_key, MPI_MAX_INFO_VAL, info_collops_name, &info_flag);
    MPIR_ERR_CHECK(mpi_errno);

    if (!info_flag || comm->collops.ptr) {
        /* Not requested or already considered. Do nothing. */
        goto fn_exit;
    }

    /* Check if collops name is given in a "comm=collops" format. */
    const char *assign = strchr(info_collops_name, '=');
    if (assign) {
        if (comm->name) {
            const char *ptr = info_collops_name;
            /* Allow also for comma-separated entries in this case. */
            while (ptr && assign) {
                const char *comma = strchr(assign + 1, ',');
                if (!comma) {
                    comma = ptr + strlen(ptr);
                }

                size_t key_len = assign - ptr;
                size_t val_len = comma - (assign + 1);

                if (strncmp(ptr, comm->name, key_len) == 0) {
                    /* The communicator name matches. Set the value as the collops name. */
                    sprintf(info_collops_name, "%.*s", (int) val_len, assign + 1);
                    break;
                }
                ptr = (*comma == ',') ? comma + 1 : comma;
                assign = strchr(ptr ? ptr : "", '=');
            }
        }
        /* Check if communicator name could be resolved. */
        assign = strchr(info_collops_name, '=');
        if (assign) {
            /* Communicator name could not be resolved. Do nothing. */
            goto fn_exit;
        }
    }

    MPIR_Collops *collops_found = NULL;
    MPIR_Collops_lookup(info_collops_name, &collops_found);

    /* If not found, check if a matching collops plugin can be loaded. */
    if (!collops_found) {

        /* First check if a plugin name was given. */
        char info_collops_plugins[MPI_MAX_INFO_VAL + 1];
        mpi_errno =
            MPIR_Info_get_impl(info, collops_info_key_plugin, MPI_MAX_INFO_VAL,
                               info_collops_plugins, &info_flag);
        MPIR_ERR_CHECK(mpi_errno);
        if (!info_flag) {
            mpi_errno =
                MPIR_Info_get_impl(info, collops_info_key_plugin_list, MPI_MAX_INFO_VAL,
                                   info_collops_plugins, &info_flag);
            MPIR_ERR_CHECK(mpi_errno);
            if (!info_flag) {
                goto fn_exit;
            }
        }

        for (char *plugin_name =
             strtok(info_collops_plugins, collops_info_key_plugin_separator);
             plugin_name != NULL; plugin_name = strtok(NULL, collops_info_key_plugin_separator)) {

            /* If so, try to load the shared library and call the register function. */
            void *dlhandle;
            char *dlerror_str;
            MPIX_Collops_register_plugin_function *collops_register_fn;

            dlerror();
            dlhandle = dlopen(plugin_name, RTLD_LAZY);
            dlerror_str = dlerror();
            if (!dlhandle || dlerror_str) {
                continue;
            }

            dlerror();
            collops_register_fn = dlsym(dlhandle, collops_register_plugin_fn);
            dlerror_str = dlerror();
            if (dlerror_str) {
                continue;
            }

            mpi_errno = collops_register_fn(info_collops_name, info->handle);
            if (mpi_errno != MPI_SUCCESS) {
                /* No error code generation: A failing `collops_register_fn` is not to be
                 * treated as an MPI error. This just deactivates the collops use. */
                continue;
            }

            /* ...and then retry the lookup.  */
            MPIR_Collops_lookup(info_collops_name, &collops_found);
            if (collops_found) {
                break;
            }
        }

        if (!collops_found) {
            goto fn_exit;
        }
    }

    MPIR_Assertp(collops_found);

    comm->collops.ptr = collops_found;
    comm->collops.comm_is_active = 0;
    comm->collops.comm_is_initialized = 0;
    comm->collops.comm_extra_state = NULL;

  fn_exit:
    return mpi_errno;

  fn_fail:
    goto fn_exit;
}

int MPIR_Collops_check_env(MPIR_Comm * comm)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Assert(comm);

    if (!MPIR_CVAR_COLLOPS_SUPPORT) {
        goto fn_exit;
    }

    /* Check if there is a collop preset for all communicators via the environment variable. */
    if (MPIR_CVAR_COLLOPS_PRESET && strlen(MPIR_CVAR_COLLOPS_PRESET)) {
        /* Set this as the collops name in the info object. */
        if (!comm->info_ptr) {
            /* If no info object exists yet, create one first. */
            mpi_errno = MPIR_Info_alloc(&comm->info_ptr);
            MPIR_ERR_CHECK(mpi_errno);
        }
        mpi_errno = MPIR_Info_set_impl(comm->info_ptr, collops_info_key, MPIR_CVAR_COLLOPS_PRESET);
        MPIR_ERR_CHECK(mpi_errno);
    }

    /* Check if there is a plugin name given via the environment variable. */
    if (MPIR_CVAR_COLLOPS_PLUGINS && strlen(MPIR_CVAR_COLLOPS_PLUGINS)) {
        /* Set this as the plugin name in the info object. */
        if (!comm->info_ptr) {
            /* If no info object exists yet, create one first. */
            mpi_errno = MPIR_Info_alloc(&comm->info_ptr);
            MPIR_ERR_CHECK(mpi_errno);
        }
        mpi_errno =
            MPIR_Info_set_impl(comm->info_ptr, collops_info_key_plugin, MPIR_CVAR_COLLOPS_PLUGINS);
        MPIR_ERR_CHECK(mpi_errno);
    }

  fn_exit:
    return mpi_errno;

  fn_fail:
    goto fn_exit;
}
