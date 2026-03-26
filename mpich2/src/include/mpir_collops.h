/*
 * ParaStation
 *
 * Copyright (C) 2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

/* info keys used for collops handling */
#define MPIX_COLLOPS_INFO_KEY_STRING "collops"
#define MPIX_COLLOPS_INFO_KEY_PLUGIN_STRING "collops_plugin"
#define MPIX_COLLOPS_INFO_KEY_PLUGIN_LIST_STRING "compressor_plugins"
#define MPIX_COLLOPS_INFO_KEY_PLUGIN_SEPARATOR ","
extern const char collops_info_key[];
extern const char collops_info_key_plugin[];
extern const char collops_info_key_plugin_list[];
extern const char collops_info_key_plugin_separator[];

/* symbol name for plugin function called to register a collops */
#define MPIX_COLLOPS_REGISTER_PLUGIN_FN_STRING "collops_register"
extern const char collops_register_plugin_fn[];

typedef struct MPIR_Collops {
    char *name;
    int collops_mask;
    MPIX_Collops_algorithm_function *algorithm_fn;
    MPIX_Collops_progress_function *progress_fn;
    MPIX_Collops_comm_init_function *comm_init_fn;
    MPIX_Collops_comm_free_function *comm_free_fn;
    MPIX_Collops_deregister_function *deregister_fn;
    void *extra_state;
    struct MPIR_Collops *next;  /* pointer to next collops */
} MPIR_Collops;

extern MPL_atomic_ptr_t MPIR_Collops_head;

/* This function searches for a collops that has already been registered by its name.
 * If a matching collops is found, the it returns a pointer the corresponding collops
 * struct. If the name is not found as already registered, NULL is returned instead. */
int MPIR_Collops_lookup(const char *collops_name, MPIR_Collops ** collops_pptr);

int MPIR_Collops_lookup_list(char **collops_name_list, int *length);
int MPIR_Collops_lookup_nth(int n, char **collops_name);
int MPIR_Collops_lookup_num(int *num);

/* Attach general information about the registered collopss to a given info object */
int MPIR_Collops_set_info(MPIR_Info *);

/* This function is to be used at the very end to release all registered collopss and
 * their allocated resources via their deregister function. */
int MPIR_Collops_deregister_all(void);

/* Check the info object attached to the communicator for collops-related info keys.
 * If any are found, they are evaluated, and any additional information is appended
 * to the communicator's `collops` sub-struct. */
int MPIR_Collops_check_info(MPIR_Comm * comm);

/* Check if there is information given via environment variables that can be translated
 * into collops-related info hints for the given communicator. If so, they are attached
 * to the info object of the communicator (which is created if it does not exist yet). */
int MPIR_Collops_check_env(MPIR_Comm * comm);

static inline void *swap_from_aint_to_count(const MPI_Aint counts[], MPIR_Comm * comm_ptr)
{
    int n =
        (comm_ptr->comm_kind ==
         MPIR_COMM_KIND__INTERCOMM) ? comm_ptr->remote_size : comm_ptr->local_size;
    MPI_Count *tmp_counts = MPL_malloc(n * sizeof(MPI_Count), MPL_MEM_OTHER);
    for (int i = 0; i < n; i++) {
        tmp_counts[i] = counts[i];
    }
    return tmp_counts;
}

#define MPIR_COLLOPS_CHECK_COMM(_comm)                                  \
    MPIR_Assert(_comm);                                                 \
    int comm_rank = MPIR_Comm_rank(_comm);                              \
    (void)comm_rank;

#define IS_ROOT(_is_root, _not_root)                                    \
    (comm_rank == root ? _is_root : _not_root)

#define NOT_ROOT(_not_root, _is_root)                                   \
    (comm_rank != root ? _not_root : _is_root)

#define MPIR_COLLOPS_CHECK_INFO(_comm)                                  \
    do {                                                                \
        if ((!_comm->collops.ptr) && (_comm->info_ptr)) {               \
            MPIR_Collops_check_info(_comm);                             \
            MPIR_Info_free_impl(_comm->info_ptr);                       \
            _comm->info_ptr = NULL;                                     \
        }                                                               \
    } while (0);

