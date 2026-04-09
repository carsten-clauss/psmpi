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
#include <stdio.h>
#include <string.h>

#ifndef USE_COLLOPS_PLUGIN
#include "gloo_wrapper.h"

#ifdef MAKE_COLLOPS_PLUGIN
/* In this plugin mode, only the callbacks are compiled and the main
 * function is omitted. This allows to generate a collops plugin
 * with these callbacks that can then be used by a separate test.
 */
#define __WITHOUT_MAIN
#endif /* MAKE_COLLOPS_PLUGIN */

typedef enum {
    PSCOLL_GLOO_RETVAL_ERROR = -1,
    PSCOLL_GLOO_RETVAL_SUCCESS = 0,
    PSCOLL_GLOO_RETVAL_FALLBACK = 1,
} pscoll_gloo_error_t;

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
} pscoll_gloo_counters_t;

pscoll_gloo_counters_t pscoll_gloo_counters = { 0 };

typedef struct {
    int world_rank;
    int world_size;
} pscoll_gloo_extra_state_t;

pscoll_gloo_extra_state_t pscoll_gloo_extra_state;

int collops_comm_init(MPI_Comm comm, void *extra_state, void *extra_comm_state)
{
    static int pscoll_gloo_is_initializing = 0;
    void **extra_comm_state_ptr = extra_comm_state;
    assert(extra_state == &pscoll_gloo_extra_state);

    if (pscoll_gloo_is_initializing) {
        /* Avoid endless recursions... */
        return MPIX_ERR_FALLBACK;
    }

    pscoll_gloo_is_initializing = 1;
    gloo_context_t ctx = gloo_mpi_create_context(comm);
    pscoll_gloo_is_initializing = 0;

    *extra_comm_state_ptr = ctx;
    return MPI_SUCCESS;
}

int collops_comm_free(void *extra_comm_state)
{
    assert(extra_comm_state != NULL);

    gloo_context_t ctx = extra_comm_state;

    gloo_mpi_free_context(ctx);

    return MPI_SUCCESS;
}

int collops_algorithms(int collop, const void *sbuf, MPI_Count scount, const MPI_Count scounts[],
                       const MPI_Aint sdispls[], MPI_Datatype stype, void *rbuf, MPI_Count rcount,
                       const MPI_Count rcounts[], const MPI_Aint rdispls[], MPI_Datatype rtype,
                       MPI_Op op, int root, MPI_Comm comm, void *extra_comm_state)
{
    gloo_context_t ctx = extra_comm_state;

    int comm_rank, comm_size;
    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &comm_size);
    int is_root = (comm_rank == root);
    int is_sinplace = (sbuf == MPI_IN_PLACE);
    int is_rinplace = (rbuf == MPI_IN_PLACE);

    int dtype_size = 0;
    void *tmpbuf = NULL;

    switch (collop) {

        case MPIX_COLLOP_BARRIER:{
                if (gloo_mpi_barrier(ctx)) {
                    goto fn_fallback;
                }
                pscoll_gloo_counters.barrier++;
                goto fn_success;
            }
        case MPIX_COLLOP_BCAST:{
                if (is_root) {
                    if (gloo_mpi_broadcast(ctx, (void *) sbuf, scount, stype, root)) {
                        goto fn_fallback;
                    }
                } else {
                    if (gloo_mpi_broadcast(ctx, rbuf, rcount, rtype, root)) {
                        goto fn_fallback;
                    }
                }
                pscoll_gloo_counters.bcast++;
                goto fn_success;
                return MPI_SUCCESS;
            }
        case MPIX_COLLOP_GATHER:{
                if (is_root && is_sinplace) {
                    tmpbuf = malloc(dtype_size * rcount);
                    memcpy(tmpbuf, rbuf + dtype_size * rcount + comm_rank, dtype_size * rcount);
                    sbuf = (const void *) tmpbuf;
                }
                if (gloo_mpi_gather(ctx, (void *) sbuf, scount, stype, rbuf, rcount, rtype, root)) {
                    goto fn_fallback;
                }
                pscoll_gloo_counters.gather++;
                goto fn_success;
            }
        case MPIX_COLLOP_SCATTER:{
                void *sbufs[comm_size];
                if (is_root) {
                    MPI_Type_size(stype, &dtype_size);
                    for (int i = 0; i < comm_size; i++) {
                        sbufs[i] = (void *) sbuf + scount * dtype_size * i;
                    }
                    if (is_rinplace) {
                        /* reveive into a dummy segment */
                        tmpbuf = malloc(dtype_size * scount);
                        rbuf = tmpbuf;
                        rcount = scount;
                        rtype = stype;
                    }
                }
                if (gloo_mpi_scatter
                    (ctx, sbufs, is_root ? scount : 0, stype, rbuf, rcount, rtype, root,
                     comm_size)) {
                    goto fn_fallback;
                }
                pscoll_gloo_counters.scatter++;
                goto fn_success;
            }
        case MPIX_COLLOP_ALLGATHER:{
                if (is_sinplace) {
                    /* in contrast to gather, Gloo's allgather can handle the in-place case */
                    sbuf = NULL;
                }
                if (gloo_mpi_allgather(ctx, (void *) sbuf, scount, stype, rbuf, rcount, rtype)) {
                    goto fn_fallback;
                }
                pscoll_gloo_counters.allgather++;
                goto fn_success;
            }
        case MPIX_COLLOP_REDUCE:{
                MPI_Type_size(stype, &dtype_size);
                if (!is_root) {
                    tmpbuf = rbuf = malloc(dtype_size * scount);
                } else if (sbuf == MPI_IN_PLACE) {
                    tmpbuf = malloc(dtype_size * scount);
                    memcpy(tmpbuf, rbuf, dtype_size * scount);
                    sbuf = (const void *) tmpbuf;
                }
                if (gloo_mpi_reduce(ctx, (void *) sbuf, rbuf, scount, stype, op, root)) {
                    goto fn_fallback;
                }
                pscoll_gloo_counters.reduce++;
                goto fn_success;
            }
        case MPIX_COLLOP_ALLREDUCE:{
                MPI_Type_size(stype, &dtype_size);
                if (sbuf == MPI_IN_PLACE) {
                    tmpbuf = malloc(dtype_size * scount);
                    memcpy(tmpbuf, rbuf, dtype_size * scount);
                    sbuf = (const void *) tmpbuf;
                }
                if (gloo_mpi_allreduce(ctx, (void *) sbuf, rbuf, scount, stype, op)) {
                    goto fn_fallback;
                }
                pscoll_gloo_counters.allreduce++;
                goto fn_success;
            }
        default:
            goto fn_fallback;
    }

  fn_success:
    return MPI_SUCCESS;
  fn_fallback:
    if (tmpbuf) {
        free(tmpbuf);
    }
    return MPIX_ERR_FALLBACK;
}

