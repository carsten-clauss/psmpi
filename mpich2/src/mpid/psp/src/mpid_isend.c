/*
 * ParaStation
 *
 * Copyright (C) 2006-2021 ParTec Cluster Competence Center GmbH, Munich
 * Copyright (C) 2021-2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#ifdef PSCOM_ALLIN
#define _GNU_SOURCE
#define __USE_GNU
#ifndef LIBDIR
#define LIBDIR ""
#endif
#endif

#include "mpidimpl.h"
#include "mpid_psp_packed_msg.h"
#include "mpid_psp_request.h"
#include "mpid_psp_datatype.h"

static inline void sendrequest_common_done(pscom_request_t * preq)
{
    /* This is an pscom.io_done call. Global lock state undefined! */
    MPIR_Request *req = preq->user->type.sr.mpid_req;
    if (pscom_req_successful(preq)) {
        req->status.MPI_ERROR = MPI_SUCCESS;
    } else if (preq->state & PSCOM_REQ_STATE_CANCELED) {
        req->status.MPI_ERROR = MPI_SUCCESS;
        MPIR_STATUS_SET_CANCEL_BIT(req->status, TRUE);
    } else {
        static char state_str[100];
        snprintf(state_str, 100, "request state:%s", pscom_req_state_str(preq->state));
        req->status.MPI_ERROR = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
                                                     "mpid_isend_done", __LINE__,
                                                     MPI_ERR_OTHER, "**write",
                                                     "**write %s", state_str);
    }

    if (preq->xheader.user.send.common.type == MPID_PSP_MSGTYPE_DATA_REQUEST_ACK) {
        /* from synchronous send. */
        if (pscom_req_successful(preq)) {
            /* Also wait for the ack (or ack cancel) */
            MPID_PSP_RecvAck(req);
        }
    }

/*	assert(*(req->cc_ptr) == 1);  */
    MPID_PSP_Subrequest_completed(req);
    MPIR_Request_free(req);
}


static
void sendrequest_done(pscom_request_t * preq)
{
    /* This is an pscom.io_done call. Global lock state undefined! */
    MPIR_Request *req = preq->user->type.sr.mpid_req;

    MPID_PSP_packed_msg_cleanup(&req->dev.kind.send.msg);

    sendrequest_common_done(preq);
}


static
void sendrequest_prepare_xheader(MPIR_Request * req, int tag, MPIR_Comm * comm, int context_offset,
                                 enum MPID_PSP_MSGTYPE type)
{
    pscom_request_t *preq = req->dev.kind.common.pscom_req;
    MPID_PSCOM_XHeader_Send_t *xheader = &preq->xheader.user.send;

    xheader->common.tag = tag;
    xheader->common.context_id = comm->context_id + context_offset;
    xheader->common.type = type;
    xheader->common._reserved_ = 0;
    xheader->common.src_rank = comm->rank;
    preq->xheader_len = sizeof(*xheader);

    req->status.MPI_TAG = tag;
    req->status.MPI_SOURCE = MPI_PROC_NULL;
}


static
void sendrequest_prepare_destination(MPIR_Request * req, MPIR_Comm * comm, int rank)
{
    pscom_request_t *preq = req->dev.kind.common.pscom_req;

    preq->connection = MPID_PSCOM_rank2connection(comm, rank);
}


static
void sendrequest_prepare_cleanup(MPIR_Request * req)
{
    pscom_request_t *preq = req->dev.kind.common.pscom_req;

    preq->ops.io_done = sendrequest_done;
}


static
int sendrequest_prepare_data(MPIR_Request * req, const void *buf, MPI_Aint count,
                             MPI_Datatype datatype, size_t * len)
{
    struct MPID_DEV_Request_send *sreq = &req->dev.kind.send;
    pscom_request_t *preq = sreq->common.pscom_req;
    int ret;

    /* Data */
    ret = MPID_PSP_packed_msg_prepare(buf, count, datatype, &sreq->msg);
    if (unlikely(ret != MPI_SUCCESS))
        goto err_create_packed_msg;

    preq->data_len = sreq->msg.msg_sz;
    preq->data = sreq->msg.msg;

    MPIR_STATUS_SET_COUNT(req->status, preq->data_len);
    MPIR_STATUS_SET_CANCEL_BIT(req->status, FALSE)

        * len = preq->data_len;

    return MPI_SUCCESS;
    /* --- */
  err_create_packed_msg:
    return ret;
}


static
void copy_data(MPIR_Request * req, const void *buf, MPI_Aint count, MPI_Datatype datatype)
{
    struct MPID_DEV_Request_send *sreq = &req->dev.kind.send;
    MPID_PSP_packed_msg_pack(buf, count, datatype, &sreq->msg);
}


