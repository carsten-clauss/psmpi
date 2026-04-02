/*
 * ParaStation
 *
 * Copyright (C) 2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

/*----------------------
  BEGIN DATATYPE SECTION (from mpid/ch4/src/ch4_impl.h)
  ----------------------*/
#define MPIDI_Datatype_get_info(count_, datatype_,              \
                                dt_contig_out_, data_sz_out_,   \
                                dt_ptr_, dt_true_lb_)           \
    do {                                                        \
        if (HANDLE_IS_BUILTIN(datatype_)) {                     \
            (dt_ptr_)        = NULL;                            \
            (dt_contig_out_) = TRUE;                            \
            (dt_true_lb_)    = 0;                               \
            (data_sz_out_)   = (size_t)(count_) *               \
                MPIR_Datatype_get_basic_size(datatype_);        \
        } else {                                                \
            MPIR_Datatype_get_ptr((datatype_), (dt_ptr_));      \
            if (dt_ptr_)                                        \
            {                                                   \
                (dt_contig_out_) = (dt_ptr_)->is_contig;        \
                (dt_true_lb_)    = (dt_ptr_)->true_lb;          \
                (data_sz_out_)   = (size_t)(count_) *           \
                    (dt_ptr_)->size;                            \
            }                                                   \
            else                                                \
            {                                                   \
                (dt_contig_out_) = 1;                           \
                (dt_true_lb_)    = 0;                           \
                (data_sz_out_)   = 0;                           \
            }                                                   \
        }                                                       \
    } while (0)
/*--------------------
  END DATATYPE SECTION
  --------------------*/

typedef struct {
    MPI_Aint basic_size;
    void *sbuf_tmp;
    void *sbuf_free;
    MPI_Count scount_tmp;
    MPI_Count *scounts_tmp;
    MPI_Aint *sdispls_tmp;
    void *rbuf_tmp;
    void *rbuf_free;
    MPI_Count rcount_tmp;
    MPI_Count *rcounts_tmp;
    MPI_Aint *rdispls_tmp;
} collapse_dtypes_req_t;

static inline MPI_Datatype collapse_dtypes_send(const void *sbuf,
                                                MPI_Count scount,
                                                MPI_Datatype mpi_dtype,
                                                int num_procs, collapse_dtypes_req_t * req)
{
    int contig;
    MPIR_Datatype *dtype_ptr;
    MPI_Datatype basic_dtype = MPI_DATATYPE_NULL;
    MPI_Aint true_lb;
    size_t data_size;

    MPIDI_Datatype_get_info(scount, mpi_dtype, contig, data_size, dtype_ptr, true_lb);
    MPIR_Datatype_get_basic_type(mpi_dtype, basic_dtype);
    req->basic_size = MPIR_Datatype_get_basic_size(basic_dtype);

    if ((basic_dtype == mpi_dtype) || (basic_dtype == MPI_DATATYPE_NULL) || !basic_dtype) {
        goto fn_fail;
    }

    if (contig || !data_size) {
        req->sbuf_tmp = (char *) sbuf + true_lb;
        req->scount_tmp = data_size / req->basic_size;
        MPIR_Assert((data_size % req->basic_size) == 0);
    } else {
        MPI_Aint actual_packed_bytes;
        MPI_Aint size_of_pack_buffer;
        MPIR_Pack_size(scount * num_procs, mpi_dtype, &size_of_pack_buffer);
        req->sbuf_tmp = req->sbuf_free = MPL_malloc(size_of_pack_buffer, MPL_MEM_BUFFER);
        MPIR_Assert(req->sbuf_tmp);
        int rc = MPIR_Typerep_pack(sbuf, scount * num_procs, mpi_dtype, 0, req->sbuf_tmp,
                                   size_of_pack_buffer, &actual_packed_bytes,
                                   MPIR_TYPEREP_FLAG_NONE);
        MPIR_Assert(rc == MPI_SUCCESS);
        MPIR_Assert(actual_packed_bytes == size_of_pack_buffer);
        req->scount_tmp = actual_packed_bytes / (num_procs * req->basic_size);
        MPIR_Assert((actual_packed_bytes % (num_procs * req->basic_size)) == 0);
    }

  fn_exit:
    return basic_dtype;
  fn_fail:
    basic_dtype = MPI_DATATYPE_NULL;
    goto fn_exit;
}

