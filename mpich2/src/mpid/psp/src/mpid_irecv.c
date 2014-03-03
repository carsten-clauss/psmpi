/*
 * ParaStation
 *
 * Copyright (C) 2006-2010 ParTec Cluster Competence Center GmbH, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 *
 * Author:	Jens Hauke <hauke@par-tec.com>
 */

#include <assert.h>
#include "mpidimpl.h"
#include "mpid_psp_request.h"
#include "mpid_psp_packed_msg.h"
#include "mpid_psp_datatype.h"

static
int cb_accept_data(pscom_request_t *request,
		   pscom_connection_t *connection,
		   pscom_header_net_t *header_net)
{
	MPID_Request *req = request->user->type.sr.mpid_req;
	struct MPID_DEV_Request_recv *rreq = &req->dev.kind.recv;
	MPID_PSCOM_XHeader_t *xhead = &header_net->xheader->user.common;

	return  (xhead->type <= MPID_PSP_MSGTYPE_DATA_REQUEST_ACK) &&
		((xhead->tag == rreq->tag) || (rreq->tag == MPI_ANY_TAG)) &&
		(xhead->context_id == rreq->context_id);
}


static
int cb_accept_ack(pscom_request_t *request,
		  pscom_connection_t *connection,
		  pscom_header_net_t *header_net)
{
	MPID_PSCOM_XHeader_t *xhead = &request->xheader.user.common;

	MPID_PSCOM_XHeader_t *xhead_net = &header_net->xheader->user.common;

	return  ((xhead_net->type == MPID_PSP_MSGTYPE_DATA_ACK) ||
		 (xhead_net->type == MPID_PSP_MSGTYPE_CANCEL_DATA_ACK)) &&
		(xhead_net->tag == xhead->tag) &&
		(xhead_net->context_id == xhead->context_id);
}


static
void cb_io_done_ack(pscom_request_t *request)
{
	/* This is an pscom.io_done call. Global lock state undefined! */
	MPID_PSCOM_XHeader_t *xhead = &request->xheader.user.common;

	/* Todo: Test for pscom_req_successful(request) ? */
	MPID_Request *send_req = request->user->type.sr.mpid_req;

	if (xhead->type == MPID_PSP_MSGTYPE_CANCEL_DATA_ACK) {
		send_req->status.cancelled = 1;
	}

	MPID_PSP_Subrequest_completed(send_req);
	MPID_DEV_Request_release_ref(send_req, MPID_REQUEST_SEND);
	request->user->type.sr.mpid_req = NULL;
	pscom_request_free(request);
}


static inline
void receive_done(pscom_request_t *request)
{
	/* This is an pscom.io_done call. Global lock state undefined! */
	MPID_Request *req = request->user->type.sr.mpid_req;
	MPID_PSCOM_XHeader_t *xhead = &request->xheader.user.common;

	req->status.count = request->header.data_len; /* status.count == datalen, or == datalen/sizeof(mpitype) ?? */
	req->status.MPI_SOURCE = xhead->src_rank;
	req->status.MPI_TAG = xhead->tag;
	if (pscom_req_successful(request)) {
		assert(request->xheader_len == request->header.xheader_len);
		req->status.MPI_ERROR = MPI_SUCCESS;

		if (unlikely(xhead->type == MPID_PSP_MSGTYPE_DATA_REQUEST_ACK)) {
			/* synchronous send : send ack */
			MPID_PSP_SendCtrl(xhead->tag, xhead->context_id, req->comm->rank,
					  request->connection, MPID_PSP_MSGTYPE_DATA_ACK);
		}
	} else if (request->state & PSCOM_REQ_STATE_TRUNCATED) {
		assert (request->header.data_len > request->data_len);
		req->status.MPI_ERROR = MPI_ERR_TRUNCATE;
	} else if (request->state & PSCOM_REQ_STATE_CANCELED) {
		/* ToDo: MPI_ERROR = MPI_SUCCESS on cancelled ? */
		req->status.MPI_ERROR = MPI_SUCCESS;
		req->status.cancelled = 1;
	} else {
		static char state_str[100];
		snprintf(state_str, 100, "request state:%s", pscom_req_state_str(request->state));
		req->status.MPI_ERROR = MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL,
							     "mpid_irecv_done", __LINE__,
							     MPI_ERR_OTHER, "**read",
							     "**read %s", state_str);
	}

	MPID_PSP_Subrequest_completed(req);
	MPID_PSP_Request_dequeue(req, MPID_REQUEST_RECV);
}


