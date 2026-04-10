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
#include <stdio.h>
#include <string.h>

#ifdef MAKE_COLLOPS_PLUGIN
/* In this plugin mode, only the callbacks are compiled and the main
 * function is omitted. This allows to generate a collops plugin
 * with these callbacks that can then be used by a separate test.
 */
#define __WITHOUT_MAIN
#endif /* MAKE_COLLOPS_PLUGIN */

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
} pscoll_mpi_counters_t;

pscoll_mpi_counters_t pscoll_mpi_counters = { 0 };

typedef struct {
    int world_rank;
    int world_size;
} pscoll_mpi_extra_state_t;

pscoll_mpi_extra_state_t pscoll_mpi_extra_state;

int collops_comm_init(MPI_Comm comm, void *extra_state, void *extra_comm_state)
{
    void **extra_comm_state_ = extra_comm_state;
    assert(extra_state == &pscoll_mpi_extra_state);

    void *extra_comm_state_ptr = malloc(sizeof(MPI_Comm));
    *(MPI_Comm *) extra_comm_state_ptr = comm;

    *extra_comm_state_ = extra_comm_state_ptr;
    return MPI_SUCCESS;
}

int collops_comm_free(void *extra_comm_state)
{
    assert(extra_comm_state != NULL);
    return MPI_SUCCESS;
}

#define CHECK_AND_FALLBACK(...)			\
    if (mpi_errno != MPI_SUCCESS) {		\
	return MPIX_ERR_FALLBACK;		\
    }

int collops_algorithms(int collop, const void *sbuf, MPI_Count scount, const MPI_Count scounts[],
                       const MPI_Aint sdispls[], MPI_Datatype stype, void *rbuf, MPI_Count rcount,
                       const MPI_Count rcounts[], const MPI_Aint rdispls[], MPI_Datatype rtype,
                       MPI_Op op, int root, MPI_Comm comm, void *extra_comm_state)
{
    int mpi_errno = MPI_SUCCESS;
    MPI_Comm _comm = *((MPI_Comm *) extra_comm_state);
    assert(_comm == comm);
    assert(sizeof(MPI_Aint) == sizeof(MPI_Count));

    int rank;
    MPI_Comm_rank(comm, &rank);

    switch (collop) {
        case MPIX_COLLOP_BARRIER:
            mpi_errno = MPI_Barrier(comm);
            if (mpi_errno != MPI_SUCCESS) {
                return MPIX_ERR_FALLBACK;
            }
            pscoll_mpi_counters.barrier++;
            return MPI_SUCCESS;
        case MPIX_COLLOP_BCAST:
            if (rank == root) {
                mpi_errno = MPI_Bcast_c((void *) sbuf, scount, stype, root, comm);
            } else {
                mpi_errno = MPI_Bcast_c(rbuf, rcount, rtype, root, comm);
            }
            if (mpi_errno != MPI_SUCCESS) {
                return MPIX_ERR_FALLBACK;
            }
            pscoll_mpi_counters.bcast++;
            return MPI_SUCCESS;
        case MPIX_COLLOP_SCATTER:
            if (rank == root) {
                mpi_errno = MPI_Scatter_c(sbuf, scount, stype, rbuf, rcount, rtype, root, comm);
            } else {
                mpi_errno =
                    MPI_Scatter_c(NULL, 0, MPI_DATATYPE_NULL, rbuf, rcount, rtype, root, comm);
            }
            if (mpi_errno != MPI_SUCCESS) {
                return MPIX_ERR_FALLBACK;
            }
            pscoll_mpi_counters.scatter++;
            return MPI_SUCCESS;
        case MPIX_COLLOP_SCATTERV:
            if (rank == root) {
                mpi_errno =
                    MPI_Scatterv_c(sbuf, scounts, sdispls, stype, rbuf, rcount, rtype, root, comm);
            } else {
                mpi_errno =
                    MPI_Scatterv_c(NULL, NULL, NULL, MPI_DATATYPE_NULL, rbuf, rcount, rtype, root,
                                   comm);
            }
            if (mpi_errno != MPI_SUCCESS) {
                return MPIX_ERR_FALLBACK;
            }
            pscoll_mpi_counters.scatterv++;
            return MPI_SUCCESS;
        case MPIX_COLLOP_GATHER:
            if (rank == root) {
                mpi_errno = MPI_Gather_c(sbuf, scount, stype, rbuf, rcount, rtype, root, comm);
            } else {
                mpi_errno =
                    MPI_Gather_c(sbuf, scount, stype, NULL, 0, MPI_DATATYPE_NULL, root, comm);
            }
            if (mpi_errno != MPI_SUCCESS) {
                return MPIX_ERR_FALLBACK;
            }
            pscoll_mpi_counters.gather++;
            return MPI_SUCCESS;
        case MPIX_COLLOP_GATHERV:
            if (rank == root) {
                mpi_errno =
                    MPI_Gatherv_c(sbuf, scount, stype, rbuf, rcounts, rdispls, rtype, root, comm);
            } else {
                mpi_errno =
                    MPI_Gatherv_c(sbuf, scount, stype, NULL, 0, NULL, MPI_DATATYPE_NULL, root,
                                  comm);
            }
            if (mpi_errno != MPI_SUCCESS) {
                return MPIX_ERR_FALLBACK;
            }
            pscoll_mpi_counters.gatherv++;
            return MPI_SUCCESS;
        case MPIX_COLLOP_ALLGATHER:
            mpi_errno = MPI_Allgather_c(sbuf, scount, stype, rbuf, rcount, rtype, comm);
            if (mpi_errno != MPI_SUCCESS) {
                return MPIX_ERR_FALLBACK;
            }
            pscoll_mpi_counters.allgather++;
            return MPI_SUCCESS;
        case MPIX_COLLOP_ALLGATHERV:
            mpi_errno = MPI_Allgatherv_c(sbuf, scount, stype, rbuf, rcounts, rdispls, rtype, comm);
            if (mpi_errno != MPI_SUCCESS) {
                return MPIX_ERR_FALLBACK;
            }
            pscoll_mpi_counters.allgatherv++;
            return MPI_SUCCESS;
        case MPIX_COLLOP_ALLTOALL:
            mpi_errno = MPI_Alltoall_c(sbuf, scount, stype, rbuf, rcount, rtype, comm);
            if (mpi_errno != MPI_SUCCESS) {
                return MPIX_ERR_FALLBACK;
            }
            pscoll_mpi_counters.alltoall++;
            return MPI_SUCCESS;
        case MPIX_COLLOP_ALLTOALLV:
            mpi_errno =
                MPI_Alltoallv_c(sbuf, scounts, sdispls, stype, rbuf, rcounts, rdispls, rtype, comm);
            if (mpi_errno != MPI_SUCCESS) {
                return MPIX_ERR_FALLBACK;
            }
            pscoll_mpi_counters.alltoallv++;
            return MPI_SUCCESS;
        case MPIX_COLLOP_REDUCE:
            if (rank == root) {
                mpi_errno = MPI_Reduce_c(sbuf, rbuf, scount, stype, op, root, comm);
            } else {
                mpi_errno = MPI_Reduce_c(sbuf, NULL, scount, stype, op, root, comm);
            }
            if (mpi_errno != MPI_SUCCESS) {
                return MPIX_ERR_FALLBACK;
            }
            pscoll_mpi_counters.reduce++;
            return MPI_SUCCESS;
        case MPIX_COLLOP_ALLREDUCE:
            mpi_errno = MPI_Allreduce_c(sbuf, rbuf, scount, stype, op, comm);
            if (mpi_errno != MPI_SUCCESS) {
                return MPIX_ERR_FALLBACK;
            }
            pscoll_mpi_counters.allreduce++;
            return MPI_SUCCESS;
        default:
            return MPIX_ERR_FALLBACK;
    }

    return MPI_SUCCESS;
}