static inline MPI_Datatype collapse_dtypes_sendv(const void *sbuf,
                                                 const MPI_Count * scounts,
                                                 const MPI_Aint * sdispls,
                                                 MPI_Datatype mpi_dtype,
                                                 int num_procs, collapse_dtypes_req_t * req)
{
    MPI_Aint total_len = 0;
    const void *buf_displ = NULL;
    MPI_Aint extent = 0;
    MPI_Datatype basic_dtype = MPI_DATATYPE_NULL;
    MPI_Aint actual_packed_bytes;
    MPI_Aint size_of_pack_buffer;

    MPIR_Datatype_get_basic_type(mpi_dtype, basic_dtype);
    req->basic_size = MPIR_Datatype_get_basic_size(basic_dtype);

    if ((basic_dtype == mpi_dtype) || (basic_dtype == MPI_DATATYPE_NULL) || !basic_dtype) {
        goto fn_fail;
    }

    req->scounts_tmp = MPL_malloc(num_procs * sizeof(MPI_Count), MPL_MEM_COLL);
    MPIR_Assert(req->scounts_tmp);
    req->sdispls_tmp = MPL_malloc(num_procs * sizeof(MPI_Aint), MPL_MEM_COLL);
    MPIR_Assert(req->sdispls_tmp);
    MPIR_Datatype_get_extent_macro(mpi_dtype, extent);

    for (int i = 0; i < num_procs; i++) {
        buf_displ = (char *) sbuf + sdispls[i] * extent;
        MPIR_Pack_size(scounts[i], mpi_dtype, &size_of_pack_buffer);
        req->scounts_tmp[i] = size_of_pack_buffer / req->basic_size;
        MPIR_Assert((size_of_pack_buffer % req->basic_size) == 0);
        total_len += size_of_pack_buffer;
    }

    req->sbuf_tmp = req->sbuf_free = MPL_malloc(total_len, MPL_MEM_BUFFER);
    MPIR_Assert(req->sbuf_tmp);

    char *ptr = req->sbuf_tmp;
    for (int i = 0; i < num_procs; i++) {
        buf_displ = (char *) sbuf + sdispls[i] * extent;
        MPIR_Pack_size(scounts[i], mpi_dtype, &size_of_pack_buffer);
        int rc = MPIR_Typerep_pack(buf_displ, scounts[i], mpi_dtype, 0, ptr,
                                   size_of_pack_buffer, &actual_packed_bytes,
                                   MPIR_TYPEREP_FLAG_NONE);
        MPIR_Assert(rc == MPI_SUCCESS);
        MPIR_Assert(actual_packed_bytes == size_of_pack_buffer);
        ptr += actual_packed_bytes;
        req->sdispls_tmp[i] = i ? (req->sdispls_tmp[i - 1] + req->scounts_tmp[i - 1]) : 0;
    }

  fn_exit:
    return basic_dtype;
  fn_fail:
    basic_dtype = MPI_DATATYPE_NULL;
    goto fn_exit;
}

static inline MPI_Datatype collapse_dtypes_recv_prep(const void *rbuf,
                                                     MPI_Count rcount,
                                                     MPI_Datatype mpi_dtype,
                                                     int num_procs, collapse_dtypes_req_t * req)
{
    int contig;
    MPIR_Datatype *dtype_ptr;
    MPI_Datatype basic_dtype = MPI_DATATYPE_NULL;
    MPI_Aint true_lb;
    size_t data_size;

    MPIDI_Datatype_get_info(rcount, mpi_dtype, contig, data_size, dtype_ptr, true_lb);
    MPIR_Datatype_get_basic_type(mpi_dtype, basic_dtype);
    req->basic_size = MPIR_Datatype_get_basic_size(basic_dtype);

    if ((basic_dtype == mpi_dtype) || (basic_dtype == MPI_DATATYPE_NULL) || !basic_dtype) {
        goto fn_fail;
    }

    if (contig || !data_size) {
        req->rbuf_tmp = (char *) rbuf + true_lb;
        req->rcount_tmp = data_size / req->basic_size;
        MPIR_Assert((data_size % req->basic_size) == 0);
    } else {
        MPI_Aint size_of_pack_buffer;
        MPIR_Pack_size(rcount, mpi_dtype, &size_of_pack_buffer);
        req->rbuf_tmp = req->rbuf_free =
            MPL_malloc(size_of_pack_buffer * num_procs, MPL_MEM_BUFFER);
        req->rcount_tmp = size_of_pack_buffer / req->basic_size;
        MPIR_Assert((size_of_pack_buffer % req->basic_size) == 0);
    }

  fn_exit:
    return basic_dtype;
  fn_fail:
    basic_dtype = MPI_DATATYPE_NULL;
    goto fn_exit;
}

