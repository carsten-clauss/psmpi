#include "mpi.h"
#include "gloo_wrapper.h"
#include "gloo/mpi/context.h"
#include "gloo/transport/tcp/device.h"
#include "gloo/barrier.h"
#include "gloo/broadcast.h"
#include "gloo/gather.h"
#include "gloo/allgather.h"
#include "gloo/scatter.h"
#include "gloo/reduce.h"
#include "gloo/allreduce.h"
#include "gloo/math.h"

#define _COLLOPS_GLOO_DTYPE_TO_BUFFER(_dtype, _buffer)                  \
    _dtype *_buffer = (_dtype *)_buffer ## _;

#define COLLOPS_GLOO_DTYPE_TO_BUFFER(_dtype, _buffer, _stmnt)           \
    if ((_dtype == MPI_CHAR )|| (_dtype == MPI_INT8_T) ||               \
        (_dtype == MPI_SIGNED_CHAR)) {                                  \
        _COLLOPS_GLOO_DTYPE_TO_BUFFER(char, _buffer);                   \
        _stmnt;                                                         \
    } else if ((_dtype == MPI_BYTE) || (_dtype == MPI_UINT8_T) ||       \
               (_dtype == MPI_UNSIGNED_CHAR)) {                         \
        _COLLOPS_GLOO_DTYPE_TO_BUFFER(unsigned char, _buffer);          \
        _stmnt;                                                         \
    } else if (_dtype == MPI_SHORT) {                                   \
        _COLLOPS_GLOO_DTYPE_TO_BUFFER(short int, _buffer);              \
        _stmnt;                                                         \
    } else if (_dtype == MPI_INT) {                                     \
        _COLLOPS_GLOO_DTYPE_TO_BUFFER(int, _buffer);                    \
        _stmnt;                                                         \
    } else if (_dtype == MPI_LONG) {                                    \
        _COLLOPS_GLOO_DTYPE_TO_BUFFER(long int, _buffer);               \
        _stmnt;                                                         \
    } else if (_dtype == MPI_FLOAT) {                                   \
        _COLLOPS_GLOO_DTYPE_TO_BUFFER(float, _buffer);                  \
        _stmnt;                                                         \
    } else if (_dtype == MPI_DOUBLE) {                                  \
        _COLLOPS_GLOO_DTYPE_TO_BUFFER(double, _buffer);                 \
        _stmnt;                                                         \
    } else {                                                            \
        /* fallback */                                                  \
        return -1;                                                      \
    }

#define _COLLOPS_GLOO_DTYPE_TO_BUFVEC(_dtype, _buffer, _nprocs)         \
        std::vector<_dtype*> _buffer(_nprocs);                          \
        for (size_t i = 0; i < _nprocs; i++) {                          \
            _buffer[i] = (_dtype*)_buffer ## _[i];                      \
        }

#define COLLOPS_GLOO_DTYPE_TO_BUFVEC(_dtype, _buffer, _nprocs, _stmnt)  \
    if ((_dtype == MPI_CHAR )|| (_dtype == MPI_INT8_T) ||               \
        (_dtype == MPI_SIGNED_CHAR)) {                                  \
        _COLLOPS_GLOO_DTYPE_TO_BUFVEC(char, _buffer, _nprocs);          \
        _stmnt;                                                         \
    } else if ((_dtype == MPI_BYTE) || (_dtype == MPI_UINT8_T) ||       \
               (_dtype == MPI_UNSIGNED_CHAR)) {                         \
        _COLLOPS_GLOO_DTYPE_TO_BUFVEC(unsigned char, _buffer, _nprocs); \
        _stmnt;                                                         \
    } else if (_dtype == MPI_SHORT) {                                   \
        _COLLOPS_GLOO_DTYPE_TO_BUFVEC(short int, _buffer, _nprocs);     \
        _stmnt;                                                         \
    } else if (_dtype == MPI_INT) {                                     \
        _COLLOPS_GLOO_DTYPE_TO_BUFVEC(int, _buffer, _nprocs);           \
        _stmnt;                                                         \
    } else if (_dtype == MPI_LONG) {                                    \
        _COLLOPS_GLOO_DTYPE_TO_BUFVEC(long int, _buffer, _nprocs);      \
        _stmnt;                                                         \
    } else if (_dtype == MPI_FLOAT) {                                   \
        _COLLOPS_GLOO_DTYPE_TO_BUFVEC(float, _buffer, _nprocs);         \
        _stmnt;                                                         \
    } else if (_dtype == MPI_DOUBLE) {                                  \
        _COLLOPS_GLOO_DTYPE_TO_BUFVEC(double, _buffer, _nprocs);        \
        _stmnt;                                                         \
    } else {                                                            \
        return -1;                                                      \
    }