static
void receive_done_noncontig(pscom_request_t *request)
{
	/* This is an pscom.io_done call. Global lock state undefined! */
	MPID_Request *req = request->user->type.sr.mpid_req;
	struct MPID_DEV_Request_recv *rreq = &req->dev.kind.recv;

	if (pscom_req_successful(request) || (request->state & PSCOM_REQ_STATE_TRUNCATED)) {
		MPID_PSP_packed_msg_unpack(rreq->addr, rreq->count, rreq->datatype,
					   &rreq->msg, request->header.data_len);
	}

	/* Noncontig receive request */
	/* cleanup temp buffer and datatype */
	MPID_PSP_packed_msg_cleanup_datatype(&rreq->msg, rreq->datatype);

	receive_done(request);
}


static
int cb_accept_cancel_data(pscom_request_t *request,
			  pscom_connection_t *connection,
			  pscom_header_net_t *header_net)
{
	MPID_PSCOM_XHeader_t *xhead = &request->xheader.user.common;
	MPID_PSCOM_XHeader_t *xhead_net = &header_net->xheader->user.common;

	return  (xhead_net->type == MPID_PSP_MSGTYPE_DATA_REQUEST_ACK) &&
		(xhead_net->tag == xhead->tag) &&
		(xhead_net->context_id == xhead->context_id);
}


static
void MPID_do_recv_cancel_data_request_ack(pscom_request_t *cancel_req)
{
	/* reuse cancel_req to eatup the generated request */
	MPID_PSCOM_XHeader_t *xhead = &cancel_req->xheader.user.common;

	cancel_req->ops.recv_accept = cb_accept_cancel_data;
	cancel_req->ops.io_done = NULL; /* delay pscom_request_free() / see below! */

	pscom_post_recv(cancel_req);

	if (!(cancel_req->state & PSCOM_REQ_STATE_IO_STARTED)) {
		/* post_recv should find a generated request. If not, we
		   cannot cancel, because ack is already send. So we
		   cancel the cancel.
		*/
		pscom_cancel_recv(cancel_req);
		pscom_request_free(cancel_req);
	} else {
		/* send cancel ack */
		MPID_PSP_SendCtrl(xhead->tag, xhead->context_id, MPI_PROC_NULL,
				  cancel_req->connection, MPID_PSP_MSGTYPE_CANCEL_DATA_ACK);
		if (pscom_req_is_done(cancel_req)) {
			pscom_request_free(cancel_req);
		} else {
			cancel_req->ops.io_done = pscom_request_free;
		}
	}
}


static
pscom_request_t *MPID_do_recv_forward_to(void (*io_done)(pscom_request_t *req), pscom_header_net_t *header_net)
{
	pscom_request_t *req = PSCOM_REQUEST_CREATE();

	assert(header_net->xheader_len <= sizeof(req->xheader));

	req->xheader_len = header_net->xheader_len;
	req->ops.io_done = io_done;

	return req;
}