static inline
    int MPID_PSP_Sendtype(const void *buf, MPI_Aint count, MPI_Datatype datatype, int rank,
                          int tag, MPIR_Comm * comm, int context_offset,
                          MPIR_Request ** request, enum MPID_PSP_MSGTYPE type)
{
    int mpi_errno = MPI_SUCCESS;
    MPIR_Request *req;
    size_t len;

#ifdef MPID_PSP_HISTOGRAM
    if (unlikely(MPIDI_Process.env.enable_histogram) &&
        unlikely(MPIDI_Process.stats.histo.points == 0)) {

        int idx;
        int limit;

        MPIDI_Process.stats.histo.points = 1;

        for (limit = MPIDI_Process.stats.histo.min_size;
             limit <= MPIDI_Process.stats.histo.max_size;
             limit <<= MPIDI_Process.stats.histo.step_width) {
            MPIDI_Process.stats.histo.points++;
        }

        MPIDI_Process.stats.histo.limit =
            MPL_malloc(MPIDI_Process.stats.histo.points * sizeof(int), MPL_MEM_OBJECT);
        MPIDI_Process.stats.histo.count =
            MPL_malloc(MPIDI_Process.stats.histo.points * sizeof(long long int), MPL_MEM_OBJECT);

        for (idx = 0, limit = MPIDI_Process.stats.histo.min_size;
             idx < MPIDI_Process.stats.histo.points;
             ++idx, limit <<= MPIDI_Process.stats.histo.step_width) {
            MPIDI_Process.stats.histo.limit[idx] = limit;
            MPIDI_Process.stats.histo.count[idx] = 0;
        }
    }
#endif

/*
  printf("#%d ps--- %s() called\n", MPIDI_Process.my_pg_rank, __func__);

  printf("#%d buf %p, count %d, datatype 0x%0x, rank %d, tag %d, comm %p, off %d\n",
  MPIDI_Process.my_pg_rank, buf, count, datatype, rank, tag, comm, context_offset);
  printf("#%d ctx.id %d ctx.rank %d, ctx.name %s\n",
  MPIDI_Process.my_pg_rank, comm->context_id, comm->rank, comm->name);
*/

    req = MPIR_Request_create(MPIR_REQUEST_KIND__SEND);
    if (unlikely(!req))
        goto err_request_send_create;
    req->comm = comm;
    MPIR_Comm_add_ref(comm);

    if (rank >= 0) {
        mpi_errno = sendrequest_prepare_data(req, buf, count, datatype, &len);
        if (unlikely(mpi_errno != MPI_SUCCESS))
            goto err_prepare_data_failed;

        copy_data(req, buf, count, datatype);

        sendrequest_prepare_xheader(req, tag, comm, context_offset, type);
        sendrequest_prepare_destination(req, comm, rank);
        sendrequest_prepare_cleanup(req);

        MPIR_Request_add_ref(req);

#ifdef MPID_PSP_HISTOGRAM
        if (unlikely(MPIDI_Process.env.enable_histogram)) {
            pscom_request_t *preq = req->dev.kind.common.pscom_req;
            if ((MPIDI_Process.stats.histo.con_type_int < 0) ||
                (MPIDI_Process.stats.histo.con_type_int == preq->connection->type)) {

                int idx;
                for (idx = 0; idx < MPIDI_Process.stats.histo.points; ++idx) {
                    if (len <= MPIDI_Process.stats.histo.limit[idx] ||
                        (idx == MPIDI_Process.stats.histo.points - 1)) {
                        ++MPIDI_Process.stats.histo.count[idx];
                        break;
                    }
                }
            }
        }
#endif
        pscom_post_send(req->dev.kind.send.common.pscom_req);

    } else
        switch (rank) {
            case MPI_PROC_NULL:
                MPIR_Status_set_procnull(&req->status);
                MPIDI_PSP_Request_set_completed(req);
                break;
            case MPI_ANY_SOURCE:
            case MPI_ROOT:
            default:
                /* printf("#%d ps--- %s(): MPI_ERR_RANK: rank = %d, comm->size=%d, comm->name=%s\n",
                 * MPIDI_Process.my_pg_rank, __func__, rank, comm->local_size, comm->name ? comm->name : "?"); */
                mpi_errno = MPI_ERR_RANK;
        }

    assert(mpi_errno == MPI_SUCCESS);
    *request = req;

    return MPI_SUCCESS;
    /* --- */
  err_request_send_create:
    mpi_errno = MPI_ERR_NO_MEM;
  err_prepare_data_failed:
    MPIR_Request_free(req);
    return mpi_errno;
}



int MPIDI_PSP_Isend(const void *buf, MPI_Aint count, MPI_Datatype datatype, int rank,
                    int tag, MPIR_Comm * comm, int context_offset, MPIR_Request ** request)
{
    int mpi_errno;
    mpi_errno = MPID_PSP_Sendtype(buf, count, datatype, rank, tag,
                                  comm, context_offset, request, MPID_PSP_MSGTYPE_DATA);
    return mpi_errno;
}



int MPID_Isend_coll(const void *buf, MPI_Aint count, MPI_Datatype datatype, int rank, int tag,
                    MPIR_Comm * comm, int context_offset, MPIR_Request ** request,
                    MPIR_Errflag_t * errflag)
{
    int mpi_errno = MPI_SUCCESS;

    switch (*errflag) {
        case MPIR_ERR_NONE:
            break;
        case MPIR_ERR_PROC_FAILED:
            MPIR_TAG_SET_PROC_FAILURE_BIT(tag);
            break;
        default:
            MPIR_TAG_SET_ERROR_BIT(tag);
    }

    mpi_errno = MPIDI_PSP_Isend(buf, count, datatype, rank, tag, comm, context_offset, request);

    return mpi_errno;
}


int MPIDI_PSP_Issend(const void *buf, MPI_Aint count, MPI_Datatype datatype, int rank, int tag,
                     MPIR_Comm * comm, int context_offset, MPIR_Request ** request)
{
    int mpi_errno;
    mpi_errno = MPID_PSP_Sendtype(buf, count, datatype, rank, tag,
                                  comm, context_offset, request, MPID_PSP_MSGTYPE_DATA_REQUEST_ACK);

    return mpi_errno;
}

#ifdef PSCOM_ALLIN
#define PSCOM_ALLIN_INCLUDE_TOKEN
#include "mpid_irecv.c"
#if defined(MPL_USE_MEMORY_TRACING)
/* MPICH's memory tracing disables the usability of malloc & co. by macro overloading.
   Therefore, we have to undefine those macros here before including pscom_all.c */
#undef malloc
#undef free
#undef calloc
#undef realloc
#undef strdup
#endif
#include "pscom_all.c"
#endif
