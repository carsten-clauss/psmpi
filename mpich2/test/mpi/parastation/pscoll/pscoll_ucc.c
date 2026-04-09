/*
 * ParaStation
 *
 * Copyright (C) 2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "mpi.h"
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#ifndef USE_COLLOPS_PLUGIN
#include <ucc/api/ucc.h>

#ifdef MAKE_COLLOPS_PLUGIN
/* In this plugin mode, only the callbacks are compiled and the main
 * function is omitted. This allows to generate a collops plugin
 * with these callbacks that can then be used by a separate test.
 */
#define __WITHOUT_MAIN
#endif /* MAKE_COLLOPS_PLUGIN */

typedef enum {
    PSCOLL_UCC_RETVAL_ERROR = -1,
    PSCOLL_UCC_RETVAL_SUCCESS = 0,
    PSCOLL_UCC_RETVAL_FALLBACK = 1,
} pscoll_ucc_error_t;

typedef struct {
    int ucc_initialized;
    ucc_team_h ucc_team;
    MPI_Comm comm;
    int rank;
    int size;
    int *rank_map;
} pscoll_ucc_comm_t;

typedef struct {
    int ucc_initialized;
    int comm_world_initialized;
    ucc_lib_h ucc_lib;
    ucc_lib_attr_t ucc_lib_attr;
    ucc_context_h ucc_context;
    ucc_thread_mode_t thread_mode;
} pscoll_ucc_priv_t;

pscoll_ucc_priv_t pscoll_ucc_priv = { 0 };

typedef struct {
    unsigned long long int barrier;
    unsigned long long int bcast;
    unsigned long long int scatter;
    unsigned long long int scatterv;
    unsigned long long int gather;
    unsigned long long int gatherv;
    unsigned long long int allgather;
    unsigned long long int allgatherv;
    unsigned long long int alltoall;
    unsigned long long int alltoallv;
    unsigned long long int reduce;
    unsigned long long int allreduce;
} pscoll_ucc_counters_t;

pscoll_ucc_counters_t pscoll_ucc_counters = { 0 };

typedef struct {
    int world_rank;
    int world_size;
} pscoll_ucc_extra_state_t;

pscoll_ucc_extra_state_t pscoll_ucc_extra_state;

static void pscoll_ucc_progress(void)
{
    assert(pscoll_ucc_priv.ucc_initialized);
    ucc_context_progress(pscoll_ucc_priv.ucc_context);
}

static ucc_status_t pscoll_ucc_oob_allgather_test(void *req)
{
    MPI_Request *request = (MPI_Request *) req;
    int completed = 0;

    MPI_Test(request, &completed, MPI_STATUS_IGNORE);

    if (!completed) {
        return UCC_INPROGRESS;
    }

    return UCC_OK;
}

static ucc_status_t pscoll_ucc_oob_allgather_free(void *req)
{
    MPI_Request *request = (MPI_Request *) req;
    assert(*request == MPI_REQUEST_NULL);
    free(request);

    return UCC_OK;
}

static ucc_status_t pscoll_ucc_oob_allgather(void *sbuf, void *rbuf, size_t msglen,
                                             void *oob_coll_ctx, void **req)
{
    assert(oob_coll_ctx == NULL);
    MPI_Comm comm = MPI_COMM_WORLD;
    MPI_Request *request;
    request = malloc(sizeof(MPI_Request));

    MPI_Iallgather(sbuf, msglen, MPI_BYTE, rbuf, msglen, MPI_BYTE, comm, request);

    *req = request;

    return UCC_OK;
}

