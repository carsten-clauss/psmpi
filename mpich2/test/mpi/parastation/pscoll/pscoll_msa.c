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
#include <unistd.h>

#define PSCOLL_MSA_BCAST_SHORT_MSG_SIZE 12288

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
    unsigned long long int reduce;
    unsigned long long int allreduce;
} pscoll_msa_counters_t;

pscoll_msa_counters_t pscoll_msa_counters = { 0 };

typedef struct {
    int world_rank;
    int world_size;
} pscoll_msa_extra_state_t;

typedef enum {
    pscoll_msa_level_none = 0,
    pscoll_msa_level_nodes,
    pscoll_msa_level_modules,
    pscoll_msa_level_max = pscoll_msa_level_modules
} pscoll_msa_awareness_level_t;

typedef struct {
    MPI_Comm comm;
    MPI_Comm local_comm;
    MPI_Comm roots_comm;
    int local_root_rank;
    int *intra_table;
    int *inter_table;
    pscoll_msa_awareness_level_t msa_awareness_level;
} pscoll_msa_extra_comm_state_t;

pscoll_msa_extra_state_t pscoll_msa_extra_state;

/* This is essentially `MPIR_Find_local()` from `mpich2/src/util/mpir_localproc.c`: */
static void pscoll_msa_find_local(MPI_Comm comm, int *local_size_p, int *local_rank_p,
                                  int **local_ranks_p, int **intra_table_p, int *rank2id_table)
{
    int i, local_size, local_rank;
    int *local_ranks, *intra_table;
    int id, my_id;
    int comm_rank, comm_size;

    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &comm_size);

    local_ranks = malloc(sizeof(int) * comm_size);
    intra_table = malloc(sizeof(int) * comm_size);

    for (i = 0; i < comm_size; ++i)
        intra_table[i] = -1;

    my_id = rank2id_table[comm_rank];

    local_size = 0;
    local_rank = -1;

    /* scan through the list of processes in comm */
    for (i = 0; i < comm_size; ++i) {
        id = rank2id_table[i];

        /* build list of local processes */
        if (id == my_id) {
            if (i == comm_rank)
                local_rank = local_size;

            intra_table[i] = local_size;
            local_ranks[local_size] = i;
            ++local_size;
        }
    }

    *local_size_p = local_size;
    *local_rank_p = local_rank;

    if (local_ranks_p)
        *local_ranks_p = realloc(local_ranks, sizeof(int) * local_size);
    else
        free(local_ranks_p);

    if (intra_table_p)
        *intra_table_p = intra_table;
    else
        free(intra_table);
}

/* This is essentially `MPIR_Find_exetrnal()` from `mpich2/src/util/mpir_localproc.c`: */
static void pscoll_msa_find_external(MPI_Comm comm, int *external_size_p, int *external_rank_p,
                                     int **external_ranks_p, int **inter_table_p,
                                     int *rank2id_table)
{
    int *id2rank_table;
    int i, external_size, external_rank;
    int *external_ranks, *inter_table;
    int id, max_id = 0;
    int comm_rank, comm_size;

    MPI_Comm_rank(comm, &comm_rank);
    MPI_Comm_size(comm, &comm_size);

    /* Scan through the list of processes in comm and add one
     * process from each node to the list of "external" processes. */
    external_ranks = malloc(sizeof(int) * comm_size);
    inter_table = malloc(sizeof(int) * comm_size);

    for (i = 0; i < comm_size; ++i) {
        if (rank2id_table[i] > max_id)
            max_id = rank2id_table[i];
    }

    id2rank_table = malloc(sizeof(int) * (max_id + 1));

    /* nodes maps node_id to rank in external_ranks of leader for that node */
    for (i = 0; i < (max_id + 1); ++i)
        id2rank_table[i] = -1;

    external_size = 0;
    external_rank = -1;

    for (i = 0; i < comm_size; ++i) {
        id = rank2id_table[i];

        /* build list of external processes */
        if (id2rank_table[id] == -1) {
            if (i == comm_rank)
                external_rank = external_size;
            id2rank_table[id] = external_size;
            external_ranks[external_size] = i;
            ++external_size;
        }

        /* build the map from rank in comm to rank in external_ranks */
        inter_table[i] = id2rank_table[id];
    }

    *external_size_p = external_size;
    *external_rank_p = external_rank;

    if (external_ranks_p)
        *external_ranks_p = realloc(external_ranks, sizeof(int) * external_size);
    else
        free(external_ranks);

    if (inter_table_p)
        *inter_table_p = inter_table;
    else
        free(inter_table);
}

