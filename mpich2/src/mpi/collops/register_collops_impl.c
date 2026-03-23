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

/*
=== BEGIN_MPI_T_CVAR_INFO_BLOCK ===

categories :
   - name : COLLOPS
     description : A category for collops-related variables.

cvars:
    - name        : MPIR_CVAR_COLLOPS_SUPPORT
      category    : COLLOPS
      alt-env     : PSP_COLLOPS_SUPPORT
      type        : boolean
      default     : true
      class       : none
      verbosity   : MPI_T_VERBOSITY_USER_BASIC
      scope       : MPI_T_SCOPE_ALL_EQ
      description : |-
        Lorem ipsum dolor sit ament.

    - name        : MPIR_CVAR_COLLOPS_PLUGINS
      category    : COLLOPS
      alt-env     : PSP_COLLOPS_PLUGINS
      type        : string
      default     : ""
      class       : none
      verbosity   : MPI_T_VERBOSITY_USER_BASIC
      scope       : MPI_T_SCOPE_ALL_EQ
      description : |-
        Lorem ipsum dolor sit ament.

    - name        : MPIR_CVAR_COLLOPS_PRESET
      category    : COLLOPS
      alt-env     : PSP_COLLOPS_PRESET
      type        : string
      default     : ""
      class       : none
      verbosity   : MPI_T_VERBOSITY_USER_BASIC
      scope       : MPI_T_SCOPE_ALL_EQ
      description : |-
        Lorem ipsum dolor sit ament.

=== END_MPI_T_CVAR_INFO_BLOCK ===
*/

/* info keys used for collops handling (see mpir_compr.h) */
const char collops_info_key[] = MPIX_COLLOPS_INFO_KEY_STRING;
const char collops_info_key_plugin[] = MPIX_COLLOPS_INFO_KEY_PLUGIN_STRING;
const char collops_info_key_plugin_list[] = MPIX_COLLOPS_INFO_KEY_PLUGIN_LIST_STRING;
const char collops_info_key_plugin_separator[] = MPIX_COLLOPS_INFO_KEY_PLUGIN_SEPARATOR;

/* symbol name for plugin function called to register a collops (see mpir_compr.h) */
const char collops_register_plugin_fn[] = MPIX_COLLOPS_REGISTER_PLUGIN_FN_STRING;

/* list of collopss registered by the user (see mpir_compr.h) */
MPL_atomic_ptr_t MPIR_Collops_head;

int mpir_progress_hook_registered = 0;
int mpir_collops_progress_hook_id = -1;
int mpir_collops_progress_hook_counter = 0;
#define MPIR_MAX_COLLOPS_PROGRESS_HOOKS 16
MPIX_Collops_progress_function *mpir_collops_progress_fn[MPIR_MAX_COLLOPS_PROGRESS_HOOKS] = { 0 };
void *mpir_collops_progress_hook_extra_state[MPIR_MAX_COLLOPS_PROGRESS_HOOKS] = { 0 };

static int mpir_collops_progress_hook(int vci, int *made_progress)
{
    int mpi_errno = MPI_SUCCESS;

    for (int i = 0; i < mpir_collops_progress_hook_counter; i++) {
        MPIR_Assert(mpir_collops_progress_fn[i]);
        mpi_errno = (mpir_collops_progress_fn[i]) (mpir_collops_progress_hook_extra_state[i]);
        if (mpi_errno != MPI_SUCCESS) {
            goto fn_fail;
        }
    }

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

static int mpir_collops_register_progress_fn(MPIX_Collops_progress_function * progress_fn,
                                             void *extra_state)
{
    /* TODO: Make this thread-safe! */
    int mpi_errno = MPI_SUCCESS;

    if (!mpir_progress_hook_registered) {
        mpi_errno =
            MPIR_Progress_hook_register(-1, mpir_collops_progress_hook,
                                        &mpir_collops_progress_hook_id);
        MPIR_Assert(mpi_errno == MPI_SUCCESS);
        MPIR_Progress_hook_activate(mpir_collops_progress_hook_id);
        mpir_progress_hook_registered = 1;
    }

    if (mpir_collops_progress_hook_counter >= MPIR_MAX_COLLOPS_PROGRESS_HOOKS) {
        MPIR_Assert(mpir_collops_progress_hook_counter == MPIR_MAX_COLLOPS_PROGRESS_HOOKS);
        /* Too few array entries. Increase MPIR_MAX_COLLOPS_PROGRESS_HOOKS. */
        return MPI_ERR_OTHER;
    } else {
        mpir_collops_progress_fn[mpir_collops_progress_hook_counter] = progress_fn;
        mpir_collops_progress_hook_extra_state[mpir_collops_progress_hook_counter] = extra_state;
        mpir_collops_progress_hook_counter++;
    }

    return mpi_errno;
}

int MPIR_Register_collops_impl(const char *name, int collops,
                               MPIX_Collops_algorithm_function * algorithm_fn,
                               MPIX_Collops_progress_function * progress_fn,
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
    mpir_collops->progress_fn = progress_fn;
    mpir_collops->comm_init_fn = comm_init_fn;
    mpir_collops->comm_free_fn = comm_free_fn;
    mpir_collops->deregister_fn = deregister_fn;
    mpir_collops->next = MPL_atomic_load_ptr(&MPIR_Collops_head);

    MPL_atomic_release_store_ptr(&MPIR_Collops_head, mpir_collops);

    if (mpir_collops->progress_fn) {
        mpi_errno = mpir_collops_register_progress_fn(mpir_collops->progress_fn, extra_state);
    }

    return mpi_errno;
}