static int pscoll_ucc_setup_lib(void)
{
    int pscoll_ucc_err = PSCOLL_UCC_RETVAL_SUCCESS;
    char str_buf[256] = { 0 };
    ucc_lib_config_h lib_config = { 0 };
    ucc_context_config_h ctx_config = { 0 };
    ucc_lib_params_t lib_params = { 0 };
    ucc_context_params_t ctx_params = { 0 };

    ucc_lib_h *lib_ptr = NULL;
    ucc_lib_attr_t *lib_attr_ptr = NULL;
    ucc_lib_config_h *lib_config_ptr = NULL;
    ucc_lib_params_t *lib_params_ptr = NULL;
    ucc_context_params_t *ctx_params_ptr = NULL;
    ucc_context_config_h *ctx_config_ptr = NULL;

    if (ucc_lib_config_read(NULL, NULL, &lib_config) != UCC_OK) {
        goto fn_fail;
    }

    lib_config_ptr = &lib_config;
    lib_params_ptr = &lib_params;

    int thread_level = MPI_THREAD_MULTIPLE;
    MPI_Query_thread(&thread_level);

    if (thread_level == MPI_THREAD_MULTIPLE) {
        lib_params_ptr->thread_mode = UCC_THREAD_MULTIPLE;
        lib_params_ptr->mask = UCC_LIB_PARAM_FIELD_THREAD_MODE;
    } else {
        lib_params_ptr->mask = 0;
    }
    if (ucc_init(lib_params_ptr, *lib_config_ptr, &pscoll_ucc_priv.ucc_lib) != UCC_OK) {
        goto fn_fail;
    }

    lib_ptr = &pscoll_ucc_priv.ucc_lib;
    lib_attr_ptr = &pscoll_ucc_priv.ucc_lib_attr;
    lib_attr_ptr->mask = UCC_LIB_ATTR_FIELD_THREAD_MODE | UCC_LIB_ATTR_FIELD_COLL_TYPES;

    if (ucc_lib_get_attr(*lib_ptr, lib_attr_ptr)) {
        goto fn_fail;
    }

    if (lib_attr_ptr->thread_mode < lib_params_ptr->thread_mode) {
        goto fn_fail;
    }

    ctx_params_ptr = &ctx_params;
    ctx_params_ptr->mask = UCC_CONTEXT_PARAM_FIELD_OOB;
    ctx_params_ptr->oob.allgather = pscoll_ucc_oob_allgather;
    ctx_params_ptr->oob.req_test = pscoll_ucc_oob_allgather_test;
    ctx_params_ptr->oob.req_free = pscoll_ucc_oob_allgather_free;

    int world_size, world_rank;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    ctx_params_ptr->oob.coll_info = NULL;
    ctx_params_ptr->oob.n_oob_eps = world_size;
    ctx_params_ptr->oob.oob_ep = world_rank;

    if (ucc_context_config_read(*lib_ptr, NULL, &ctx_config) != UCC_OK) {
        goto fn_fail;
    }

    ctx_config_ptr = &ctx_config;

    snprintf(str_buf, sizeof(str_buf), "%d", world_size);
    if (ucc_context_config_modify(*ctx_config_ptr, NULL, "ESTIMATED_NUM_EPS", str_buf) != UCC_OK) {
        goto fn_fail;
    }

    if (ucc_context_create(*lib_ptr, ctx_params_ptr, *ctx_config_ptr, &pscoll_ucc_priv.ucc_context)
        != UCC_OK) {
        goto fn_fail;
    }

    pscoll_ucc_priv.ucc_initialized = 1;

  fn_exit:
    if (lib_config_ptr)
        ucc_lib_config_release(*lib_config_ptr);
    if (ctx_config_ptr)
        ucc_context_config_release(*ctx_config_ptr);
    return pscoll_ucc_err;
  fn_fail:
    if (lib_ptr)
        ucc_finalize(*lib_ptr);
    pscoll_ucc_err = PSCOLL_UCC_RETVAL_ERROR;
    goto fn_exit;
}

static uint64_t pscoll_ucc_rank_map_cb(uint64_t ep, void *cb_ctx)
{
    pscoll_ucc_comm_t *pscoll_ptr = cb_ctx;
    return (uint64_t) pscoll_ptr->rank_map[(int) ep];
}

static int pscoll_ucc_setup_comm_team(pscoll_ucc_comm_t * pscoll_ptr)
{
    int pscoll_ucc_err = PSCOLL_UCC_RETVAL_SUCCESS;
    ucc_status_t status;
    ucc_team_params_t team_params = {
        .mask =
            UCC_TEAM_PARAM_FIELD_EP_MAP | UCC_TEAM_PARAM_FIELD_EP | UCC_TEAM_PARAM_FIELD_EP_RANGE,
        .ep_map = {
                   .type = (pscoll_ptr->comm == MPI_COMM_WORLD) ? UCC_EP_MAP_FULL : UCC_EP_MAP_CB,
                   .ep_num = pscoll_ptr->size,
                   .cb.cb = pscoll_ucc_rank_map_cb,
                   .cb.cb_ctx = (void *) pscoll_ptr},
        .ep = pscoll_ptr->rank,
        .ep_range = UCC_COLLECTIVE_EP_RANGE_CONTIG,
    };

    if (ucc_team_create_post(&pscoll_ucc_priv.ucc_context, 1, &team_params, &pscoll_ptr->ucc_team)
        != UCC_OK) {
        goto fn_fail;
    }
    while ((status = ucc_team_create_test(pscoll_ptr->ucc_team)) == UCC_INPROGRESS) {
        int flag;
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE);
        pscoll_ucc_progress();
    }
    if (status != UCC_OK) {
        goto fn_fail;
    }

  fn_exit:
    return pscoll_ucc_err;
  fn_fail:
    pscoll_ptr->ucc_team = NULL;
    pscoll_ptr->ucc_initialized = 0;
    pscoll_ucc_err = PSCOLL_UCC_RETVAL_ERROR;
    goto fn_exit;
}