static inline void collapse_dtypes_recv_done(void *rbuf,
                                             MPI_Count rcount,
                                             MPI_Datatype mpi_dtype,
                                             int num_procs, collapse_dtypes_req_t * req)
{
    if (req->rbuf_tmp && (req->rbuf_tmp == req->rbuf_free) && !req->rdispls_tmp) {
        MPI_Aint actual_unpacked_bytes;
        MPI_Aint size_of_pack_buffer = req->rcount_tmp * req->basic_size * num_procs;
        int rc = MPIR_Typerep_unpack(req->rbuf_tmp, size_of_pack_buffer, rbuf, rcount * num_procs,
                                     mpi_dtype, 0, &actual_unpacked_bytes, MPIR_TYPEREP_FLAG_NONE);
        MPIR_Assert(rc == MPI_SUCCESS);
        MPIR_Assert(actual_unpacked_bytes == size_of_pack_buffer);
    }
}

static inline MPI_Datatype collapse_dtypes_recv_prepv(const void *rbuf,
                                                      const MPI_Count * rcounts,
                                                      const MPI_Aint * rdispls,
                                                      MPI_Datatype mpi_dtype,
                                                      int num_procs, collapse_dtypes_req_t * req)
{
    MPI_Aint *len_vec;
    MPI_Aint total_len = 0;
    MPI_Datatype basic_dtype = MPI_DATATYPE_NULL;
    MPI_Aint size_of_pack_buffer;

    MPIR_Datatype_get_basic_type(mpi_dtype, basic_dtype);
    req->basic_size = MPIR_Datatype_get_basic_size(basic_dtype);

    if ((basic_dtype == mpi_dtype) || (basic_dtype == MPI_DATATYPE_NULL) || !basic_dtype) {
        goto fn_fail;
    }

    len_vec = MPL_malloc(num_procs * sizeof(MPI_Aint), MPL_MEM_COLL);
    MPIR_Assert(len_vec);
    req->rcounts_tmp = MPL_malloc(num_procs * sizeof(MPI_Aint), MPL_MEM_COLL);
    MPIR_Assert(req->rcounts_tmp);
    req->rdispls_tmp = MPL_malloc(num_procs * sizeof(MPI_Aint), MPL_MEM_COLL);
    MPIR_Assert(req->rdispls_tmp);

    for (int i = 0; i < num_procs; i++) {
        MPIR_Pack_size(rcounts[i], mpi_dtype, &size_of_pack_buffer);
        len_vec[i] = size_of_pack_buffer;
        total_len += size_of_pack_buffer;
    }

    req->rbuf_tmp = req->rbuf_free = MPL_malloc(total_len, MPL_MEM_BUFFER);
    MPIR_Assert(req->rbuf_tmp);

    for (int i = 0; i < num_procs; i++) {
        req->rcounts_tmp[i] = len_vec[i] / req->basic_size;
        MPIR_Assert((len_vec[i] % req->basic_size) == 0);
        req->rdispls_tmp[i] = i ? (req->rdispls_tmp[i - 1] + len_vec[i - 1] / req->basic_size) : 0;
    }
    MPL_free(len_vec);

  fn_exit:
    return basic_dtype;
  fn_fail:
    basic_dtype = MPI_DATATYPE_NULL;
    goto fn_exit;
}