int collops_comm_init(MPI_Comm comm, void *extra_state, void *extra_comm_state)
{
    void **extra_comm_state_ = extra_comm_state;

    assert(extra_state == &pscoll_msa_extra_state);

    pscoll_msa_extra_comm_state_t *extra_comm_state_ptr =
        malloc(sizeof(pscoll_msa_extra_comm_state_t));
    extra_comm_state_ptr->comm = comm;
    extra_comm_state_ptr->local_comm = MPI_COMM_NULL;
    extra_comm_state_ptr->roots_comm = MPI_COMM_NULL;
    extra_comm_state_ptr->local_root_rank = MPI_PROC_NULL;
    extra_comm_state_ptr->intra_table = NULL;
    extra_comm_state_ptr->inter_table = NULL;
    extra_comm_state_ptr->msa_awareness_level = pscoll_msa_level_none;

    MPI_Comm module_local_comm = MPI_COMM_NULL;
    MPI_Comm module_roots_comm = MPI_COMM_NULL;
    MPI_Comm node_local_comm = MPI_COMM_NULL;
    MPI_Comm node_roots_comm = MPI_COMM_NULL;

    int root_size, root_rank;
    int comm_size, comm_rank;
    MPI_Comm_size(comm, &comm_size);
    MPI_Comm_rank(comm, &comm_rank);

    if (comm_size == 1) {
        return MPI_SUCCESS;
    }

    MPI_Comm_split_type(comm, MPIX_COMM_TYPE_MODULE, 0, MPI_INFO_NULL, &module_local_comm);
    assert(module_local_comm != MPI_COMM_NULL);

    int module_comm_size, module_comm_rank;
    MPI_Comm_size(module_local_comm, &module_comm_size);
    MPI_Comm_rank(module_local_comm, &module_comm_rank);

    if (module_comm_size == comm_size) {
        /* comm is flat on module level */
        MPI_Comm_free(&module_local_comm);
    } else {
        /* split comm into local groups on module level */
        MPI_Comm_split(comm, module_comm_rank ? MPI_UNDEFINED : 0, 0, &module_roots_comm);
        if (module_roots_comm != MPI_COMM_NULL) {
            MPI_Comm_size(module_roots_comm, &root_size);
            MPI_Comm_rank(module_roots_comm, &root_rank);
            if (root_size == comm_size) {
                /* root comm and comm are identical on module level */
                MPI_Comm_free(&module_local_comm);
                MPI_Comm_free(&module_roots_comm);
                goto fn_exit;
            }
        }

        extra_comm_state_ptr->local_comm = module_local_comm;
        extra_comm_state_ptr->roots_comm = module_roots_comm;
        extra_comm_state_ptr->msa_awareness_level = pscoll_msa_level_modules;
        goto fn_exit_comm;
    }

    MPI_Comm_split_type(comm, MPI_COMM_TYPE_SHARED, 0, MPI_INFO_NULL, &node_local_comm);
    assert(node_local_comm != MPI_COMM_NULL);

    int node_comm_size, node_comm_rank;
    MPI_Comm_size(node_local_comm, &node_comm_size);
    MPI_Comm_rank(node_local_comm, &node_comm_rank);

    if (node_comm_size == comm_size) {
        /* comm is flat on node level */
        MPI_Comm_free(&node_local_comm);
    } else {
        /* split comm into local groups on node level */
        MPI_Comm_split(comm, node_comm_rank ? MPI_UNDEFINED : 0, 0, &node_roots_comm);
        if (node_roots_comm != MPI_COMM_NULL) {
            MPI_Comm_size(node_roots_comm, &root_size);
            MPI_Comm_rank(node_roots_comm, &root_rank);
            if (root_size == comm_size) {
                /* root comm and comm are identical on node level */
                MPI_Comm_free(&node_local_comm);
                MPI_Comm_free(&node_roots_comm);
                goto fn_exit;
            }
        }

        extra_comm_state_ptr->local_comm = node_local_comm;
        extra_comm_state_ptr->roots_comm = node_roots_comm;
        extra_comm_state_ptr->msa_awareness_level = pscoll_msa_level_nodes;
        goto fn_exit_comm;
    }

  fn_exit:
    *extra_comm_state_ = extra_comm_state_ptr;
    return MPI_SUCCESS;

  fn_exit_comm:
    assert(extra_comm_state_ptr->local_comm != MPI_COMM_NULL);
    MPI_Comm_rank(comm, &extra_comm_state_ptr->local_root_rank);
    MPI_Bcast(&extra_comm_state_ptr->local_root_rank, 1, MPI_INT, 0,
              extra_comm_state_ptr->local_comm);

    int *rank2id_table = malloc(comm_size * sizeof(int));
    MPI_Allgather(&extra_comm_state_ptr->local_root_rank, 1, MPI_INT, rank2id_table, 1, MPI_INT,
                  comm);

    int local_size = 0;
    int local_rank = -1;
    pscoll_msa_find_local(comm, &local_size, &local_rank, NULL, &extra_comm_state_ptr->intra_table,
                          rank2id_table);

    int local_comm_size = 0;
    int local_comm_rank = -1;
    MPI_Comm_size(extra_comm_state_ptr->local_comm, &local_comm_size);
    MPI_Comm_rank(extra_comm_state_ptr->local_comm, &local_comm_rank);
    assert((local_size == local_comm_size) && (local_rank == local_comm_rank));

    int external_size = 0;
    int external_rank = -1;
    pscoll_msa_find_external(comm, &external_size, &external_rank, NULL,
                             &extra_comm_state_ptr->inter_table, rank2id_table);

    free(rank2id_table);

    goto fn_exit;
}