int pscoll_ucc_comm_create_hook(pscoll_ucc_comm_t * pscoll_ptr)
{
    int pscoll_ucc_err = PSCOLL_UCC_RETVAL_SUCCESS;

    pscoll_ptr->ucc_initialized = 0;

    if (!pscoll_ucc_priv.comm_world_initialized && (pscoll_ptr->comm != MPI_COMM_WORLD)) {
        goto fn_exit;
    }

    if (pscoll_ptr->size < 2) {
        goto disabled;
    }

    int is_intercomm;
    MPI_Comm_test_inter(pscoll_ptr->comm, &is_intercomm);
    if (is_intercomm) {
        goto disabled;
    }

    if (!pscoll_ucc_priv.ucc_initialized) {
        pscoll_ucc_err = pscoll_ucc_setup_lib();
        if (pscoll_ucc_err != PSCOLL_UCC_RETVAL_SUCCESS) {
            goto fn_fail;
        }
    }

    assert(pscoll_ucc_priv.ucc_initialized);

    pscoll_ucc_err = pscoll_ucc_setup_comm_team(pscoll_ptr);
    if (pscoll_ucc_err != PSCOLL_UCC_RETVAL_SUCCESS) {
        goto fn_fail;
    }

    if (pscoll_ptr->comm == MPI_COMM_WORLD) {
        pscoll_ucc_priv.comm_world_initialized = 1;
    }

    pscoll_ptr->ucc_initialized = 1;

  fn_exit:
    return pscoll_ucc_err;
  fn_fail:
    pscoll_ucc_err = PSCOLL_UCC_RETVAL_ERROR;
    goto fn_exit;
  disabled:
    goto fn_exit;
}

int pscomm_ucc_comm_destroy_hook(pscoll_ucc_comm_t * pscoll_ptr)
{
    int pscoll_ucc_err = PSCOLL_UCC_RETVAL_SUCCESS;
    ucc_status_t status;

    if (!pscoll_ucc_priv.ucc_initialized || !pscoll_ptr || !pscoll_ptr->ucc_initialized) {
        goto fn_exit;
    }

    if ((pscoll_ptr->comm != MPI_COMM_WORLD) && (pscoll_ptr->comm != MPI_COMM_SELF)) {
        int is_intercomm;
        MPI_Comm_test_inter(pscoll_ptr->comm, &is_intercomm);
        if (is_intercomm) {
            goto disabled;
        }
    }

    while ((status = ucc_team_destroy(pscoll_ptr->ucc_team)) == UCC_INPROGRESS) {
        int flag;
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, MPI_STATUS_IGNORE);
        pscoll_ucc_progress();
    }

    if (status != UCC_OK) {
        goto fn_fail;
    }

    if (pscoll_ptr->comm == MPI_COMM_WORLD) {
        pscoll_ucc_priv.comm_world_initialized = 0;
    }

    pscoll_ptr->ucc_initialized = 0;

  fn_exit:
    return pscoll_ucc_err;
  fn_fail:
    goto fn_exit;
  disabled:
    goto fn_exit;
}

int collops_comm_init(MPI_Comm comm, void *extra_state, void *extra_comm_state)
{
    int rank, size;
    void **extra_comm_state_ = extra_comm_state;
    assert(extra_state == &pscoll_ucc_extra_state);

    pscoll_ucc_comm_t *pscoll_ptr = malloc(sizeof(pscoll_ucc_comm_t));

    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);
    pscoll_ptr->comm = comm;
    pscoll_ptr->size = size;
    pscoll_ptr->rank = rank;

    pscoll_ptr->rank_map = malloc(size * sizeof(int));
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Allgather(&rank, 1, MPI_INT, pscoll_ptr->rank_map, 1, MPI_INT, comm);

    pscoll_ucc_comm_create_hook(pscoll_ptr);

    *extra_comm_state_ = pscoll_ptr;
    return MPI_SUCCESS;
}

int collops_comm_free(void *extra_comm_state)
{
    assert(extra_comm_state != NULL);
    pscoll_ucc_comm_t *pscoll_ptr = extra_comm_state;

    pscomm_ucc_comm_destroy_hook(pscoll_ptr);

    free(pscoll_ptr->rank_map);
    free(pscoll_ptr);

    return MPI_SUCCESS;
}

#define PSCOLL_UCC_DTYPE_NULL ((ucc_datatype_t)-1)
#define PSCOLL_UCC_DTYPE_UNSUPPORTED ((ucc_datatype_t)-2)

#define PSCOLL_UCC_DTYPE_MAP_SIGNED(_ctype)         \
    do {                                            \
        switch (sizeof(signed _ctype)) {            \
        case 1:                                     \
            return UCC_DT_INT8;                     \
        case 2:                                     \
            return UCC_DT_INT16;                    \
        case 4:                                     \
            return UCC_DT_INT32;                    \
        case 8:                                     \
            return UCC_DT_INT64;                    \
        case 16:                                    \
            return UCC_DT_INT128;                   \
        }                                           \
    } while (0)
#define PSCOLL_UCC_DTYPE_MAP_UNSIGNED(_ctype)       \
    do {                                            \
        switch (sizeof(unsigned _ctype)) {          \
        case 1:                                     \
            return UCC_DT_UINT8;                    \
        case 2:                                     \
            return UCC_DT_UINT16;                   \
        case 4:                                     \
            return UCC_DT_UINT32;                   \
        case 8:                                     \
            return UCC_DT_UINT64;                   \
        case 16:                                    \
            return UCC_DT_UINT128;                  \
        }                                           \
    } while (0)