#define MPIR_COLLOPS_CALL_AND_FALLBACK(_collop, _sbuf, _scount, _scountv, _sdispls, _sdtype,     \
                                                _rbuf, _rcount, _rcountv, _rdispls, _rdtype,     \
                                                _op, _root, _comm)                               \
    MPIR_COLLOPS_CHECK_COMM(_comm);                                                              \
    MPIR_COLLOPS_CHECK_INFO(_comm);                                                              \
    do {                                                                                         \
        int mpi_errno = MPIX_ERR_FALLBACK;                                                       \
        MPI_Count *tmp_scountv = (MPI_Count*)_scountv;                                           \
        MPI_Count *free_scountv = NULL;                                                          \
        MPI_Count *tmp_rcountv = (MPI_Count*)_rcountv;                                           \
        MPI_Count *free_rcountv = NULL;                                                          \
        if (_comm->collops.ptr &&                                                                \
            (_comm->collops.ptr->collops_mask & MPIX_COLLOP_ ## _collop) &&                      \
             ((_comm->comm_kind == MPIR_COMM_KIND__INTRACOMM) ||                                 \
              (_comm->collops.ptr->collops_mask & MPIX_COLLOP_INTERCOMM))) {                     \
            if (_comm->collops.ptr->comm_init_fn && !_comm->collops.comm_is_initialized &&       \
                !_comm->collops.comm_is_active) {                                                \
                _comm->collops.comm_is_active |= MPIX_COLLOP_ALL;                                \
                mpi_errno = _comm->collops.ptr->comm_init_fn(_comm->handle,                      \
                                                 _comm->collops.ptr->extra_state,                \
                                                 &_comm->collops.comm_extra_state);              \
                _comm->collops.comm_is_active &= ~(MPIX_COLLOP_ALL);                             \
                if (mpi_errno == MPI_SUCCESS) {                                                  \
                    _comm->collops.comm_is_initialized = 1;                                      \
                } else {                                                                         \
                    _comm->collops.ptr = NULL;                                                   \
                }                                                                                \
            }                                                                                    \
            if (_comm->collops.ptr && _comm->collops.ptr->algorithm_fn &&                        \
                !(_comm->collops.comm_is_active & MPIX_COLLOP_ ## _collop)) {                    \
                _comm->collops.comm_is_active |= MPIX_COLLOP_ ## _collop;                        \
                if (sizeof(MPI_Count) != sizeof(MPI_Aint)) {                                     \
                    if (_scountv) {                                                              \
                        tmp_scountv = free_scountv = swap_from_aint_to_count(_scountv, _comm);   \
                    }                                                                            \
                    if (_rcountv) {                                                              \
                        tmp_rcountv = free_rcountv = swap_from_aint_to_count(_rcountv, _comm);   \
                    }                                                                            \
                }                                                                                \
                mpi_errno = _comm->collops.ptr->algorithm_fn(MPIX_COLLOP_ ## _collop,            \
                                                 _sbuf, _scount, tmp_scountv, _sdispls, _sdtype, \
                                                 _rbuf, _rcount, tmp_rcountv, _rdispls, _rdtype, \
                                                 _op, _root, _comm->handle,                      \
                                                 _comm->collops.comm_extra_state);               \
                _comm->collops.comm_is_active &= ~(MPIX_COLLOP_ ## _collop);                     \
            }                                                                                    \
        }                                                                                        \
        MPL_free(free_scountv);                                                                  \
        MPL_free(free_rcountv);                                                                  \
        return mpi_errno;                                                                        \
    } while (0);


static inline int MPIR_Collops_barrier(MPIR_Comm * comm_ptr, MPIR_Errflag_t errflag)
{
    MPIR_COLLOPS_CALL_AND_FALLBACK(BARRIER,
                                   NULL, 0, NULL, NULL, MPI_DATATYPE_NULL,
                                   NULL, 0, NULL, NULL, MPI_DATATYPE_NULL,
                                   MPI_OP_NULL, MPI_PROC_NULL, comm_ptr);
}

static inline int MPIR_Collops_bcast(void *buffer, MPI_Aint count, MPI_Datatype datatype, int root,
                                     MPIR_Comm * comm_ptr, MPIR_Errflag_t errflag)
{
    MPIR_COLLOPS_CALL_AND_FALLBACK(BCAST,
                                   IS_ROOT(buffer, NULL), IS_ROOT(count, 0), NULL, NULL,
                                   IS_ROOT(datatype, MPI_DATATYPE_NULL),
                                   NOT_ROOT(buffer, NULL), NOT_ROOT(count, 0), NULL, NULL,
                                   NOT_ROOT(datatype, MPI_DATATYPE_NULL),
                                   MPI_OP_NULL, root, comm_ptr);
}

static inline int MPIR_Collops_scatter(const void *sendbuf, MPI_Aint sendcount,
                                       MPI_Datatype sendtype, void *recvbuf, MPI_Aint recvcount,
                                       MPI_Datatype recvtype, int root, MPIR_Comm * comm_ptr,
                                       MPIR_Errflag_t errflag)
{
    MPIR_COLLOPS_CALL_AND_FALLBACK(SCATTER,
                                   IS_ROOT(sendbuf, NULL), IS_ROOT(sendcount, 0), NULL, NULL,
                                   IS_ROOT(sendtype, MPI_DATATYPE_NULL),
                                   recvbuf, recvcount, NULL, NULL, recvtype,
                                   MPI_OP_NULL, root, comm_ptr);
}

static inline int MPIR_Collops_scatterv(const void *sendbuf, const MPI_Aint sendcounts[],
                                        const MPI_Aint displs[], MPI_Datatype sendtype,
                                        void *recvbuf, MPI_Aint recvcount, MPI_Datatype recvtype,
                                        int root, MPIR_Comm * comm_ptr, MPIR_Errflag_t errflag)
{
    MPIR_COLLOPS_CALL_AND_FALLBACK(SCATTERV,
                                   IS_ROOT(sendbuf, NULL), 0,
                                   IS_ROOT(sendcounts, NULL), IS_ROOT(displs, NULL),
                                   IS_ROOT(sendtype, MPI_DATATYPE_NULL),
                                   recvbuf, recvcount, NULL, NULL, recvtype,
                                   MPI_OP_NULL, root, comm_ptr);
}

static inline int MPIR_Collops_gather(const void *sendbuf, MPI_Aint sendcount,
                                      MPI_Datatype sendtype, void *recvbuf, MPI_Aint recvcount,
                                      MPI_Datatype recvtype, int root, MPIR_Comm * comm_ptr,
                                      MPIR_Errflag_t errflag)
{
    MPIR_COLLOPS_CALL_AND_FALLBACK(GATHER,
                                   sendbuf, sendcount, NULL, NULL, sendtype,
                                   IS_ROOT(recvbuf, NULL), IS_ROOT(recvcount, 0), NULL, NULL,
                                   IS_ROOT(recvtype, MPI_DATATYPE_NULL),
                                   MPI_OP_NULL, root, comm_ptr);
}

static inline int MPIR_Collops_gatherv(const void *sendbuf, MPI_Aint sendcount,
                                       MPI_Datatype sendtype, void *recvbuf,
                                       const MPI_Aint recvcounts[], const MPI_Aint displs[],
                                       MPI_Datatype recvtype, int root, MPIR_Comm * comm_ptr,
                                       MPIR_Errflag_t errflag)
{
    MPIR_COLLOPS_CALL_AND_FALLBACK(GATHERV,
                                   sendbuf, sendcount, NULL, NULL, sendtype,
                                   IS_ROOT(recvbuf, NULL), 0,
                                   IS_ROOT(recvcounts, NULL), IS_ROOT(displs, NULL),
                                   IS_ROOT(recvtype, MPI_DATATYPE_NULL),
                                   MPI_OP_NULL, root, comm_ptr);
}

static inline int MPIR_Collops_allgather(const void *sendbuf, MPI_Aint sendcount,
                                         MPI_Datatype sendtype, void *recvbuf, MPI_Aint recvcount,
                                         MPI_Datatype recvtype, MPIR_Comm * comm_ptr,
                                         MPIR_Errflag_t errflag)
{
    MPIR_COLLOPS_CALL_AND_FALLBACK(ALLGATHER,
                                   sendbuf, sendcount, NULL, NULL, sendtype,
                                   recvbuf, recvcount, NULL, NULL, recvtype,
                                   MPI_OP_NULL, MPI_PROC_NULL, comm_ptr);
}

static inline int MPIR_Collops_allgatherv(const void *sendbuf, MPI_Aint sendcount,
                                          MPI_Datatype sendtype, void *recvbuf,
                                          const MPI_Aint recvcounts[], const MPI_Aint displs[],
                                          MPI_Datatype recvtype, MPIR_Comm * comm_ptr,
                                          MPIR_Errflag_t errflag)
{
    MPIR_COLLOPS_CALL_AND_FALLBACK(ALLGATHERV,
                                   sendbuf, sendcount, NULL, NULL, sendtype,
                                   recvbuf, 0, recvcounts, displs, recvtype,
                                   MPI_OP_NULL, MPI_PROC_NULL, comm_ptr);
}

static inline int MPIR_Collops_alltoall(const void *sendbuf, MPI_Aint sendcount,
                                        MPI_Datatype sendtype, void *recvbuf, MPI_Aint recvcount,
                                        MPI_Datatype recvtype, MPIR_Comm * comm_ptr,
                                        MPIR_Errflag_t errflag)
{
    MPIR_COLLOPS_CALL_AND_FALLBACK(ALLTOALL,
                                   sendbuf, sendcount, NULL, NULL, sendtype,
                                   recvbuf, recvcount, NULL, NULL, recvtype,
                                   MPI_OP_NULL, MPI_PROC_NULL, comm_ptr);
}

static inline int MPIR_Collops_alltoallv(const void *sendbuf, const MPI_Aint sendcounts[],
                                         const MPI_Aint sdispls[], MPI_Datatype sendtype,
                                         void *recvbuf, const MPI_Aint recvcounts[],
                                         const MPI_Aint rdispls[], MPI_Datatype recvtype,
                                         MPIR_Comm * comm_ptr, MPIR_Errflag_t errflag)
{
    MPIR_COLLOPS_CALL_AND_FALLBACK(ALLTOALLV,
                                   sendbuf, 0, sendcounts, sdispls, sendtype,
                                   recvbuf, 0, recvcounts, rdispls, recvtype,
                                   MPI_OP_NULL, MPI_PROC_NULL, comm_ptr);
}

static inline int MPIR_Collops_reduce(const void *sendbuf, void *recvbuf, MPI_Aint count,
                                      MPI_Datatype datatype, MPI_Op op, int root,
                                      MPIR_Comm * comm_ptr, MPIR_Errflag_t errflag)
{
    MPIR_COLLOPS_CALL_AND_FALLBACK(REDUCE,
                                   sendbuf, count, NULL, NULL,
                                   datatype,
                                   IS_ROOT(recvbuf, NULL), IS_ROOT(count, 0), NULL, NULL,
                                   IS_ROOT(datatype, MPI_DATATYPE_NULL), op, root, comm_ptr);
}

static inline int MPIR_Collops_allreduce(const void *sendbuf, void *recvbuf, MPI_Aint count,
                                         MPI_Datatype datatype, MPI_Op op, MPIR_Comm * comm_ptr,
                                         MPIR_Errflag_t errflag)
{
    MPIR_COLLOPS_CALL_AND_FALLBACK(ALLREDUCE,
                                   sendbuf, count, NULL, NULL, datatype,
                                   recvbuf, count, NULL, NULL, datatype,
                                   op, MPI_PROC_NULL, comm_ptr);
}
