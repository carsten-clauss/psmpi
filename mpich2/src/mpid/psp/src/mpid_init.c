/*
 * ParaStation
 *
 * Copyright (C) 2006-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2024 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include <assert.h>
#include <unistd.h>
#include "mpidimpl.h"
#include "mpi-ext.h"
#include "mpl.h"
#include "errno.h"
#include "mpid_debug.h"
#include "mpid_coll.h"
#include "datatype.h"
#include "mpiimpl.h"

/*
 * MPIX_Query_cuda_support - Query CUDA support of the MPI library
 */
int __attribute__ ((visibility("default")))
    MPIX_Query_cuda_support(void)
{
    return MPID_Query_cuda_support();
}

#if defined(__GNUC__) || defined (__PGI)
#define dinit(name) .name =
#else
#define dinit(name)
#endif
MPIDI_Process_t MPIDI_Process = {
    dinit(socket) NULL,
    dinit(grank2con) NULL,
    dinit(my_pg_rank) - 1,
    dinit(my_pg_size) 0,
    dinit(singleton_but_no_pm) 0,
    dinit(pg_id_name) NULL,
    dinit(next_lpid) 0,
    dinit(my_pg) NULL,
    dinit(shm_attr_key) 0,
    dinit(smp_node_id) - 1,
    dinit(msa_module_id) - 1,
    dinit(use_world_model) 0,
    dinit(env) {
                dinit(debug_level) 0,
                dinit(debug_version) 0,
                dinit(debug_settings) 0,
                dinit(enable_collectives) 0,
                dinit(enable_ondemand) 0,
                dinit(enable_ondemand_spawn) 0,
                dinit(enable_smp_awareness) 1,
                dinit(enable_msa_awareness) 0,
#ifdef MPID_PSP_MSA_AWARE_COLLOPS
                dinit(enable_smp_aware_collops) 0,
                dinit(enable_msa_aware_collops) 1,
#endif
#ifdef MPID_PSP_HISTOGRAM
                dinit(enable_histogram) 0,
#endif
#ifdef MPID_PSP_HCOLL_STATS
                dinit(enable_hcoll_stats) 0,
#endif
                dinit(enable_lazy_disconnect) 1,
                dinit(rma) {
                            dinit(enable_rma_accumulate_ordering) - 1,
                            dinit(enable_explicit_wait_on_passive_side) - 1,
                            }
                ,
                dinit(hard_abort) 0,
                dinit(finalize) {
                                 dinit(barrier) 0,
                                 dinit(timeout) 30,
                                 dinit(shutdown) 0,
                                 dinit(exit) 0,
                                 }
                ,
                dinit(universe_size) 0,
                }
    ,
#ifdef MPIDI_PSP_WITH_STATISTICS
    dinit(stats) {
#ifdef MPID_PSP_HISTOGRAM
                  dinit(histo) {
                                dinit(con_type_str) NULL,
                                dinit(con_type_int) - 1,
                                dinit(max_size) 64 * 1024 * 1024,
                                dinit(min_size) 64,
                                dinit(step_width) 1,
                                dinit(points) 0,
                                dinit(limit) NULL,
                                dinit(count) NULL,
                                }
                  ,
#endif
#ifdef MPID_PSP_HCOLL_STATS
                  dinit(hcoll) {
                                dinit(counter) {0}
                                ,
                                }
                  ,
#endif
                  }
    ,
#endif /* MPIDI_PSP_WITH_STATISTICS */
};