static
pscom_request_t *receive_dispatch(pscom_connection_t *connection,
				  pscom_header_net_t *header_net)
{
	MPID_PSCOM_XHeader_t *xhead = &header_net->xheader->user.common;

	if (xhead->type == MPID_PSP_MSGTYPE_DATA) {
		/* fastpath */
		return NULL;
	}

	switch (xhead->type) {
	case MPID_PSP_MSGTYPE_RMA_PUT:
		return MPID_do_recv_rma_put(connection, &header_net->xheader->user.put);

	case MPID_PSP_MSGTYPE_RMA_ACCUMULATE:
		return MPID_do_recv_rma_accumulate(connection, header_net);

	case MPID_PSP_MSGTYPE_DATA_REQUEST_ACK:
		break;

	case MPID_PSP_MSGTYPE_CANCEL_DATA_REQUEST_ACK:
		return MPID_do_recv_forward_to(MPID_do_recv_cancel_data_request_ack, header_net);

	case MPID_PSP_MSGTYPE_RMA_GET_REQ:
		return MPID_do_recv_rma_get_req(connection, &header_net->xheader->user.get_req);

	case MPID_PSP_MSGTYPE_RMA_LOCK_EXCLUSIVE_REQUEST:
		return MPID_do_recv_forward_to(MPID_do_recv_rma_lock_exclusive_req, header_net);

	case MPID_PSP_MSGTYPE_RMA_LOCK_SHARED_REQUEST:
		return MPID_do_recv_forward_to(MPID_do_recv_rma_lock_shared_req, header_net);

	case MPID_PSP_MSGTYPE_RMA_UNLOCK_REQUEST:
		return MPID_do_recv_forward_to(MPID_do_recv_rma_unlock_req, header_net);
	}

	return NULL;
}


void MPID_enable_receive_dispach(void)
{
	if (!MPIDI_Process.socket->ops.default_recv) {
		MPIDI_Process.socket->ops.default_recv = receive_dispatch;
	} else {
		assert(MPIDI_Process.socket->ops.default_recv == receive_dispatch);
	}
}


static
void prepare_recvreq(MPID_Request *req, int tag, MPID_Comm * comm, int context_offset)
{
	struct MPID_DEV_Request_recv *rreq = &req->dev.kind.recv;
	pscom_request_t *preq = rreq->common.pscom_req;

	rreq->tag = tag;
	rreq->context_id = comm->recvcontext_id + context_offset;

	preq->ops.recv_accept = cb_accept_data;
	preq->xheader_len = sizeof(MPID_PSCOM_XHeader_Send_t);
}


static
void prepare_probereq(MPID_Request *req, int tag, MPID_Comm * comm, int context_offset)
{
	struct MPID_DEV_Request_recv *rreq = &req->dev.kind.recv;
	pscom_request_t *preq = rreq->common.pscom_req;

	prepare_recvreq(req, tag, comm, context_offset);
	preq->ops.recv_accept = cb_accept_data;
}


static
void prepare_data(MPID_Request *req, void * buf, int count, MPI_Datatype datatype)
{
	struct MPID_DEV_Request_recv *rreq = &req->dev.kind.recv;
	pscom_request_t *preq = rreq->common.pscom_req;
	int ret;

	ret = MPID_PSP_packed_msg_prepare(buf, count, datatype, &rreq->msg);
	if (unlikely(ret != MPI_SUCCESS)) goto err_alloc_tmpbuf;

	preq->data = rreq->msg.msg;
	preq->data_len = rreq->msg.msg_sz;

	return;
	/* --- */
err_alloc_tmpbuf: /* ToDo: */
	fprintf(stderr, "MPIU_Malloc() failed\n");
	exit(1);
}


static
void prepare_cleanup(MPID_Request *req, void * buf, int count, MPI_Datatype datatype)
{
	struct MPID_DEV_Request_recv *rreq = &req->dev.kind.recv;
	pscom_request_t *preq = rreq->common.pscom_req;

	preq->ops.io_done = receive_done;

	if (MPID_PSP_packed_msg_need_unpack(&rreq->msg)) {
		rreq->addr = buf;
		rreq->count = count;
		rreq->datatype = datatype;
		MPID_PSP_Datatype_add_ref(datatype);

		preq->ops.io_done = receive_done_noncontig;
	}
}


static
void prepare_source(MPID_Request *req, pscom_connection_t *con, pscom_socket_t *sock)
{
	struct MPID_DEV_Request_recv *rreq = &req->dev.kind.recv;
	pscom_request_t *preq = rreq->common.pscom_req;

	preq->connection = con;
	preq->socket = sock;
}