static inline
    int pscoll_msa_get_intra_rank(pscoll_msa_extra_comm_state_t * extra_comm_state_ptr, int rank)
{
    assert(extra_comm_state_ptr->intra_table);
    return extra_comm_state_ptr->intra_table[rank];
}

static inline
    int pscoll_msa_get_inter_rank(pscoll_msa_extra_comm_state_t * extra_comm_state_ptr, int rank)
{
    assert(extra_comm_state_ptr->inter_table);
    return extra_comm_state_ptr->inter_table[rank];
}

int collops_comm_free(void *extra_comm_state)
{
    assert(extra_comm_state != NULL);

    pscoll_msa_extra_comm_state_t *extra_comm_state_ptr =
        (pscoll_msa_extra_comm_state_t *) extra_comm_state;

    if (extra_comm_state_ptr->local_comm != MPI_COMM_NULL)
        MPI_Comm_free(&extra_comm_state_ptr->local_comm);
    if (extra_comm_state_ptr->roots_comm != MPI_COMM_NULL)
        MPI_Comm_free(&extra_comm_state_ptr->roots_comm);

    if (extra_comm_state_ptr->intra_table)
        free(extra_comm_state_ptr->intra_table);
    if (extra_comm_state_ptr->inter_table)
        free(extra_comm_state_ptr->inter_table);

    return MPI_SUCCESS;
}

#define CHECK_AND_FALLBACK(...)			\
    if (mpi_errno != MPI_SUCCESS) {		\
	return MPIX_ERR_FALLBACK;		\
    }

