#ifdef __cplusplus
extern "C" {
#endif

    typedef void *gloo_context_t;

    gloo_context_t gloo_mpi_create_context(MPI_Comm comm);
    void gloo_mpi_free_context(gloo_context_t ctx);

    int gloo_mpi_barrier(gloo_context_t ctx_);
    int gloo_mpi_broadcast(gloo_context_t ctx, void *buffer, size_t count, MPI_Datatype dtype,
                           int root);
    int gloo_mpi_gather(gloo_context_t ctx_, void *sendbuffer_, size_t sendcount,
                        MPI_Datatype sendtype, void *recvbuffer_, size_t recvcount,
                        MPI_Datatype recvtype, int root);
    int gloo_mpi_allgather(gloo_context_t ctx_, void *sendbuffer_, size_t sendcount,
                           MPI_Datatype sendtype, void *recvbuffer_, size_t recvcount,
                           MPI_Datatype recvtype);
    int gloo_mpi_scatter(gloo_context_t ctx_, void *sendbuffer_[], size_t sendcount,
                         MPI_Datatype sendtype, void *recvbuffer_, size_t recvcount,
                         MPI_Datatype recvtype, int root, int num_procs);
    int gloo_mpi_reduce(gloo_context_t ctx_, void *sendbuffer_, void *recvbuffer_, size_t count,
                        MPI_Datatype dtype, MPI_Op op, int root);
    int gloo_mpi_allreduce(gloo_context_t ctx_, void *sendbuffer_, void *recvbuffer_, size_t count,
                           MPI_Datatype dtype, MPI_Op op);

#ifdef __cplusplus
}
#endif