#define PSCOLL_UCC_DTYPE_MAP_FLOAT(_ctype)          \
    do {                                            \
        switch (sizeof(_ctype)) {                   \
        case 4:                                     \
            return UCC_DT_FLOAT32;                  \
        case 8:                                     \
            return UCC_DT_FLOAT64;                  \
        case 16:                                    \
            return UCC_DT_FLOAT128;                 \
        }                                           \
    } while (0)

static inline ucc_datatype_t pscoll_mpi_dtype_to_ucc_dtype(MPI_Datatype datatype)
{
    switch (datatype) {
        case MPI_CHAR:
        case MPI_INT8_T:
        case MPI_SIGNED_CHAR:
            return UCC_DT_INT8;
        case MPI_BYTE:
        case MPI_UINT8_T:
        case MPI_UNSIGNED_CHAR:
            return UCC_DT_UINT8;
        case MPI_INT16_T:
            return UCC_DT_INT16;
        case MPI_UINT16_T:
            return UCC_DT_UINT16;
        case MPI_INT32_T:
            return UCC_DT_INT32;
        case MPI_UINT32_T:
            return UCC_DT_UINT32;
        case MPI_INT64_T:
            return UCC_DT_INT64;
        case MPI_UINT64_T:
            return UCC_DT_UINT64;
        case MPI_SHORT:
            PSCOLL_UCC_DTYPE_MAP_SIGNED(short int);
            break;
        case MPI_INT:
            PSCOLL_UCC_DTYPE_MAP_SIGNED(int);
            break;
        case MPI_LONG:
            PSCOLL_UCC_DTYPE_MAP_SIGNED(long int);
            break;
        case MPI_LONG_LONG:
            PSCOLL_UCC_DTYPE_MAP_SIGNED(long long int);
            break;
        case MPI_UNSIGNED_SHORT:
            PSCOLL_UCC_DTYPE_MAP_UNSIGNED(short int);
            break;
        case MPI_UNSIGNED:
            PSCOLL_UCC_DTYPE_MAP_UNSIGNED(int);
            break;
        case MPI_UNSIGNED_LONG:
            PSCOLL_UCC_DTYPE_MAP_UNSIGNED(long);
            break;
        case MPI_UNSIGNED_LONG_LONG:
            PSCOLL_UCC_DTYPE_MAP_UNSIGNED(long long);
            break;
        case MPI_FLOAT:
            PSCOLL_UCC_DTYPE_MAP_FLOAT(float);
            break;
        case MPI_DOUBLE:
            PSCOLL_UCC_DTYPE_MAP_FLOAT(double);
            break;
        case MPI_LONG_DOUBLE:
            PSCOLL_UCC_DTYPE_MAP_FLOAT(long double);
            break;
        default:
            return PSCOLL_UCC_DTYPE_UNSUPPORTED;
    }
}

#define PSCOLL_UCC_REDUCTION_OP_NULL ((ucc_reduction_op_t)-1)
#define PSCOLL_UCC_REDUCTION_OP_UNSUPPORTED ((ucc_reduction_op_t)-2)

static inline ucc_reduction_op_t pscoll_mpi_op_to_ucc_reduction_op(MPI_Op operation)
{
    switch (operation) {
        case MPI_MAX:
            return UCC_OP_MAX;
        case MPI_MIN:
            return UCC_OP_MIN;
        case MPI_SUM:
            return UCC_OP_SUM;
        case MPI_PROD:
            return UCC_OP_PROD;
        case MPI_LAND:
            return UCC_OP_LAND;
        case MPI_BAND:
            return UCC_OP_BAND;
        case MPI_LOR:
            return UCC_OP_LOR;
        case MPI_BOR:
            return UCC_OP_BOR;
        case MPI_LXOR:
            return UCC_OP_LXOR;
        case MPI_BXOR:
            return UCC_OP_BXOR;
        default:
            return PSCOLL_UCC_REDUCTION_OP_UNSUPPORTED;
    }
}

#define PSCOLL_UCC_CALL_AND_FALLBACK(...)                               \
    ucc_collective_init(&coll_args, &ucc_req, pscoll_ptr->ucc_team);    \
    ucc_status_t status = ucc_collective_post(ucc_req);                 \
    if (UCC_OK != status) {                                             \
        ucc_collective_finalize(ucc_req);                               \
        return MPIX_ERR_FALLBACK;                                       \
    }                                                                   \
    while ((status = ucc_collective_test(ucc_req)) != UCC_OK) {         \
        if (status < 0) {                                               \
            return MPIX_ERR_FALLBACK;                                   \
        }                                                               \
        int flag;                                                       \
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag,  \
                   MPI_STATUS_IGNORE);                                  \
        pscoll_ucc_progress();                                          \
    }                                                                   \
    ucc_collective_finalize(ucc_req);

#define PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(_dtype)                     \
    if (_dtype == PSCOLL_UCC_DTYPE_UNSUPPORTED) {                       \
        return MPIX_ERR_FALLBACK;                                       \
    }