static int msa_barrier_intra_comm(pscoll_msa_extra_comm_state_t * extra_comm_state_ptr)
{
    int mpi_errno = MPI_SUCCESS;

    int comm_size, comm_rank;
    MPI_Comm_size(extra_comm_state_ptr->comm, &comm_size);
    MPI_Comm_rank(extra_comm_state_ptr->comm, &comm_rank);

    if (extra_comm_state_ptr->msa_awareness_level == pscoll_msa_level_none) {
        return MPIX_ERR_FALLBACK;
    }

    /* do the intra barrier with the local local */
    assert(extra_comm_state_ptr->local_comm != MPI_COMM_NULL);
    mpi_errno = MPI_Barrier(extra_comm_state_ptr->local_comm);
    assert(mpi_errno == MPI_SUCCESS);

    /* do the barrier across the root local */
    if (extra_comm_state_ptr->roots_comm != MPI_COMM_NULL) {
        mpi_errno = MPI_Barrier(extra_comm_state_ptr->roots_comm);
        assert(mpi_errno == MPI_SUCCESS);
    }

    /* release the local local with a 1-byte broadcast */
    int i = 0;
    mpi_errno = MPI_Bcast(&i, 1, MPI_BYTE, 0, extra_comm_state_ptr->local_comm);
    assert(mpi_errno == MPI_SUCCESS);

    return mpi_errno;
}

static int msa_bcast_intra_comm(void *buffer, MPI_Count count, MPI_Datatype dtype, int root,
                                pscoll_msa_extra_comm_state_t * extra_comm_state_ptr)
{
    int mpi_errno = MPI_SUCCESS;

    int comm_size, comm_rank;
    MPI_Comm_size(extra_comm_state_ptr->comm, &comm_size);
    MPI_Comm_rank(extra_comm_state_ptr->comm, &comm_rank);

    if (extra_comm_state_ptr->msa_awareness_level == pscoll_msa_level_none) {
        return MPIX_ERR_FALLBACK;
    }

    assert(extra_comm_state_ptr->local_comm != MPI_COMM_NULL);

    int local_comm_size, local_comm_rank;
    MPI_Comm_size(extra_comm_state_ptr->local_comm, &local_comm_size);
    MPI_Comm_rank(extra_comm_state_ptr->local_comm, &local_comm_rank);

    int msg_size;
    MPI_Type_size(dtype, &msg_size);

    if (msg_size == 0) {
        return MPI_SUCCESS;
    }

    if (msg_size < PSCOLL_MSA_BCAST_SHORT_MSG_SIZE) {
        /* SHORT MESSAGES:
         *  1. Send to intra-node rank 0 on root's node
         *  2. Perform the inter-node bcast
         *  3. Perform the intra-node bcast on all nodes
         */

        if (pscoll_msa_get_intra_rank(extra_comm_state_ptr, root) > 0) {
            /* is not the local root (0) but is local (!-1) */
            if (root == comm_rank) {
                mpi_errno = MPI_Send(buffer, count, dtype, 0, 42, extra_comm_state_ptr->local_comm);
                assert(mpi_errno == MPI_SUCCESS);
            } else if (local_comm_rank == 0) {
                mpi_errno =
                    MPI_Recv(buffer, count, dtype,
                             pscoll_msa_get_intra_rank(extra_comm_state_ptr, root), 42,
                             extra_comm_state_ptr->local_comm, MPI_STATUS_IGNORE);
                assert(mpi_errno == MPI_SUCCESS);
            }
        }

        /* perform the inter-group broadcast */
        if (extra_comm_state_ptr->roots_comm != MPI_COMM_NULL) {
            mpi_errno =
                MPI_Bcast(buffer, count, dtype,
                          pscoll_msa_get_inter_rank(extra_comm_state_ptr, root),
                          extra_comm_state_ptr->roots_comm);
            assert(mpi_errno == MPI_SUCCESS);
        }

        /* perform the intra-group broadcast */
        mpi_errno = MPI_Bcast(buffer, count, dtype, 0, extra_comm_state_ptr->local_comm);
        assert(mpi_errno == MPI_SUCCESS);

    } else {
        /* LARGE MESSAGES:
         *  1. Perform the intra-node bcast on root's node
         *  2. Perform the inter-node bcast
         *  3. Perform the intra-node bcast except for root's node
         */

        /* perform the locla broadcast in the root's local group */
        if (pscoll_msa_get_intra_rank(extra_comm_state_ptr, root) > 0) {
            /* is not the local root (0) but is local (!-1) */
            mpi_errno =
                MPI_Bcast(buffer, count, dtype,
                          pscoll_msa_get_intra_rank(extra_comm_state_ptr, root),
                          extra_comm_state_ptr->local_comm);
            assert(mpi_errno == MPI_SUCCESS);
        }

        /* perform the inter-group broadcast */
        if (extra_comm_state_ptr->roots_comm != MPI_COMM_NULL) {
            mpi_errno =
                MPI_Bcast(buffer, count, dtype,
                          pscoll_msa_get_inter_rank(extra_comm_state_ptr, root),
                          extra_comm_state_ptr->roots_comm);
            assert(mpi_errno == MPI_SUCCESS);
        }

        /* perform the local broadcast on all except for the root's group */
        if (pscoll_msa_get_intra_rank(extra_comm_state_ptr, root) <= 0) {
            /* 0 if root was local root too, -1 if different node than root */
            mpi_errno = MPI_Bcast(buffer, count, dtype, 0, extra_comm_state_ptr->local_comm);
            assert(mpi_errno == MPI_SUCCESS);
        }
    }

    return mpi_errno;
}

