
#include <mpi.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

int compressor_init(void *buf, int partitions, MPI_Count count, MPI_Datatype dtype, MPI_Info info,
                    void *compr_buf, MPI_Aint * extent, void *extra_state)
{
    printf("MY darexa_compressor_init() FUNC! :-) %d\n", *((int *) extra_state));
    *extent = count * 8;
}

int compressor_deflate(void *buf, int partitions, MPI_Count count, MPI_Datatype dtype,
                       MPI_Info info, void *compr_buf, MPI_Aint * extent, void *extra_state)
{
    printf("MY darexa_compressor_deflate) FUNC! :-) %d\n", *((int *) extra_state));

    for (int i = 0; i < count; i++) {
        double *_buf = (double *) buf;
        int *_compr_buf = (int *) compr_buf;

        _compr_buf[i] = (int) _buf[i];

        _compr_buf++;
        _buf++;
    }

    *extent = count * sizeof(int);
}

int compressor_inflate(void *buf, int partitions, MPI_Count count, MPI_Datatype dtype,
                       MPI_Info info, void *compr_buf, MPI_Aint * extent, void *extra_state)
{
    printf("MY darexa_compressor_inflate) FUNC! :-) %d\n", *((int *) extra_state));

    for (int i = 0; i < count; i++) {
        double *_buf = (double *) buf;
        int *_compr_buf = (int *) compr_buf;

        _buf[i] = (double) _compr_buf[i];

        _compr_buf++;
        _buf++;
    }

    *extent = count * sizeof(double);
}


int global_state = 19;
void *compressor_register(const char *name, MPI_Info info)
{
    void *extra_state = &global_state;
    MPIX_Register_compressor(name, compressor_init, compressor_deflate, compressor_inflate,
                             /* info, */ extra_state);
    printf("REGISTER!\n");
    return extra_state;
}

#ifndef __WITHOUT_MAIN

#define total_count 4
double buffer[total_count];

int main()
{
    MPI_Request request;
    MPI_Info info;
    int rank, size;
    int flag = 0;

    int partitions = 1;
    int state = 42;

    MPI_Init(NULL, NULL);

    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

//    MPI_Register_datarep("darexa-f", NULL, NULL, NULL, NULL);
//    MPIX_Register_compressor("darexa-f", compressor_init, compressor_deflate, compressor_inflate, &state);

    MPI_Info_create(&info);
    MPI_Info_set(info, "compressor", "darexa-f");
    MPI_Info_set(info, "compressor_plugin", "libwrapper.so");

    if (rank == 0) {

        for (int j = 0; j < total_count; j++) {
            buffer[j] = 3.14159265;
            printf("(%d) S: %f\n", getpid(), buffer[j]);
        }

        MPI_Psend_init(buffer, partitions, total_count, MPI_DOUBLE, 1, 0, MPI_COMM_WORLD, info,
                       &request);

        MPI_Start(&request);

        for (int i = 0; i < partitions; i++) {
            MPI_Pready(i, request);
        }

        MPI_Wait(&request, MPI_STATUS_IGNORE);

    } else if (rank == 1) {

        MPI_Precv_init(buffer, partitions, total_count, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD, info,
                       &request);

        MPI_Start(&request);

//      while (!flag) {
//          MPI_Parrived(request, 1, &flag);
//      }

        MPI_Wait(&request, MPI_STATUS_IGNORE);

        for (int j = 0; j < total_count; j++) {
//          printf("(%d) R: %f\n", getpid(), buffer[j]);
            printf("(%d) R: %d\n", getpid(), ((int *) buffer)[j]);
        }

    }

    MPI_Request_free(&request);

    MPI_Info_free(&info);

    MPI_Finalize();
}

#endif