int MPID_Irecv(void * buf, int count, MPI_Datatype datatype, int rank, int tag,
	       MPID_Comm * comm, int context_offset, MPID_Request ** request)
{
	MPID_Request *req;
	pscom_connection_t *con;
	pscom_socket_t *sock;
/*
	printf("#%d ps--- %s() called\n", MPIDI_Process.my_pg_rank, __func__);
	printf("#%d buf %p, count %d, datatype 0x%0x, rank %d, tag %d, comm %p, off %d\n",
	       MPIDI_Process.my_pg_rank, buf, count, datatype, rank, tag, comm, context_offset);
	printf("#%d ctx.id %d ctx.rank %d, ctx.name %s\n",
	       MPIDI_Process.my_pg_rank, comm->context_id, comm->rank, comm->name);
*/
	req = MPID_DEV_Request_recv_create(comm);
	if (unlikely(!req)) goto err_request_recv_create;

	prepare_recvreq(req, tag, comm, context_offset);

	con = MPID_PSCOM_rank2connection(comm, rank);
	sock = MPIDI_Process.socket; /* ToDo: get socket from comm? */

	if (con || (rank == MPI_ANY_SOURCE)) {

		prepare_data(req, buf, count, datatype);
		prepare_source(req, con, sock);
		prepare_cleanup(req, buf, count, datatype);

		MPID_PSP_Request_enqueue(req);

		pscom_post_recv(req->dev.kind.recv.common.pscom_req);

	} else switch (rank) {
	case MPI_PROC_NULL:
		MPIR_Status_set_procnull(&req->status);
		_MPID_Request_set_completed(req);
		break;
	case MPI_ROOT:
	default:
		/* printf("%s(): MPI_ERR_RANK: rank = %d, comm->size=%d, comm->name=%s\n",
		   __func__, rank, comm->local_size, comm->name ? comm->name : "?"); */
		goto err_rank;
	}

	*request = req;

	return MPI_SUCCESS;
	/* --- */
 err_request_recv_create:
	return  MPI_ERR_NO_MEM;
	/* --- */
 err_rank:
	MPID_DEV_Request_release_ref(req, MPID_REQUEST_RECV);
	return  MPI_ERR_RANK;
}


void MPID_PSP_RecvAck(MPID_Request *send_req)
{
	pscom_request_t *preq;
	pscom_request_t *preq_send;
	MPID_PSCOM_XHeader_t *xhead;

	preq = PSCOM_REQUEST_CREATE();
	assert(preq != NULL);

	preq_send = send_req->dev.kind.send.common.pscom_req;

	preq->xheader_len = sizeof(*xhead);
	preq->ops.recv_accept = cb_accept_ack;
	preq->ops.io_done = cb_io_done_ack;
	preq->connection = preq_send->connection;
	assert(preq->connection != NULL);

	/* Copy xheader from send request */
	xhead = &preq->xheader.user.common;
	*xhead = preq_send->xheader.user.common;

	preq->user->type.sr.mpid_req = send_req;

	MPID_PSP_Subrequest_add(send_req);   /* Subrequest_completed(sendreq) and */
	MPID_DEV_Request_add_ref(send_req);  /* Request_release_ref(sendreq) in cb_receive_ack() */

	pscom_post_recv(preq);
}


static
void set_probe_status(pscom_request_t *req, MPI_Status *status)
{
	if (!status || status == MPI_STATUS_IGNORE) return;

	status->count = req->header.data_len;
	status->cancelled = (req->state & PSCOM_REQ_STATE_CANCELED) ? 1 : 0;
	status->MPI_SOURCE = req->xheader.user.common.src_rank;
	status->MPI_TAG    = req->xheader.user.common.tag;
	/* status->MPI_ERROR  = MPI_SUCCESS; */
}