#define MAX(a, b) ({        \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a > _b ? _a : _b;      \
})

static int msa_reduce_intra_comm(const void *sendbuf, void *recvbuf, MPI_Count count,
                                 MPI_Datatype dtype, MPI_Op op, int root,
                                 pscoll_msa_extra_comm_state_t * extra_comm_state_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    void *tmp_buf = NULL;
    MPI_Aint lb, true_lb, true_extent, extent;

    int comm_size, comm_rank;
    MPI_Comm_size(extra_comm_state_ptr->comm, &comm_size);
    MPI_Comm_rank(extra_comm_state_ptr->comm, &comm_rank);

    if (extra_comm_state_ptr->msa_awareness_level == pscoll_msa_level_none) {
        return MPIX_ERR_FALLBACK;
    }

    assert(extra_comm_state_ptr->local_comm != MPI_COMM_NULL);

    /* create a temporary buffer on local roots */
    if (extra_comm_state_ptr->roots_comm != MPI_COMM_NULL) {

        MPI_Type_get_extent(dtype, &lb, &extent);
        MPI_Type_get_true_extent(dtype, &true_lb, &extent);

        tmp_buf = malloc(count * MAX(extent, true_extent));
        tmp_buf = (void *) ((char *) tmp_buf - true_lb);
    }


    /* do the intra-group reduce on all groups other than the root's local one */
    if (pscoll_msa_get_intra_rank(extra_comm_state_ptr, root) == -1) {
        mpi_errno =
            MPI_Reduce(sendbuf, tmp_buf, count, dtype, op, 0, extra_comm_state_ptr->local_comm);
        assert(mpi_errno == MPI_SUCCESS);
    }

    /* do the inter-group reduce to the root's group */
    if (extra_comm_state_ptr->roots_comm != MPI_COMM_NULL) {
        int root_rank;
        MPI_Comm_rank(extra_comm_state_ptr->roots_comm, &root_rank);
        if (root_rank != pscoll_msa_get_inter_rank(extra_comm_state_ptr, root)) {
            /* I am not in root's local group. */
            mpi_errno =
                MPI_Reduce(tmp_buf, MPI_IN_PLACE, count, dtype, op,
                           pscoll_msa_get_inter_rank(extra_comm_state_ptr, root),
                           extra_comm_state_ptr->roots_comm);
            assert(mpi_errno == MPI_SUCCESS);
        } else {
            /* I am in root's local group. I have not participated in the earlier reduce. */
            if (comm_rank != root) {
                /* I am not the root though. I don't have a valid recvbuf.
                 * Use tmp_buf as recvbuf. */
                mpi_errno =
                    MPI_Reduce(sendbuf, tmp_buf, count, dtype, op,
                               pscoll_msa_get_inter_rank(extra_comm_state_ptr, root),
                               extra_comm_state_ptr->roots_comm);
                assert(mpi_errno == MPI_SUCCESS);

                /* point sendbuf at tmp_buf to make final intranode reduce easy */
                sendbuf = tmp_buf;
            } else {
                /* I am the root. in_place is automatically handled. */

                mpi_errno =
                    MPI_Reduce(sendbuf, recvbuf, count, dtype, op,
                               pscoll_msa_get_inter_rank(extra_comm_state_ptr, root),
                               extra_comm_state_ptr->roots_comm);
                assert(mpi_errno == MPI_SUCCESS);

                /* set sendbuf to MPI_IN_PLACE to make final intra-group reduce easy. */
                sendbuf = MPI_IN_PLACE;
            }
        }

    }

    /* do the intra-group reduce on the root's node */
    if (pscoll_msa_get_intra_rank(extra_comm_state_ptr, root) != -1) {
        mpi_errno =
            MPI_Reduce(sendbuf, recvbuf, count, dtype, op,
                       pscoll_msa_get_intra_rank(extra_comm_state_ptr, root),
                       extra_comm_state_ptr->local_comm);
        assert(mpi_errno == MPI_SUCCESS);
    }

    return mpi_errno;
}