int collops_deregister(void *_extra_state)
{
    pscoll_mpi_extra_state_t *extra_state = _extra_state;
    assert(extra_state == &pscoll_mpi_extra_state);

    char *envval = getenv("PSCOLL_COLLOPS_STATS");
    if (envval && strstr(envval, "mpi")) {
        printf
            ("=== pscoll_mpi stats === (%d) === Barrier: %lld | Bcast: %lld | Gather: %lld | Gatherv: %lld | Scatter: %lld | Scatterv: %lld |"
             "Allgather: %lld | Allgatherv: %lld | Alltoall: %lld | Alltoallv: %lld | Reduce: %lld | Allreduce: %lld\n",
             extra_state->world_rank, pscoll_mpi_counters.barrier, pscoll_mpi_counters.bcast,
             pscoll_mpi_counters.gather, pscoll_mpi_counters.gatherv,
             pscoll_mpi_counters.scatter, pscoll_mpi_counters.scatterv,
             pscoll_mpi_counters.allgather, pscoll_mpi_counters.allgatherv,
             pscoll_mpi_counters.alltoall, pscoll_mpi_counters.alltoallv,
             pscoll_mpi_counters.reduce, pscoll_mpi_counters.allreduce);
    }

    return MPI_SUCCESS;
}

/* entry function for collops plugin */
int collops_register(const char *name, MPI_Info info)
{
    int rank, size;
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    pscoll_mpi_extra_state.world_size = size;
    pscoll_mpi_extra_state.world_rank = rank;

    if (strcmp(name, "pscoll_mpi") == 0) {
        MPIX_Register_collops(name, MPIX_COLLOP_ALL, 0, collops_algorithms,
                              MPIX_COLLOPS_PROGRESS_FN_NULL, collops_comm_init, collops_comm_free,
                              collops_deregister, MPI_INFO_NULL, &pscoll_mpi_extra_state);
    }

    return MPI_SUCCESS;
}

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
    collops_register("pscoll_mpi", MPI_INFO_NULL);
    MPI_Info_create(&info);
    MPI_Info_set(info, "collops", "pscoll_mpi");
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

    double pi, p = h * s;

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