template <typename T>
gloo::ReduceOptions::Func getSumFunction() {
    void (*func)(void*, const void*, const void*, size_t) = &::gloo::sum<T>;
    return gloo::ReduceOptions::Func(func);
}

template <typename T>
gloo::ReduceOptions::Func getProductFunction() {
    void (*func)(void*, const void*, const void*, size_t) = &::gloo::product<T>;
    return gloo::ReduceOptions::Func(func);
}

template <typename T>
gloo::ReduceOptions::Func getMinFunction() {
    void (*func)(void*, const void*, const void*, size_t) = &::gloo::min<T>;
    return gloo::ReduceOptions::Func(func);
}

template <typename T>
gloo::ReduceOptions::Func getMaxFunction() {
    void (*func)(void*, const void*, const void*, size_t) = &::gloo::max<T>;
    return gloo::ReduceOptions::Func(func);
}

#define COLLOPS_GLOO_REDUCE_MAP_OP_TO_FUNCTION(_opts, _dtype)		\
	switch (op) {							\
	case MPI_SUM:							\
	    _opts.setReduceFunction(getSumFunction<_dtype>());		\
	    break;							\
	case MPI_PROD:							\
	    _opts.setReduceFunction(getProductFunction<_dtype>());	\
	    break;							\
	case MPI_MIN:							\
	    _opts.setReduceFunction(getMinFunction<_dtype>());		\
	    break;							\
	case MPI_MAX:							\
	    _opts.setReduceFunction(getMaxFunction<_dtype>());		\
	    break;							\
	default:							\
	    return -1;							\
	}								\

#define COLLOPS_GLOO_REDUCE_MAP_DTYPE_AND_OP(_opts, _dtype, _op)	\
    if ((_dtype == MPI_CHAR )|| (_dtype == MPI_INT8_T) ||		\
	(_dtype == MPI_SIGNED_CHAR)) {					\
	COLLOPS_GLOO_REDUCE_MAP_OP_TO_FUNCTION(_opts, char);		\
    } else if ((_dtype == MPI_BYTE) || (_dtype == MPI_UINT8_T) ||	\
	       (_dtype == MPI_UNSIGNED_CHAR)) {				\
    	COLLOPS_GLOO_REDUCE_MAP_OP_TO_FUNCTION(_opts, unsigned char);	\
    } else if (_dtype == MPI_SHORT) {					\
	COLLOPS_GLOO_REDUCE_MAP_OP_TO_FUNCTION(_opts, short int);	\
    } else if (_dtype == MPI_INT) {					\
	COLLOPS_GLOO_REDUCE_MAP_OP_TO_FUNCTION(_opts, int);		\
    } else if (_dtype == MPI_LONG) {					\
	COLLOPS_GLOO_REDUCE_MAP_OP_TO_FUNCTION(_opts, long int);	\
    } else if (_dtype == MPI_FLOAT) {					\
	COLLOPS_GLOO_REDUCE_MAP_OP_TO_FUNCTION(_opts, float);		\
    } else if (_dtype == MPI_DOUBLE) {					\
	COLLOPS_GLOO_REDUCE_MAP_OP_TO_FUNCTION(_opts, double);		\
    } else {								\
	return -1;							\
    }