int collops_deregister(void *_extra_state)
{
    pscoll_gloo_extra_state_t *extra_state = _extra_state;
    assert(extra_state == &pscoll_gloo_extra_state);

    char *envval = getenv("PSCOLL_COLLOPS_STATS");
    if (envval && strstr(envval, "gloo")) {
        printf
            ("=== pscoll_gloo stats === (%d) === Barrier: %lld | Bcast: %lld | Gather: %lld | Gatherv: %lld | Scatter: %lld | Scatterv: %lld |"
             "Allgather: %lld | Allgatherv: %lld | Alltoall: %lld | Alltoallv: %lld | Reduce: %lld | Allreduce: %lld\n",
             extra_state->world_rank, pscoll_gloo_counters.barrier, pscoll_gloo_counters.bcast,
             pscoll_gloo_counters.gather, pscoll_gloo_counters.gatherv,
             pscoll_gloo_counters.scatter, pscoll_gloo_counters.scatterv,
             pscoll_gloo_counters.allgather, pscoll_gloo_counters.allgatherv,
             pscoll_gloo_counters.alltoall, pscoll_gloo_counters.alltoallv,
             pscoll_gloo_counters.reduce, pscoll_gloo_counters.allreduce);
    }

    return MPI_SUCCESS;
}

/* entry function for collops plugin */
int collops_register(const char *name, MPI_Info info)
{
    int rank, size;
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    pscoll_gloo_extra_state.world_size = size;
    pscoll_gloo_extra_state.world_rank = rank;

    if (strcmp(name, "pscoll_gloo") == 0) {
        MPIX_Register_collops(name,
                              MPIX_COLLOP_BARRIER + MPIX_COLLOP_BCAST + MPIX_COLLOP_REDUCE +
                              MPIX_COLLOP_ALLREDUCE + MPIX_COLLOP_GATHER + MPIX_COLLOP_ALLGATHER +
                              MPIX_COLLOP_SCATTER, 1, collops_algorithms,
                              MPIX_COLLOPS_PROGRESS_FN_NULL, collops_comm_init, collops_comm_free,
                              collops_deregister, MPI_INFO_NULL, &pscoll_gloo_extra_state);
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
    collops_register("pscoll_gloo", MPI_INFO_NULL);
    MPI_Info_create(&info);
    MPI_Info_set(info, "collops", "pscoll_gloo");
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