/* Read global settings from environment variables */
static
void mpid_env_init(void)
{
    /* Initialize the switches */
    pscom_env_get_uint(&MPIDI_Process.env.enable_collectives, "PSP_COLLECTIVES");
    pscom_env_get_uint(&MPIDI_Process.env.enable_ondemand, "PSP_ONDEMAND");

    /* enable_ondemand_spawn defaults to enable_ondemand */
    MPIDI_Process.env.enable_ondemand_spawn = MPIDI_Process.env.enable_ondemand;
    pscom_env_get_uint(&MPIDI_Process.env.enable_ondemand_spawn, "PSP_ONDEMAND_SPAWN");

    /* take SMP-related locality information into account (e.g., for MPI_Win_allocate_shared) */
    pscom_env_get_uint(&MPIDI_Process.env.enable_smp_awareness, "PSP_SMP_AWARENESS");
    if (MPIDI_Process.env.enable_smp_awareness) {
        pscom_env_get_int(&MPIDI_Process.smp_node_id, "PSP_SMP_NODE_ID");
    }
#ifdef MPID_PSP_MSA_AWARENESS
    /* take MSA-related topology information into account */
    pscom_env_get_uint(&MPIDI_Process.env.enable_msa_awareness, "PSP_MSA_AWARENESS");
    if (MPIDI_Process.env.enable_msa_awareness) {
        pscom_env_get_int(&MPIDI_Process.msa_module_id, "PSP_MSA_MODULE_ID");
        pscom_env_get_int(&MPIDI_Process.smp_node_id, "PSP_MSA_NODE_ID");
    }
#endif

#ifdef MPID_PSP_MSA_AWARE_COLLOPS
    /* use hierarchy-aware collectives on SMP level */
    pscom_env_get_uint(&MPIDI_Process.env.enable_smp_aware_collops, "PSP_SMP_AWARE_COLLOPS");
#ifndef HAVE_HCOLL
    /* The usage of HCOLL and MSA aware collops are mutually exclusive.
     * Use hierarchy-aware collectives on MSA level only if HCOLL is not enabled */
    pscom_env_get_uint(&MPIDI_Process.env.enable_msa_aware_collops, "PSP_MSA_AWARE_COLLOPS");
#else
    MPIDI_Process.env.enable_msa_aware_collops = 0;
#endif
#endif

#ifdef MPID_PSP_HISTOGRAM
    /* collect statistics information and print them at the end of a run */
    pscom_env_get_uint(&MPIDI_Process.env.enable_histogram, "PSP_HISTOGRAM");
    pscom_env_get_int(&MPIDI_Process.stats.histo.max_size, "PSP_HISTOGRAM_MAX");
    pscom_env_get_int(&MPIDI_Process.stats.histo.min_size, "PSP_HISTOGRAM_MIN");
    pscom_env_get_int(&MPIDI_Process.stats.histo.step_width, "PSP_HISTOGRAM_SHIFT");
    MPIDI_Process.stats.histo.con_type_str = getenv("PSP_HISTOGRAM_CONTYPE");
    if (MPIDI_Process.stats.histo.con_type_str) {
        for (MPIDI_Process.stats.histo.con_type_int = PSCOM_CON_TYPE_GW;
             MPIDI_Process.stats.histo.con_type_int > PSCOM_CON_TYPE_NONE;
             MPIDI_Process.stats.histo.con_type_int--) {
            if (strcmp
                (MPIDI_Process.stats.histo.con_type_str,
                 pscom_con_type_str(MPIDI_Process.stats.histo.con_type_int)) == 0)
                break;
        }
    }
#endif
#ifdef MPID_PSP_HCOLL_STATS
    /* collect usage information of hcoll collectives and print them at the end of a run */
    pscom_env_get_uint(&MPIDI_Process.env.enable_hcoll_stats, "PSP_HCOLL_STATS");
#endif

    pscom_env_get_uint(&MPIDI_Process.env.enable_lazy_disconnect, "PSP_LAZY_DISCONNECT");

    pscom_env_get_int(&MPIDI_Process.env.rma.enable_rma_accumulate_ordering,
                      "PSP_ACCUMULATE_ORDERING");
    pscom_env_get_int(&MPIDI_Process.env.rma.enable_explicit_wait_on_passive_side,
                      "PSP_RMA_EXPLICIT_WAIT");

    pscom_env_get_int(&MPIDI_Process.env.hard_abort, "PSP_HARD_ABORT");

    /* PSP_FINALIZE_BARRIER (default=1)
     * With this environment variable, an additional barrier call for explicitly synchronizing
     * all processes at the end via a hook within MPI_Finalize can be controlled.
     * 0: Use _no_ additional (psp-related) barrier within MPI_Finalize()
     * 1: Use MPIR_Barrier() twice (with a timeout for the second, see PSP_FINALIZE_TIMEOUT)
     * 2: Use the barrier method of PMI/PMIx (Warning: without pscom progress within!)
     * others: N/A (i.e., no barrier)
     * (This is supposed to be a "hidden" variable! Therefore, we make use of MPIDI_PSP_env_get_int()
     * instead of pscom_env_get_int() here so that there is no logging about it.)
     */
    MPIDI_Process.env.finalize.barrier = MPIDI_PSP_env_get_int("PSP_FINALIZE_BARRIER", 1);

    /* PSP_FINALIZE_TIMEOUT (default=30)
     * Set the number of seconds that are allowed to elapse in MPI_Finalize() after leaving
     * the first MPIR_Barrier() call (PSP_FINALIZE_BARRIER=1, see above) until the second
     * barrier call is aborted via a timeout signal.
     * If set to 0, then no timeout and no second barrier are used.
     * (This is supposed to be a "hidden" variable! Therefore, we make use of MPIDI_PSP_env_get_int()
     * instead of pscom_env_get_int() here so that there is no logging about it.)
     */
    MPIDI_Process.env.finalize.timeout = MPIDI_PSP_env_get_int("PSP_FINALIZE_TIMEOUT", 30);

    /* PSP_FINALIZE_SHUTDOWN (default=0)
     * If set to >=1, all pscom sockets are already shut down (synchronized)
     * within MPI_Finalize().
     * (This is supposed to be a "hidden" variable! Therefore, we make use of MPIDI_PSP_env_get_int()
     * instead of pscom_env_get_int() here so that there is no logging about it.)
     */
    MPIDI_Process.env.finalize.shutdown = MPIDI_PSP_env_get_int("PSP_FINALIZE_SHUTDOWN", 0);

    /* PSP_FINALIZE_EXIT (default=0)
     * If set to 1, then exit() is called at the very end of MPI_Finalize().
     * If set to 2, then it is _exit().
     * (This is supposed to be a "hidden" variable! Therefore, we make use of MPIDI_PSP_env_get_int()
     * instead of pscom_env_get_int() here so that there is no logging about it.)
     */
    MPIDI_Process.env.finalize.exit = MPIDI_PSP_env_get_int("PSP_FINALIZE_EXIT", 0);

    MPIDI_Process.env.universe_size = MPIDI_PSP_env_get_int("MPIEXEC_UNIVERSE_SIZE",
                                                            MPIR_UNIVERSE_SIZE_NOT_AVAILABLE);

    /* PSP_DEBUG_SETTINGS (default=0)
     * If set to >=1, the psmpi version, PM, and ondemand setting of all processes are
     * compared to that of rank 0 during MPID_Init(). An additional KVS barrier over
     * all processes is required for this check. Hence it is disabled by default.
     * (This is supposed to be a hidden variable for internal debugging purposes!)
     */
    pscom_env_get_uint(&MPIDI_Process.env.debug_settings, "PSP_DEBUG_SETTINGS");
}