static int msa_allreduce_intra_comm(const void *sendbuf, void *recvbuf, MPI_Count count,
                                    MPI_Datatype dtype, MPI_Op op,
                                    pscoll_msa_extra_comm_state_t * extra_comm_state_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    void *tmp_buf = NULL;
    MPI_Aint lb, true_lb, true_extent, extent;

    int comm_size, comm_rank;
    MPI_Comm_size(extra_comm_state_ptr->comm, &comm_size);
    MPI_Comm_rank(extra_comm_state_ptr->comm, &comm_rank);

    if (extra_comm_state_ptr->msa_awareness_level == pscoll_msa_level_none) {
        return MPIX_ERR_FALLBACK;
    }

    assert(extra_comm_state_ptr->local_comm != MPI_COMM_NULL);

    int local_rank;
    MPI_Comm_rank(extra_comm_state_ptr->local_comm, &local_rank);

    /* on each node, do a reduce to the local root */
    if ((sendbuf == MPI_IN_PLACE) && (local_rank != 0)) {
        /* IN_PLACE and not root of reduce. Data supplied to this
         * allreduce is in recvbuf. Pass that as the sendbuf to reduce. */
        mpi_errno =
            MPI_Reduce(recvbuf, NULL, count, dtype, op, 0, extra_comm_state_ptr->local_comm);
        assert(mpi_errno == MPI_SUCCESS);
    } else {
        mpi_errno =
            MPI_Reduce(sendbuf, recvbuf, count, dtype, op, 0, extra_comm_state_ptr->local_comm);
        assert(mpi_errno == MPI_SUCCESS);
    }

    /* now do an IN_PLACE allreduce among the local roots of all nodes */
    if (extra_comm_state_ptr->roots_comm != MPI_COMM_NULL) {
        mpi_errno =
            MPI_Allreduce(MPI_IN_PLACE, recvbuf, count, dtype, op,
                          extra_comm_state_ptr->roots_comm);
        assert(mpi_errno == MPI_SUCCESS);
    }

    /* now broadcast the result among local processes */
    if (extra_comm_state_ptr->local_comm != NULL) {
        mpi_errno = MPI_Bcast(recvbuf, count, dtype, 0, extra_comm_state_ptr->local_comm);
        assert(mpi_errno == MPI_SUCCESS);
    }

    return mpi_errno;
}