static inline void collapse_dtypes_recv_donev(void *rbuf,
                                              const MPI_Count rcounts[],
                                              const MPI_Aint rdispls[],
                                              MPI_Datatype mpi_dtype,
                                              int num_procs, collapse_dtypes_req_t * req)
{
    if (req->rbuf_tmp && req->rdispls_tmp) {
        MPI_Aint extent = 0;
        MPI_Aint actual_unpacked_bytes;
        char *buf_ptr = NULL;
        char *tmp_ptr = NULL;
        MPIR_Datatype_get_extent_macro(mpi_dtype, extent);
        for (int i = 0; i < num_procs; i++) {
            tmp_ptr = (char *) req->rbuf_tmp + req->rdispls_tmp[i] * req->basic_size;
            buf_ptr = (char *) rbuf + rdispls[i] * extent;
            int rc = MPIR_Typerep_unpack(tmp_ptr, req->rcounts_tmp[i] * req->basic_size, buf_ptr,
                                         rcounts[i], mpi_dtype, 0, &actual_unpacked_bytes,
                                         MPIR_TYPEREP_FLAG_NONE);
            MPIR_Assert(rc == MPI_SUCCESS);
            MPIR_Assert(actual_unpacked_bytes == (req->rcounts_tmp[i] * req->basic_size));
        }
    }
}

