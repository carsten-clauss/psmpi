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
#include <unistd.h>
#include <string.h>
#include <math.h>

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

#define NBINS_DOUBLE 64
#define NBINS_FLOAT 32

typedef struct {
    int64_t bin[NBINS_DOUBLE];
} binned_double_t;

typedef struct {
    int32_t bin[NBINS_FLOAT];
} binned_float_t;

typedef struct {
    MPI_Datatype binned_mpi_dtype_double;
    MPI_Datatype binned_mpi_dtype_float;
    MPI_Op binned_mpi_op_double;
    MPI_Op binned_mpi_op_float;
} binned_mpi_t;

static inline void binned_add_double(binned_double_t * acc, double x)
{
    if (x == 0.0)
        return;

    int exp;
    double m = frexp(x, &exp);

    int idx = exp + NBINS_DOUBLE / 2;
    if (idx < 0)
        idx = 0;
    if (idx >= NBINS_DOUBLE)
        idx = NBINS_DOUBLE - 1;

    int64_t q = (int64_t) (m * (double) (1ULL << 52));
    acc->bin[idx] += q;
}

static inline void binned_add_float(binned_float_t * acc, float x)
{
    if (x == 0.0)
        return;

    int exp;
    float m = frexp(x, &exp);

    int idx = exp + NBINS_FLOAT / 2;
    if (idx < 0)
        idx = 0;
    if (idx >= NBINS_FLOAT)
        idx = NBINS_FLOAT - 1;

    int32_t q = (int32_t) (m * (float) (1ULL << 23));
    acc->bin[idx] += q;
}

static void binned_merge_double(void *in, void *inout, MPI_Count * len, MPI_Datatype * dtype)
{
    binned_double_t *a = (binned_double_t *) in;
    binned_double_t *b = (binned_double_t *) inout;

    for (MPI_Count i = 0; i < *len; i++) {
        for (int j = 0; j < NBINS_DOUBLE; j++)
            b[i].bin[j] += a[i].bin[j];
    }
}

static void binned_merge_float(void *in, void *inout, MPI_Count * len, MPI_Datatype * dtype)
{
    binned_float_t *a = (binned_float_t *) in;
    binned_float_t *b = (binned_float_t *) inout;

    for (MPI_Count i = 0; i < *len; i++) {
        for (int j = 0; j < NBINS_FLOAT; j++)
            b[i].bin[j] += a[i].bin[j];
    }
}

static inline double binned_to_double(const binned_double_t * acc)
{
    double sum = 0.0;

    for (int i = 0; i < NBINS_DOUBLE; i++) {
        int exp = i - NBINS_DOUBLE / 2;
        double scale = ldexp(1.0, exp);
        sum += (double) acc->bin[i] * scale / (double) (1ULL << 52);
    }
    return sum;
}

static inline float binned_to_float(const binned_float_t * acc)
{
    float sum = 0.0;

    for (int i = 0; i < NBINS_FLOAT; i++) {
        int exp = i - NBINS_FLOAT / 2;
        float scale = ldexp(1.0, exp);
        sum += (float) acc->bin[i] * scale / (float) (1ULL << 23);
    }
    return sum;
}

static int binned_allreduce_double(const double *sendbuf, double *recvbuf, MPI_Count count,
                                   MPI_Comm comm, void *extra_comm_state)
{
    binned_mpi_t *binned_mpi = extra_comm_state;
    binned_double_t local[count];
    binned_double_t global[count];

    for (MPI_Count i = 0; i < count; i++) {
        for (int b = 0; b < NBINS_DOUBLE; b++)
            local[i].bin[b] = 0;

        double x = ((double *) sendbuf)[i];
        binned_add_double(&local[i], x);
    }

    MPI_Allreduce_c(local, global, count, binned_mpi->binned_mpi_dtype_double,
                    binned_mpi->binned_mpi_op_double, comm);

    for (int i = 0; i < count; i++)
        ((double *) recvbuf)[i] = binned_to_double(&global[i]);

    return MPI_SUCCESS;
}

static int binned_allreduce_float(const float *sendbuf, float *recvbuf, MPI_Count count,
                                  MPI_Comm comm, void *extra_comm_state)
{
    binned_mpi_t *binned_mpi = extra_comm_state;
    binned_float_t local[count];
    binned_float_t global[count];

    for (MPI_Count i = 0; i < count; i++) {
        for (int b = 0; b < NBINS_FLOAT; b++)
            local[i].bin[b] = 0;

        float x = ((float *) sendbuf)[i];
        binned_add_float(&local[i], x);
    }

    MPI_Allreduce_c(local, global, count, binned_mpi->binned_mpi_dtype_float,
                    binned_mpi->binned_mpi_op_float, comm);

    for (int i = 0; i < count; i++)
        ((float *) recvbuf)[i] = binned_to_float(&global[i]);

    return MPI_SUCCESS;
}

static int binned_reduce_double(const double *sendbuf, double *recvbuf, MPI_Count count, int root,
                                MPI_Comm comm, void *extra_comm_state)
{
    binned_mpi_t *binned_mpi = extra_comm_state;
    binned_double_t local[count];
    binned_double_t global[count];

    for (MPI_Count i = 0; i < count; i++) {
        for (int b = 0; b < NBINS_DOUBLE; b++)
            local[i].bin[b] = 0;

        double x = ((double *) sendbuf)[i];
        binned_add_double(&local[i], x);
    }

    MPI_Reduce_c(local, global, count, binned_mpi->binned_mpi_dtype_double,
                 binned_mpi->binned_mpi_op_double, root, comm);

    int rank;
    MPI_Comm_rank(comm, &rank);
    if (rank == root) {
        for (int i = 0; i < count; i++)
            ((double *) recvbuf)[i] = binned_to_double(&global[i]);
    }

    return MPI_SUCCESS;
}