int collops_algorithms(int collop, const void *sbuf, MPI_Count scount, const MPI_Count scounts[],
                       const MPI_Aint sdispls[], MPI_Datatype stype, void *rbuf, MPI_Count rcount,
                       const MPI_Count rcounts[], const MPI_Aint rdispls[], MPI_Datatype rtype,
                       MPI_Op op, int root, MPI_Comm comm, void *extra_comm_state)
{
    ucc_coll_req_h ucc_req;
    pscoll_ucc_comm_t *pscoll_ptr = extra_comm_state;
    assert(comm == pscoll_ptr->comm);

    int comm_rank, comm_size;
    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &comm_size);
    bool is_root = (comm_rank == root);

    bool is_sinplace = (sbuf == MPI_IN_PLACE);
    bool is_rinplace = (rbuf == MPI_IN_PLACE);

    ucc_datatype_t ucc_sdt = PSCOLL_UCC_DTYPE_NULL;
    ucc_datatype_t ucc_rdt = PSCOLL_UCC_DTYPE_NULL;

    uint64_t flags = 0;

    if (!pscoll_ptr->ucc_initialized) {
        return MPIX_ERR_FALLBACK;
    }

    switch (collop) {

        case MPIX_COLLOP_BARRIER:{
                ucc_coll_args_t coll_args = {
                    .mask = 0,
                    .flags = 0,
                    .coll_type = UCC_COLL_TYPE_BARRIER
                };

                PSCOLL_UCC_CALL_AND_FALLBACK();
                pscoll_ucc_counters.barrier++;
                break;
            }
        case MPIX_COLLOP_BCAST:{
                ucc_datatype_t ucc_dt = pscoll_mpi_dtype_to_ucc_dtype(is_root ? stype : rtype);
                PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_dt);

                ucc_coll_args_t coll_args = {
                    .mask = 0,
                    .flags = 0,
                    .coll_type = UCC_COLL_TYPE_BCAST,
                    .root = root,
                    .src.info = {
                                 .buffer = is_root ? (void *) sbuf : rbuf,
                                 .count = is_root ? scount : rcount,
                                 .datatype = ucc_dt,
                                 .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                 }
                };

                PSCOLL_UCC_CALL_AND_FALLBACK();
                pscoll_ucc_counters.bcast++;
                break;
            }
        case MPIX_COLLOP_SCATTER:{
                // SCATTER is *not* yet supported by the UCP TL of UCC (as of v1.4.4)
                return MPIX_ERR_FALLBACK;
                if (is_root) {
                    ucc_sdt = pscoll_mpi_dtype_to_ucc_dtype(stype);
                    PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_sdt);
                    if (!is_rinplace) {
                        ucc_rdt = pscoll_mpi_dtype_to_ucc_dtype(rtype);
                        PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_rdt);
                    }
                } else {
                    ucc_rdt = pscoll_mpi_dtype_to_ucc_dtype(rtype);
                    PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_rdt);
                }

                if (is_root && is_rinplace) {
                    flags |= UCC_COLL_ARGS_FLAG_IN_PLACE;
                }

                ucc_coll_args_t coll_args = {
                    .mask = flags ? UCC_COLL_ARGS_FIELD_FLAGS : 0,
                    .flags = flags,
                    .coll_type = UCC_COLL_TYPE_SCATTER,
                    .root = root,
                    .src.info = {
                                 .buffer = (void *) sbuf,
                                 .count = scount * comm_size,
                                 .datatype = ucc_sdt,
                                 .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                 }
                    ,
                    .dst.info = {
                                 .buffer = rbuf,
                                 .count = rcount,
                                 .datatype = ucc_rdt,
                                 .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                 }
                };

                PSCOLL_UCC_CALL_AND_FALLBACK();
                pscoll_ucc_counters.scatter++;
                break;
            }
        case MPIX_COLLOP_SCATTERV:{
                if (is_root) {
                    ucc_sdt = pscoll_mpi_dtype_to_ucc_dtype(stype);
                    PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_sdt);
                    if (!is_rinplace) {
                        ucc_rdt = pscoll_mpi_dtype_to_ucc_dtype(rtype);
                        PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_rdt);
                    }
                } else {
                    ucc_rdt = pscoll_mpi_dtype_to_ucc_dtype(rtype);
                    PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_rdt);
                }

                if (is_root && is_rinplace) {
                    flags |= UCC_COLL_ARGS_FLAG_IN_PLACE;
                }
                flags |= UCC_COLL_ARGS_FLAG_COUNT_64BIT;
                flags |= UCC_COLL_ARGS_FLAG_DISPLACEMENTS_64BIT;

                ucc_coll_args_t coll_args = {
                    .mask = UCC_COLL_ARGS_FIELD_FLAGS,
                    .flags = flags,
                    .coll_type = UCC_COLL_TYPE_SCATTERV,
                    .root = root,
                    .src.info_v = {
                                   .buffer = (void *) sbuf,
                                   .counts = (ucc_count_t *) scounts,
                                   .displacements = (ucc_aint_t *) sdispls,
                                   .datatype = ucc_sdt,
                                   .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                   }
                    ,
                    .dst.info = {
                                 .buffer = rbuf,
                                 .count = rcount,
                                 .datatype = ucc_rdt,
                                 .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                 }
                };

                PSCOLL_UCC_CALL_AND_FALLBACK();
                pscoll_ucc_counters.scatterv++;
                break;
            }
        case MPIX_COLLOP_GATHER:{
                if (is_root) {
                    ucc_rdt = pscoll_mpi_dtype_to_ucc_dtype(rtype);
                    PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_rdt);
                    if (!is_sinplace) {
                        ucc_sdt = pscoll_mpi_dtype_to_ucc_dtype(stype);
                        PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_sdt);
                    }
                } else {
                    ucc_sdt = pscoll_mpi_dtype_to_ucc_dtype(stype);
                    PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_sdt);
                }

                if (is_sinplace) {
                    assert(is_root);
                    flags |= UCC_COLL_ARGS_FLAG_IN_PLACE;
                }

                ucc_coll_args_t coll_args = {
                    .mask = flags ? UCC_COLL_ARGS_FIELD_FLAGS : 0,
                    .flags = flags,
                    .coll_type = UCC_COLL_TYPE_GATHER,
                    .root = root,
                    .src.info = {
                                 .buffer = (void *) sbuf,
                                 .count = scount,
                                 .datatype = ucc_sdt,
                                 .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                 }
                    ,
                    .dst.info = {
                                 .buffer = rbuf,
                                 .count = rcount * comm_size,
                                 .datatype = ucc_rdt,
                                 .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                 }
                };

                PSCOLL_UCC_CALL_AND_FALLBACK();
                pscoll_ucc_counters.gather++;
                break;
            }
        case MPIX_COLLOP_GATHERV:{
                if (is_root) {
                    ucc_rdt = pscoll_mpi_dtype_to_ucc_dtype(rtype);
                    PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_rdt);
                    if (!is_sinplace) {
                        ucc_sdt = pscoll_mpi_dtype_to_ucc_dtype(stype);
                        PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_sdt);
                    }
                } else {
                    ucc_sdt = pscoll_mpi_dtype_to_ucc_dtype(stype);
                    PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_sdt);
                }

                if (is_sinplace) {
                    assert(is_root);
                    flags |= UCC_COLL_ARGS_FLAG_IN_PLACE;
                }
                flags |= UCC_COLL_ARGS_FLAG_COUNT_64BIT;
                flags |= UCC_COLL_ARGS_FLAG_DISPLACEMENTS_64BIT;

                ucc_coll_args_t coll_args = {
                    .mask = UCC_COLL_ARGS_FIELD_FLAGS,
                    .flags = flags,
                    .coll_type = UCC_COLL_TYPE_GATHERV,
                    .root = root,
                    .src.info = {
                                 .buffer = (void *) sbuf,
                                 .count = scount,
                                 .datatype = ucc_sdt,
                                 .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                 }
                    ,
                    .dst.info_v = {
                                   .buffer = rbuf,
                                   .counts = (ucc_count_t *) rcounts,
                                   .displacements = (ucc_aint_t *) rdispls,
                                   .datatype = ucc_rdt,
                                   .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                   }
                };

                PSCOLL_UCC_CALL_AND_FALLBACK();
                pscoll_ucc_counters.gatherv++;
                break;
            }
        case MPIX_COLLOP_ALLGATHER:{
                ucc_rdt = pscoll_mpi_dtype_to_ucc_dtype(rtype);
                PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_rdt);
                if (!is_sinplace) {
                    ucc_sdt = pscoll_mpi_dtype_to_ucc_dtype(stype);
                    PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_sdt);
                }

                if (is_sinplace) {
                    flags |= UCC_COLL_ARGS_FLAG_IN_PLACE;
                }

                ucc_coll_args_t coll_args = {
                    .mask = flags ? UCC_COLL_ARGS_FIELD_FLAGS : 0,
                    .flags = flags,
                    .coll_type = UCC_COLL_TYPE_ALLGATHER,
                    .src.info = {
                                 .buffer = (void *) sbuf,
                                 .count = scount,
                                 .datatype = ucc_sdt,
                                 .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                 }
                    ,
                    .dst.info = {
                                 .buffer = rbuf,
                                 .count = rcount * comm_size,
                                 .datatype = ucc_rdt,
                                 .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                 }
                };

                PSCOLL_UCC_CALL_AND_FALLBACK();
                pscoll_ucc_counters.allgather++;
                break;
            }
        case MPIX_COLLOP_ALLGATHERV:{
                ucc_rdt = pscoll_mpi_dtype_to_ucc_dtype(rtype);
                PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_rdt);
                if (!is_sinplace) {
                    ucc_sdt = pscoll_mpi_dtype_to_ucc_dtype(stype);
                    PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_sdt);
                }

                if (is_sinplace) {
                    flags |= UCC_COLL_ARGS_FLAG_IN_PLACE;
                }
                flags |= UCC_COLL_ARGS_FLAG_COUNT_64BIT;
                flags |= UCC_COLL_ARGS_FLAG_DISPLACEMENTS_64BIT;

                ucc_coll_args_t coll_args = {
                    .mask = UCC_COLL_ARGS_FIELD_FLAGS,
                    .flags = flags,
                    .coll_type = UCC_COLL_TYPE_ALLGATHERV,
                    .src.info = {
                                 .buffer = (void *) sbuf,
                                 .count = scount,
                                 .datatype = ucc_sdt,
                                 .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                 }
                    ,
                    .dst.info_v = {
                                   .buffer = rbuf,
                                   .counts = (ucc_count_t *) rcounts,
                                   .displacements = (ucc_aint_t *) rdispls,
                                   .datatype = ucc_rdt,
                                   .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                   }
                };

                PSCOLL_UCC_CALL_AND_FALLBACK();
                pscoll_ucc_counters.allgatherv++;
                break;
            }
        case MPIX_COLLOP_ALLTOALL:{
                ucc_rdt = pscoll_mpi_dtype_to_ucc_dtype(rtype);
                PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_rdt);
                if (!is_sinplace) {
                    ucc_sdt = pscoll_mpi_dtype_to_ucc_dtype(stype);
                    PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_sdt);
                }

                if (is_sinplace) {
                    flags |= UCC_COLL_ARGS_FLAG_IN_PLACE;
                }

                ucc_coll_args_t coll_args = {
                    .mask = flags ? UCC_COLL_ARGS_FIELD_FLAGS : 0,
                    .flags = flags,
                    .coll_type = UCC_COLL_TYPE_ALLTOALL,
                    .src.info = {
                                 .buffer = (void *) sbuf,
                                 .count = scount * comm_size,
                                 .datatype = ucc_sdt,
                                 .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                 }
                    ,
                    .dst.info = {
                                 .buffer = rbuf,
                                 .count = rcount * comm_size,
                                 .datatype = ucc_rdt,
                                 .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                 }
                };

                PSCOLL_UCC_CALL_AND_FALLBACK();
                pscoll_ucc_counters.alltoall++;
                break;
            }
        case MPIX_COLLOP_ALLTOALLV:{
                ucc_rdt = pscoll_mpi_dtype_to_ucc_dtype(rtype);
                PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_rdt);
                if (!is_sinplace) {
                    ucc_sdt = pscoll_mpi_dtype_to_ucc_dtype(stype);
                    PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_sdt);
                }

                if (is_sinplace) {
                    flags |= UCC_COLL_ARGS_FLAG_IN_PLACE;
                }
                flags |= UCC_COLL_ARGS_FLAG_COUNT_64BIT;
                flags |= UCC_COLL_ARGS_FLAG_DISPLACEMENTS_64BIT;

                ucc_coll_args_t coll_args = {
                    .mask = UCC_COLL_ARGS_FIELD_FLAGS,
                    .flags = flags,
                    .coll_type = UCC_COLL_TYPE_ALLTOALLV,
                    .src.info_v = {
                                   .buffer = (void *) sbuf,
                                   .counts = (ucc_count_t *) scounts,
                                   .displacements = (ucc_aint_t *) sdispls,
                                   .datatype = ucc_sdt,
                                   .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                   }
                    ,
                    .dst.info_v = {
                                   .buffer = rbuf,
                                   .counts = (ucc_count_t *) rcounts,
                                   .displacements = (ucc_aint_t *) rdispls,
                                   .datatype = ucc_rdt,
                                   .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                   }
                };

                PSCOLL_UCC_CALL_AND_FALLBACK();
                pscoll_ucc_counters.alltoallv++;
                break;
            }
        case MPIX_COLLOP_REDUCE:{
                ucc_reduction_op_t ucc_op = pscoll_mpi_op_to_ucc_reduction_op(op);
                if (ucc_op == PSCOLL_UCC_REDUCTION_OP_UNSUPPORTED) {
                    return MPIX_ERR_FALLBACK;
                }

                if (is_root) {
                    ucc_rdt = pscoll_mpi_dtype_to_ucc_dtype(rtype);
                    PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_rdt);
                    if (!is_sinplace) {
                        assert(stype == rtype);
                    }
                    ucc_sdt = ucc_rdt;
                } else {
                    ucc_sdt = pscoll_mpi_dtype_to_ucc_dtype(stype);
                    PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_sdt);
                }

                if (is_sinplace) {
                    assert(is_root);
                    flags |= UCC_COLL_ARGS_FLAG_IN_PLACE;
                }

                ucc_coll_args_t coll_args = {
                    .mask = flags ? UCC_COLL_ARGS_FIELD_FLAGS : 0,
                    .flags = flags,
                    .coll_type = UCC_COLL_TYPE_REDUCE,
                    .root = root,
                    .src.info = {
                                 .buffer = (void *) sbuf,
                                 .count = scount,
                                 .datatype = ucc_sdt,
                                 .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                 }
                    ,
                    .dst.info = {
                                 .buffer = rbuf,
                                 .count = rcount,
                                 .datatype = ucc_rdt,
                                 .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                 },
                    .op = ucc_op,
                };

                PSCOLL_UCC_CALL_AND_FALLBACK();
                pscoll_ucc_counters.reduce++;
                break;
            }
        case MPIX_COLLOP_ALLREDUCE:{
                ucc_reduction_op_t ucc_op = pscoll_mpi_op_to_ucc_reduction_op(op);
                if (ucc_op == PSCOLL_UCC_REDUCTION_OP_UNSUPPORTED) {
                    return MPIX_ERR_FALLBACK;
                }

                ucc_rdt = pscoll_mpi_dtype_to_ucc_dtype(rtype);
                PSCOLL_UCC_CHECK_DTYPE_AND_FALLBACK(ucc_rdt);
                if (!is_sinplace) {
                    assert(stype == rtype);
                }
                ucc_sdt = ucc_rdt;

                if (is_sinplace) {
                    flags |= UCC_COLL_ARGS_FLAG_IN_PLACE;
                }

                ucc_coll_args_t coll_args = {
                    .mask = flags ? UCC_COLL_ARGS_FIELD_FLAGS : 0,
                    .flags = flags,
                    .coll_type = UCC_COLL_TYPE_ALLREDUCE,
                    .src.info = {
                                 .buffer = (void *) sbuf,
                                 .count = scount,
                                 .datatype = ucc_sdt,
                                 .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                 }
                    ,
                    .dst.info = {
                                 .buffer = rbuf,
                                 .count = rcount,
                                 .datatype = ucc_rdt,
                                 .mem_type = UCC_MEMORY_TYPE_UNKNOWN,
                                 },
                    .op = ucc_op,
                };

                PSCOLL_UCC_CALL_AND_FALLBACK();
                pscoll_ucc_counters.allreduce++;
                break;
            }
        default:
            return MPIX_ERR_FALLBACK;
    }

    return MPI_SUCCESS;
}