static inline int collapse_dtypes(MPIX_Collops_algorithm_function * algorithm_fn, int collop,
                                  const void *sbuf, MPI_Count scount, const MPI_Count scounts[],
                                  const MPI_Aint sdispls[], MPI_Datatype sdatatype, void *rbuf,
                                  MPI_Count rcount, const MPI_Count rcounts[],
                                  const MPI_Aint rdispls[], MPI_Datatype rdatatype, MPI_Op op,
                                  int root, MPIR_Comm * comm_ptr, void *extra_state)
{
    int mpi_errno = MPI_SUCCESS;
    collapse_dtypes_req_t req = { 0 };
    int comm_rank = MPIR_Comm_rank(comm_ptr);
    int comm_size = MPIR_Comm_size(comm_ptr);
    MPI_Datatype basic_sdatatype = MPI_DATATYPE_NULL;
    MPI_Datatype basic_rdatatype = MPI_DATATYPE_NULL;

    switch (collop) {
        case MPIX_COLLOP_BCAST:
            if (comm_rank == root) {
                basic_sdatatype =
                    collapse_dtypes_send(sbuf, scount, sdatatype, 1 /* single send chunk */ , &req);
            } else {
                basic_rdatatype =
                    collapse_dtypes_recv_prep(rbuf, rcount, rdatatype, 1 /* single send chunk */ ,
                                              &req);
            }
            break;
        case MPIX_COLLOP_SCATTER:
            if (comm_rank == root) {
                basic_sdatatype = collapse_dtypes_send(sbuf, scount, sdatatype, comm_size, &req);
            }
            if ((comm_rank != root) || (rbuf != MPI_IN_PLACE)) {
                basic_rdatatype =
                    collapse_dtypes_recv_prep(rbuf, rcount, rdatatype, 1 /* single send chunk */ ,
                                              &req);
            }
            break;
        case MPIX_COLLOP_SCATTERV:
            if (comm_rank == root) {
                basic_sdatatype =
                    collapse_dtypes_sendv(sbuf, scounts, sdispls, sdatatype, comm_size, &req);
            }
            if ((comm_rank != root) || (rbuf != MPI_IN_PLACE)) {
                basic_rdatatype =
                    collapse_dtypes_recv_prep(rbuf, rcount, rdatatype, 1 /* single recv chunk */ ,
                                              &req);
            }
            break;
        case MPIX_COLLOP_GATHER:
            if (comm_rank == root) {
                basic_rdatatype =
                    collapse_dtypes_recv_prep(rbuf, rcount, rdatatype, comm_size, &req);
            }
            if ((comm_rank != root) || (sbuf != MPI_IN_PLACE)) {
                basic_sdatatype =
                    collapse_dtypes_send(sbuf, scount, sdatatype, 1 /* single send chunk */ , &req);
            }
            break;
        case MPIX_COLLOP_GATHERV:
            if (comm_rank == root) {
                basic_rdatatype =
                    collapse_dtypes_recv_prepv(rbuf, rcounts, rdispls, rdatatype, comm_size, &req);
            }
            if ((comm_rank != root) || (sbuf != MPI_IN_PLACE)) {
                basic_sdatatype =
                    collapse_dtypes_send(sbuf, scount, sdatatype, 1 /* single send chunk */ , &req);
            }
            break;
        case MPIX_COLLOP_ALLGATHER:
            basic_rdatatype = collapse_dtypes_recv_prep(rbuf, rcount, rdatatype, comm_size, &req);
            if (sbuf != MPI_IN_PLACE) {
                basic_sdatatype =
                    collapse_dtypes_send(sbuf, scount, sdatatype, 1 /* single send chunk */ , &req);
            }
            break;
        case MPIX_COLLOP_ALLGATHERV:
            basic_rdatatype =
                collapse_dtypes_recv_prepv(rbuf, rcounts, rdispls, rdatatype, comm_size, &req);
            if (sbuf != MPI_IN_PLACE) {
                basic_sdatatype =
                    collapse_dtypes_send(sbuf, scount, sdatatype, 1 /* single send chunk */ , &req);
            }
            break;
        case MPIX_COLLOP_ALLTOALL:
            basic_rdatatype = collapse_dtypes_recv_prep(rbuf, rcount, rdatatype, comm_size, &req);
            if (sbuf != MPI_IN_PLACE) {
                basic_sdatatype = collapse_dtypes_send(sbuf, scount, sdatatype, comm_size, &req);
            }
            break;
        case MPIX_COLLOP_ALLTOALLV:
            basic_rdatatype =
                collapse_dtypes_recv_prepv(rbuf, rcounts, rdispls, rdatatype, comm_size, &req);
            if (sbuf != MPI_IN_PLACE) {
                basic_sdatatype =
                    collapse_dtypes_sendv(sbuf, scounts, sdispls, sdatatype, comm_size, &req);
            }
            break;
    };

    mpi_errno = algorithm_fn(collop,
                             req.sbuf_tmp ? req.sbuf_tmp : sbuf,
                             req.scount_tmp ? req.scount_tmp : scount,
                             req.scounts_tmp ? req.scounts_tmp : scounts,
                             req.sdispls_tmp ? req.sdispls_tmp : sdispls,
                             basic_sdatatype != MPI_DATATYPE_NULL ? basic_sdatatype : sdatatype,
                             req.rbuf_tmp ? req.rbuf_tmp : rbuf,
                             req.rcount_tmp ? req.rcount_tmp : rcount,
                             req.rcounts_tmp ? req.rcounts_tmp : rcounts,
                             req.rdispls_tmp ? req.rdispls_tmp : rdispls,
                             basic_rdatatype != MPI_DATATYPE_NULL ? basic_rdatatype : rdatatype,
                             op, root, comm_ptr->handle, comm_ptr->collops.comm_extra_state);

    if (mpi_errno != MPI_SUCCESS) {
        goto fn_exit;
    }

    switch (collop) {
        case MPIX_COLLOP_BCAST:
        case MPIX_COLLOP_SCATTER:
        case MPIX_COLLOP_SCATTERV:
            collapse_dtypes_recv_done(rbuf, rcount, rdatatype, 1 /* single recv chunk */ , &req);
            break;
        case MPIX_COLLOP_GATHER:
        case MPIX_COLLOP_ALLGATHER:
        case MPIX_COLLOP_ALLTOALL:
            collapse_dtypes_recv_done(rbuf, rcount, rdatatype, comm_size, &req);
            break;
        case MPIX_COLLOP_GATHERV:
        case MPIX_COLLOP_ALLGATHERV:
        case MPIX_COLLOP_ALLTOALLV:
            collapse_dtypes_recv_donev(rbuf, rcounts, rdispls, rdatatype, comm_size, &req);
            break;
    };

  fn_exit:
    MPL_free(req.sbuf_free);
    MPL_free(req.rbuf_free);
    MPL_free(req.scounts_tmp);
    MPL_free(req.sdispls_tmp);
    MPL_free(req.rcounts_tmp);
    MPL_free(req.rdispls_tmp);
    return mpi_errno;
}