static int binned_reduce_float(const float *sendbuf, float *recvbuf, MPI_Count count, int root,
                               MPI_Comm comm, void *extra_comm_state)
{
    binned_mpi_t *binned_mpi = extra_comm_state;
    binned_float_t local[count];
    binned_float_t global[count];

    for (MPI_Count i = 0; i < count; i++) {
        for (int b = 0; b < NBINS_FLOAT; b++)
            local[i].bin[b] = 0;

        float x = ((float *) sendbuf)[i];
        binned_add_float(&local[i], x);
    }

    MPI_Reduce_c(local, global, count, binned_mpi->binned_mpi_dtype_float,
                 binned_mpi->binned_mpi_op_float, root, comm);

    int rank;
    MPI_Comm_rank(comm, &rank);
    if (rank == root) {
        for (int i = 0; i < count; i++)
            ((float *) recvbuf)[i] = binned_to_float(&global[i]);
    }

    return MPI_SUCCESS;
}

int collops_comm_init(MPI_Comm comm, void *extra_state, void *extra_comm_state)
{
    void **extra_comm_state_ = extra_comm_state;
    assert(extra_state == &pscoll_mpi_extra_state);

    binned_mpi_t *binned_mpi = malloc(sizeof(binned_mpi_t));

    MPI_Type_contiguous(NBINS_DOUBLE, MPI_INT64_T, &binned_mpi->binned_mpi_dtype_double);
    MPI_Type_commit(&binned_mpi->binned_mpi_dtype_double);
    MPI_Op_create_c(binned_merge_double, 1, &binned_mpi->binned_mpi_op_double);

    MPI_Type_contiguous(NBINS_FLOAT, MPI_INT32_T, &binned_mpi->binned_mpi_dtype_float);
    MPI_Type_commit(&binned_mpi->binned_mpi_dtype_float);
    MPI_Op_create_c(binned_merge_float, 1, &binned_mpi->binned_mpi_op_float);

    *extra_comm_state_ = binned_mpi;
    return MPI_SUCCESS;
}

int collops_comm_free(void *extra_comm_state)
{
    assert(extra_comm_state);

    binned_mpi_t *binned_mpi = extra_comm_state;

    MPI_Type_free(&binned_mpi->binned_mpi_dtype_double);
    MPI_Type_free(&binned_mpi->binned_mpi_dtype_float);
    MPI_Op_free(&binned_mpi->binned_mpi_op_double);
    MPI_Op_free(&binned_mpi->binned_mpi_op_float);

    return MPI_SUCCESS;
}

int collops_algorithms(int collop, const void *sbuf, MPI_Count scount, const MPI_Count scounts[],
                       const MPI_Aint sdispls[], MPI_Datatype stype, void *rbuf, MPI_Count rcount,
                       const MPI_Count rcounts[], const MPI_Aint rdispls[], MPI_Datatype rtype,
                       MPI_Op op, int root, MPI_Comm comm, void *extra_comm_state)
{
    int mpi_errno = MPI_SUCCESS;
    assert(sizeof(MPI_Aint) == sizeof(MPI_Count));

    int rank;
    MPI_Comm_rank(comm, &rank);

    if (op != MPI_SUM) {
        return MPIX_ERR_FALLBACK;
    }

    switch (collop) {
        case MPIX_COLLOP_REDUCE:
            if (rank == root) {
                if (stype == MPI_DOUBLE)
                    mpi_errno =
                        binned_reduce_double(sbuf, rbuf, scount, root, comm, extra_comm_state);
                else if (stype == MPI_FLOAT)
                    mpi_errno =
                        binned_reduce_float(sbuf, rbuf, scount, root, comm, extra_comm_state);
                else
                    return MPIX_ERR_FALLBACK;
            } else {
                if (stype == MPI_DOUBLE)
                    mpi_errno =
                        binned_reduce_double(sbuf, NULL, scount, root, comm, extra_comm_state);
                else if (stype == MPI_FLOAT)
                    mpi_errno =
                        binned_reduce_double(sbuf, NULL, scount, root, comm, extra_comm_state);
                else
                    return MPIX_ERR_FALLBACK;
            }
            if (mpi_errno != MPI_SUCCESS) {
                return MPIX_ERR_FALLBACK;
            }
            pscoll_mpi_counters.reduce++;
            return MPI_SUCCESS;
        case MPIX_COLLOP_ALLREDUCE:
            if (stype == MPI_DOUBLE)
                mpi_errno = binned_allreduce_double(sbuf, rbuf, scount, comm, extra_comm_state);
            else if (stype == MPI_FLOAT)
                mpi_errno = binned_allreduce_float(sbuf, rbuf, scount, comm, extra_comm_state);
            else
                return MPIX_ERR_FALLBACK;
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
    if (envval && strstr(envval, "binsum")) {
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

    if (strcmp(name, "pscoll_binsum") == 0) {
        MPIX_Register_collops(name, MPIX_COLLOP_REDUCE + MPIX_COLLOP_ALLREDUCE, 0,
                              collops_algorithms, MPIX_COLLOPS_PROGRESS_FN_NULL, collops_comm_init,
                              collops_comm_free, collops_deregister, MPI_INFO_NULL,
                              &pscoll_mpi_extra_state);
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
    collops_register("pscoll_binsum", MPI_INFO_NULL);
    MPI_Info_create(&info);
    MPI_Info_set(info, "collops", "pscoll_binsum");
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

    float pi = 0.0;
    float p = h * s;

    MPI_Allreduce(&p, &pi, 1, MPI_FLOAT, MPI_SUM, MPI_COMM_WORLD);

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