/* Add callbacks invoked during finalize */
static
void mpid_add_finalize_callbacks(void)
{
    /* add the callback for applying a Barrier (if enabled) at the very beginninf of Finalize */
    MPIR_Add_finalize(MPIDI_PSP_finalize_add_barrier_cb, NULL, MPIR_FINALIZE_CALLBACK_MAX_PRIO);

#ifdef MPIDI_PSP_WITH_STATISTICS
    /* add a callback for printing statistical information (if enabled) during Finalize */
    MPIR_Add_finalize(MPIDI_PSP_finalize_print_stats_cb, NULL, MPIR_FINALIZE_CALLBACK_PRIO + 1);
#endif
}

static
const char *ondemand_to_str(int ondemand)
{
    if (ondemand) {
        return "ondemand";
    } else {
        return "immediate";
    }
}

static
const char *pm_to_str(void)
{
#ifdef USE_PMI1_API
    return "pmi";
#elif defined USE_PMIX_API
    return "pmix";
#else
#error Neither PMI nor PMIx enabled, check your configure parameters!
#endif
}

/* Ensure at runtime that all processes use the same settings of psmpi (as rank 0).
 * This can be relevant, e.g., in case of MSA runs where there might be different
 * module trees */
static
int check_psmpi_settings(void)
{
    int mpi_errno = MPI_SUCCESS;
    char *settings = NULL;
    char *rank_0_settings = NULL;
    const char key[] = "psmpi_settings";
    int max_len;
    int rank = MPIR_Process.rank;
    const char *ondemand = ondemand_to_str(MPIDI_Process.env.enable_ondemand);
    const char *pm = pm_to_str();

    MPIR_CHKLMEM_DECL(2);

    /* There is no need to check settings in the singleton case and we moreover must
     * not use MPIR_pmi_kvs_put/get() in this case either since there is no process manager. */
    if (MPIDI_Process.singleton_but_no_pm) {
        goto fn_exit;
    }

    /* Prepare settings string including psmpi version, PM interface, ondemand */
    max_len = MPIR_pmi_max_val_size();
    MPIR_CHKLMEM_MALLOC(settings, char *, max_len, mpi_errno, "settings", MPL_MEM_OTHER);
    snprintf(settings, max_len, "%s-%s-%s", MPIDI_PSP_VC_VERSION, pm, ondemand);

    if (rank == 0) {
        mpi_errno = MPIR_pmi_kvs_put(key, settings);
        MPIR_ERR_CHECK(mpi_errno);
    }

    mpi_errno = MPIR_pmi_barrier();
    MPIR_ERR_CHECK(mpi_errno);

    if (rank != 0) {
        MPIR_CHKLMEM_MALLOC(rank_0_settings, char *, max_len, mpi_errno, "rank_0_settings",
                            MPL_MEM_OTHER);
        memset(rank_0_settings, 0, max_len);
        mpi_errno = MPIR_pmi_kvs_get(0, key, rank_0_settings, max_len);
        MPIR_ERR_CHECK(mpi_errno);

        if (strcmp(rank_0_settings, settings)) {
            fprintf(stderr,
                    "MPI error: different psmpi settings (rank 0:'%s' != rank %d:'%s')\n",
                    rank_0_settings, rank, settings);
            mpi_errno = MPI_ERR_OTHER;
            goto fn_fail;
        }
    }

  fn_exit:
    MPIR_CHKLMEM_FREEALL();
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

int MPID_Init(int requested, int *provided)
{
    int mpi_errno = MPI_SUCCESS;
    pscom_err_t rc;

    mpid_debug_init();

    MPIR_FUNC_ENTER;

    /* Set process parameters */
    MPIDI_Process.my_pg_rank = MPIR_Process.rank >= 0 ? MPIR_Process.rank : 0;
    MPIDI_Process.my_pg_size = MPIR_Process.size > 0 ? MPIR_Process.size : 1;
    MPIDI_Process.pg_id_name = MPL_strdup(MPIR_pmi_job_id());
    /* keep track if we are a singleton without process manager */
    MPIDI_Process.singleton_but_no_pm = (MPIR_Process.appnum == -1) ? 1 : 0;

    MPIR_Process.attrs.appnum = MPIR_Process.appnum;
    MPIR_Process.attrs.tag_ub = MPIDI_TAG_UB;

    if (
#ifndef MPICH_IS_THREADED
           1
#else
           requested < MPI_THREAD_MULTIPLE
#endif
) {
        rc = pscom_init(PSCOM_VERSION);
        if (rc != PSCOM_SUCCESS) {
            fprintf(stderr, "pscom_init(0x%04x) failed : %s\n", PSCOM_VERSION, pscom_err_str(rc));
            exit(1);
        }
    } else {
        rc = pscom_init_thread(PSCOM_VERSION);
        if (rc != PSCOM_SUCCESS) {
            fprintf(stderr, "pscom_init_thread(0x%04x) failed : %s\n",
                    PSCOM_VERSION, pscom_err_str(rc));
            exit(1);
        }
    }

    mpid_env_init();

    mpid_add_finalize_callbacks();

    /* Do the settings check via KVS if requested */
    if (MPIDI_Process.env.debug_settings) {
        mpi_errno = check_psmpi_settings();
        MPIR_ERR_CHECK(mpi_errno);
    }

    /* Init global pscom socket and connectons */
    mpi_errno = MPIDI_PSP_connection_init();
    MPIR_ERR_CHECK(mpi_errno);

    /* Init global PG, representing the group of processes started together with me (my_pg). */
    mpi_errno = MPIDI_PSP_PG_init();
    MPIR_ERR_CHECK(mpi_errno);

    MPID_PSP_shm_rma_init();

    if (provided) {
        *provided = (MPICH_THREAD_LEVEL < requested) ? MPICH_THREAD_LEVEL : requested;
    }

    /* init lists for partitioned communication operations (used on receiver side) */
    INIT_LIST_HEAD(&(MPIDI_Process.part_posted_list));
    INIT_LIST_HEAD(&(MPIDI_Process.part_unexp_list));

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


int MPID_InitCompleted(void)
{
    int mpi_errno = MPI_SUCCESS;

    /* Do the world model related device initialization here. */
    MPIDI_Process.use_world_model = 1;

    MPIR_Process.comm_world->pscom_socket = MPIDI_Process.socket;
    MPIR_Process.comm_self->pscom_socket = MPIDI_Process.socket;

    /* Call the other init routines */
    mpi_errno = MPID_PSP_comm_init(MPIR_Process.has_parent);
    MPIR_ERR_CHECK(mpi_errno);

    /*
     * Setup the MPI_INFO_ENV object
     */
    {
        MPIR_Info *info_ptr = NULL;
        MPIR_Info_get_ptr(MPI_INFO_ENV, info_ptr);
        if (MPID_Query_cuda_support()) {
            mpi_errno = MPIR_Info_set_impl(info_ptr, "cuda_aware", "true");
        } else {
            mpi_errno = MPIR_Info_set_impl(info_ptr, "cuda_aware", "false");
        }
        if (MPI_SUCCESS != mpi_errno) {
            MPIR_ERR_POP(mpi_errno);
        }
#ifdef MPID_PSP_MSA_AWARENESS
        char id_str[64];
        if (MPIDI_Process.msa_module_id >= 0) {
            snprintf(id_str, 63, "%d", MPIDI_Process.msa_module_id);
            mpi_errno = MPIR_Info_set_impl(info_ptr, "msa_module_id", id_str);
            if (MPI_SUCCESS != mpi_errno) {
                MPIR_ERR_POP(mpi_errno);
            }
        }
        if (MPIDI_Process.smp_node_id >= 0 && MPIDI_Process.env.enable_msa_awareness) {
            snprintf(id_str, 63, "%d", MPIDI_Process.smp_node_id);
            mpi_errno = MPIR_Info_set_impl(info_ptr, "msa_node_id", id_str);
            if (MPI_SUCCESS != mpi_errno) {
                MPIR_ERR_POP(mpi_errno);
            }
        }
#endif
    }

    return MPI_SUCCESS;

  fn_exit:
    MPIR_FUNC_EXIT;
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

int MPID_Allocate_vci(int *vci)
{
    int mpi_errno = MPI_SUCCESS;
    *vci = 0;
    MPIR_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**pspnostream");
    return mpi_errno;
}

int MPID_Deallocate_vci(int vci)
{
    MPIR_Assert(0);
    return MPI_SUCCESS;
}


/* return connection_t for rank, NULL on error */
pscom_connection_t *MPID_PSCOM_rank2connection(MPIR_Comm * comm, int rank)
{
    if ((rank >= 0) && (rank < comm->remote_size)) {
        return comm->vcr[rank]->con;
    } else {
        return NULL;
    }
}


/*
 * MPID_Get_universe_size - Set the universe size to what was provided by the
 * environment or to the default value MPIR_UNIVERSE_SIZE_NOT_AVAILABLE
 */
int MPID_Get_universe_size(int *universe_size)
{
    *universe_size = MPIDI_Process.env.universe_size;
    return MPI_SUCCESS;
}

/*
 * MPID_Query_cuda_support - Query CUDA support of the device
 */
int MPID_Query_cuda_support(void)
{
#if MPIX_CUDA_AWARE_SUPPORT
    return pscom_is_cuda_enabled();
#else
    return 0;
#endif
}