int collops_deregister(void *_extra_state)
{
    pscoll_ucc_extra_state_t *extra_state = _extra_state;
    assert(extra_state == &pscoll_ucc_extra_state);

    char *envval = getenv("PSCOLL_COLLOPS_STATS");
    if (envval && strstr(envval, "ucc")) {
        printf
            ("=== pscoll_ucc stats === (%d) === Barrier: %lld | Bcast: %lld | Gather: %lld | Gatherv: %lld | Scatter: %lld | Scatterv: %lld |"
             "Allgather: %lld | Allgatherv: %lld | Alltoall: %lld | Alltoallv: %lld | Reduce: %lld | Allreduce: %lld\n",
             extra_state->world_rank, pscoll_ucc_counters.barrier, pscoll_ucc_counters.bcast,
             pscoll_ucc_counters.gather, pscoll_ucc_counters.gatherv, pscoll_ucc_counters.scatter,
             pscoll_ucc_counters.scatterv, pscoll_ucc_counters.allgather,
             pscoll_ucc_counters.allgatherv, pscoll_ucc_counters.alltoall,
             pscoll_ucc_counters.alltoallv, pscoll_ucc_counters.reduce,
             pscoll_ucc_counters.allreduce);
    }

    return MPI_SUCCESS;
}

/* entry function for collops plugin */
int collops_register(const char *name, MPI_Info info)
{
    int rank, size;
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    pscoll_ucc_extra_state.world_size = size;
    pscoll_ucc_extra_state.world_rank = rank;

    if (strcmp(name, "pscoll_ucc") == 0) {
        MPIX_Register_collops(name, MPIX_COLLOP_ALL, 1, collops_algorithms,
                              MPIX_COLLOPS_PROGRESS_FN_NULL, collops_comm_init, collops_comm_free,
                              collops_deregister, MPI_INFO_NULL, &pscoll_ucc_extra_state);
    }

    return MPI_SUCCESS;
}
#endif /* !USE_COLLOPS_PLUGIN */