int collops_algorithms(int collop, const void *sbuf, MPI_Count scount, const MPI_Count scounts[],
                       const MPI_Aint sdispls[], MPI_Datatype stype, void *rbuf, MPI_Count rcount,
                       const MPI_Count rcounts[], const MPI_Aint rdispls[], MPI_Datatype rtype,
                       MPI_Op op, int root, MPI_Comm comm, void *extra_comm_state)
{
    int mpi_errno = MPI_SUCCESS;
    pscoll_msa_extra_comm_state_t *extra_comm_state_ptr =
        (pscoll_msa_extra_comm_state_t *) extra_comm_state;

    assert(extra_comm_state_ptr != NULL);
    assert(extra_comm_state_ptr->comm == comm);
    assert(sizeof(MPI_Aint) == sizeof(MPI_Count));

    int rank;
    MPI_Comm_rank(comm, &rank);

    switch (collop) {
        case MPIX_COLLOP_BARRIER:
            mpi_errno = msa_barrier_intra_comm(extra_comm_state_ptr);
            if (mpi_errno != MPI_SUCCESS) {
                return MPIX_ERR_FALLBACK;
            }
            pscoll_msa_counters.barrier++;
            return MPI_SUCCESS;
        case MPIX_COLLOP_BCAST:
            if (rank == root) {
                mpi_errno =
                    msa_bcast_intra_comm((void *) sbuf, scount, stype, root, extra_comm_state_ptr);
            } else {
                mpi_errno = msa_bcast_intra_comm(rbuf, rcount, rtype, root, extra_comm_state_ptr);
            }
            if (mpi_errno != MPI_SUCCESS) {
                return MPIX_ERR_FALLBACK;
            }
            pscoll_msa_counters.bcast++;
            return MPI_SUCCESS;
        case MPIX_COLLOP_REDUCE:
            if (rank == root) {
                mpi_errno =
                    msa_reduce_intra_comm(sbuf, rbuf, scount, stype, op, root,
                                          extra_comm_state_ptr);
            } else {
                mpi_errno =
                    msa_reduce_intra_comm(sbuf, NULL, scount, stype, op, root,
                                          extra_comm_state_ptr);
            }
            if (mpi_errno != MPI_SUCCESS) {
                return MPIX_ERR_FALLBACK;
            }
            pscoll_msa_counters.reduce++;
            return MPI_SUCCESS;
        case MPIX_COLLOP_ALLREDUCE:
            mpi_errno =
                msa_allreduce_intra_comm(sbuf, rbuf, scount, stype, op, extra_comm_state_ptr);
            if (mpi_errno != MPI_SUCCESS) {
                return MPIX_ERR_FALLBACK;
            }
            pscoll_msa_counters.allreduce++;
            return MPI_SUCCESS;
        default:
            return MPIX_ERR_FALLBACK;
    }

    return MPI_SUCCESS;
}

int collops_deregister(void *_extra_state)
{
    pscoll_msa_extra_state_t *extra_state = _extra_state;
    assert(extra_state == &pscoll_msa_extra_state);

    char *envval = getenv("PSCOLL_COLLOPS_STATS");
    if (envval && strstr(envval, "msa")) {
        printf
            ("=== pscoll_msa stats === (%d) === Barrier: %lld | Bcast: %lld | Reduce: %lld | Allreduce: %lld\n",
             extra_state->world_rank, pscoll_msa_counters.barrier, pscoll_msa_counters.bcast,
             pscoll_msa_counters.reduce, pscoll_msa_counters.allreduce);
    }

    return MPI_SUCCESS;
}

/* entry function for collops plugin */
int collops_register(const char *name, MPI_Info info)
{
    int rank, size;
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    pscoll_msa_extra_state.world_size = size;
    pscoll_msa_extra_state.world_rank = rank;

    if (strcmp(name, "pscoll_msa") == 0) {
        MPIX_Register_collops(name,
                              MPIX_COLLOP_BARRIER + MPIX_COLLOP_BCAST + MPIX_COLLOP_REDUCE +
                              MPIX_COLLOP_ALLREDUCE,
                              0, collops_algorithms,
                              collops_comm_init, collops_comm_free,
                              collops_deregister, MPI_INFO_NULL, &pscoll_msa_extra_state);
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
    collops_register("pscoll_msa", MPI_INFO_NULL);
    MPI_Info_create(&info);
    MPI_Info_set(info, "collops", "pscoll_msa");
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