int MPID_Probe(int rank, int tag, MPID_Comm * comm, int context_offset, MPI_Status * status)
{
	pscom_connection_t *con;
	pscom_socket_t *sock;
/*
	printf("#%d ps--- %s() called\n", MPIDI_Process.my_pg_rank, __func__);
	printf("#%d buf %p, count %d, datatype 0x%0x, rank %d, tag %d, comm %p, off %d\n",
	       MPIDI_Process.my_pg_rank, buf, count, datatype, rank, tag, comm, context_offset);
	printf("#%d ctx.id %d ctx.rank %d, ctx.name %s\n",
	       MPIDI_Process.my_pg_rank, comm->context_id, comm->rank, comm->name);
*/

	con = MPID_PSCOM_rank2connection(comm, rank);
	sock = MPIDI_Process.socket; /* ToDo: get socket from comm? */

	if (con || (rank == MPI_ANY_SOURCE)) {
		MPID_Request *req;
		req = MPID_DEV_Request_recv_create(comm);
		if (unlikely(!req)) goto err_request_recv_create;

		prepare_probereq(req, tag, comm, context_offset);

		prepare_source(req, con, sock);

		MPID_PSP_LOCKFREE_CALL(pscom_probe(req->dev.kind.recv.common.pscom_req));

		set_probe_status(req->dev.kind.recv.common.pscom_req, status);

		MPID_PSP_Subrequest_completed(req);
		MPID_DEV_Request_release_ref(req, MPID_REQUEST_RECV);
	} else switch (rank) {
	case MPI_PROC_NULL:
		MPIR_Status_set_procnull(status);
		break;
	case MPI_ROOT:
	default:
		/* printf("#%d ps--- %s(): MPI_ERR_RANK: rank = %d, comm->size=%d, comm->name=%s\n",
		   MPIDI_Process.my_pg_rank, __func__, rank, comm->local_size, comm->name ? comm->name : "?"); */
		goto err_rank;
	}

	return MPI_SUCCESS;
	/* --- */
 err_request_recv_create:
	return  MPI_ERR_NO_MEM;
	/* --- */
 err_rank:
	return  MPI_ERR_RANK;
}


int MPID_Iprobe(int rank, int tag, MPID_Comm * comm, int context_offset, int * flag, MPI_Status * status)
{
	pscom_connection_t *con;
	pscom_socket_t *sock;
/*
	printf("#%d ps--- %s() called\n", MPIDI_Process.my_pg_rank, __func__);
	printf("#%d buf %p, count %d, datatype 0x%0x, rank %d, tag %d, comm %p, off %d\n",
	       MPIDI_Process.my_pg_rank, buf, count, datatype, rank, tag, comm, context_offset);
	printf("#%d ctx.id %d ctx.rank %d, ctx.name %s\n",
	       MPIDI_Process.my_pg_rank, comm->context_id, comm->rank, comm->name);
*/

	con = MPID_PSCOM_rank2connection(comm, rank);
	sock = MPIDI_Process.socket; /* ToDo: get socket from comm? */

	if (con || (rank == MPI_ANY_SOURCE)) {
		MPID_Request *req;
		req = MPID_DEV_Request_recv_create(comm);
		if (unlikely(!req)) goto err_request_recv_create;

		prepare_probereq(req, tag, comm, context_offset);

		prepare_source(req, con, sock);

		*flag = pscom_iprobe(req->dev.kind.recv.common.pscom_req);
		if (*flag) {
			set_probe_status(req->dev.kind.recv.common.pscom_req, status);
		}

		MPID_PSP_Subrequest_completed(req);
		MPID_DEV_Request_release_ref(req, MPID_REQUEST_RECV);
	} else switch (rank) {
	case MPI_PROC_NULL:
		MPIR_Status_set_procnull(status);
		*flag = 1;
		break;
	case MPI_ROOT:
	default:
		/* printf("#%d ps--- %s(): MPI_ERR_RANK: rank = %d, comm->size=%d, comm->name=%s\n",
		   MPIDI_Process.my_pg_rank, __func__, rank, comm->local_size, comm->name ? comm->name : "?"); */
		goto err_rank;
	}

	return MPI_SUCCESS;
	/* --- */
 err_request_recv_create:
	return  MPI_ERR_NO_MEM;
	/* --- */
 err_rank:
	return  MPI_ERR_RANK;
}