extern "C" {

gloo_context_t gloo_mpi_create_context(MPI_Comm comm)
{
    auto ctx = std::make_shared<gloo::mpi::Context>(comm);
    auto dev = gloo::transport::tcp::CreateDevice("localhost");
    ctx->connectFullMesh(dev);
    return new std::shared_ptr<gloo::Context>(ctx);
}

void gloo_mpi_free_context(gloo_context_t ctx_)
{
    delete static_cast<std::shared_ptr<gloo::Context>*>(ctx_);
}

int gloo_mpi_barrier(gloo_context_t ctx_)
{
    auto ctx = *static_cast<std::shared_ptr<gloo::Context>*>(ctx_);
    try {
        gloo::BarrierOptions opts(ctx);
        gloo::barrier(opts);
        return 0;
    } catch (...) {
        return -1;
    }
}

int gloo_mpi_broadcast(gloo_context_t ctx_, void* buffer_, size_t count, MPI_Datatype dtype,
		       int root)
{
    auto ctx = *static_cast<std::shared_ptr<gloo::Context>*>(ctx_);
    try {
	gloo::BroadcastOptions opts(ctx);
	opts.setRoot(root);
	COLLOPS_GLOO_DTYPE_TO_BUFFER(dtype, buffer, opts.setOutput(buffer, count));
	gloo::broadcast(opts);
        return 0;
    } catch (...) {
        return -1;
    }
}

int gloo_mpi_gather(gloo_context_t ctx_, void* sendbuffer_, size_t sendcount,
		    MPI_Datatype sendtype, void* recvbuffer_, size_t recvcount,
		    MPI_Datatype recvtype, int root)
{
    auto ctx = *static_cast<std::shared_ptr<gloo::Context>*>(ctx_);
    try {
	gloo::GatherOptions opts(ctx);
	opts.setRoot(root);
	COLLOPS_GLOO_DTYPE_TO_BUFFER(sendtype, sendbuffer, opts.setInput(sendbuffer, sendcount));
	COLLOPS_GLOO_DTYPE_TO_BUFFER(recvtype, recvbuffer, opts.setOutput(recvbuffer, recvcount));
	gloo::gather(opts);
        return 0;
    } catch (...) {
        return -1;
    }
}

int gloo_mpi_allgather(gloo_context_t ctx_, void* sendbuffer_, size_t sendcount,
		       MPI_Datatype sendtype, void* recvbuffer_, size_t recvcount,
		       MPI_Datatype recvtype)
{
    auto ctx = *static_cast<std::shared_ptr<gloo::Context>*>(ctx_);
    try {
	gloo::AllgatherOptions opts(ctx);
	COLLOPS_GLOO_DTYPE_TO_BUFFER(sendtype, sendbuffer, opts.setInput(sendbuffer, sendcount));
	COLLOPS_GLOO_DTYPE_TO_BUFFER(recvtype, recvbuffer, opts.setOutput(recvbuffer, recvcount));
	gloo::allgather(opts);
        return 0;
    } catch (...) {
        return -1;
    }
}

int gloo_mpi_scatter(gloo_context_t ctx_, void* sendbuffer_[], size_t sendcount,
		     MPI_Datatype sendtype, void* recvbuffer_, size_t recvcount,
		     MPI_Datatype recvtype, int root, int num_procs)
{
    auto ctx = *static_cast<std::shared_ptr<gloo::Context>*>(ctx_);
    try {
	gloo::ScatterOptions opts(ctx);
	opts.setRoot(root);
	if (sendcount) {
	    COLLOPS_GLOO_DTYPE_TO_BUFVEC(sendtype, sendbuffer, num_procs, opts.setInputs(sendbuffer, sendcount));
	}
	COLLOPS_GLOO_DTYPE_TO_BUFFER(recvtype, recvbuffer, opts.setOutput(recvbuffer, recvcount));
	gloo::scatter(opts);
        return 0;
    } catch (...) {
        return -1;
    }
}

int gloo_mpi_reduce(gloo_context_t ctx_, void* sendbuffer_, void* recvbuffer_, size_t count,
		    MPI_Datatype dtype, MPI_Op op, int root)
{
    auto ctx = *static_cast<std::shared_ptr<gloo::Context>*>(ctx_);
    try {
	gloo::ReduceOptions opts(ctx);
	opts.setRoot(root);
	COLLOPS_GLOO_DTYPE_TO_BUFFER(dtype, sendbuffer, opts.setInput(sendbuffer, count));
	COLLOPS_GLOO_DTYPE_TO_BUFFER(dtype, recvbuffer, opts.setOutput(recvbuffer, count));
	COLLOPS_GLOO_REDUCE_MAP_DTYPE_AND_OP(opts, dtype, op);
	gloo::reduce(opts);
        return 0;
    } catch (...) {
        return -1;
    }
}

int gloo_mpi_allreduce(gloo_context_t ctx_, void* sendbuffer_, void* recvbuffer_, size_t count,
		       MPI_Datatype dtype, MPI_Op op)
{
    auto ctx = *static_cast<std::shared_ptr<gloo::Context>*>(ctx_);
    try {
	gloo::AllreduceOptions opts(ctx);
	COLLOPS_GLOO_DTYPE_TO_BUFFER(dtype, sendbuffer, opts.setInput(sendbuffer, count));
	COLLOPS_GLOO_DTYPE_TO_BUFFER(dtype, recvbuffer, opts.setOutput(recvbuffer, count));
	COLLOPS_GLOO_REDUCE_MAP_DTYPE_AND_OP(opts, dtype, op);
	gloo::allreduce(opts);
        return 0;
    } catch (...) {
        return -1;
    }
}

}