#ifndef __WITHOUT_MAIN
int main(int argc, char *argv[])
{
    int errs = 0;
    MPI_Info info;
    int rank, size;

    MPI_Init(&argc, &argv);

    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

#ifndef USE_COLLOPS_PLUGIN
    /* Register and use "pscoll" explicitly. */
    collops_register("pscoll_ucc", MPI_INFO_NULL);
    MPI_Info_create(&info);
    MPI_Info_set(info, "collops", "pscoll_ucc");
    MPI_Comm_set_info(MPI_COMM_WORLD, info);
    MPI_Info_free(&info);
#endif

    MPI_Barrier(MPI_COMM_WORLD);

    int n = 1000;

    MPI_Bcast(&n, 1, MPI_INT, size - 1, MPI_COMM_WORLD);

    double h = 1.0 / (double) n;
    double x, s = 0.0;

    for (int i = rank + 1; i <= n; i += size) {
        x = h * ((double) i - 0.5);
        s += (4.0 / (1.0 + x * x));
    }

    double pi = 0.0;
    double p = h * s;

    MPI_Allreduce(&p, &pi, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

    if ((pi < 3.141590) || (pi > 3.141595)) {
        fprintf(stderr, "(%d) ERROR: Got %.7f as pi.\n", rank, pi);
        errs++;
    }

    MPI_Finalize();

    if (!errs) {
        if (!rank)
            printf(" No Errors\n");
        return 0;
    }
    return 1;
}
#endif /* !__WITHOUT_MAIN */
