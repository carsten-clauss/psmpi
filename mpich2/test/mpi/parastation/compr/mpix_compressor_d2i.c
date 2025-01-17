/*
 * ParaStation
 *
 * Copyright (C) 2025 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


#ifdef MPIX_COMPRESSOR_PLUGIN
#define __WITHOUT_MAIN
#endif /* MPIX_COMPRESSOR_PLUGIN */


int compressor_req_init(void *buf, int partitions, MPI_Count count, MPI_Datatype dtype,
                        MPI_Info info, MPI_Aint * size, void *extra_state, void *extra_req_state)
{
    *size = count * sizeof(int);
    return MPI_SUCCESS;
}

int compressor_deflate(void *buf, int partition, MPI_Count count, MPI_Datatype dtype,
                       void *compr_buf, MPI_Aint * size, void *extra_req_state)
{
    for (int i = 0; i < count; i++) {
        double *_buf = (double *) buf;
        int *_compr_buf = (int *) compr_buf;

        _compr_buf[i] = (int) _buf[i];

        _compr_buf++;
        _buf++;
    }

    *size = count * sizeof(int);

    return MPI_SUCCESS;
}

int compressor_inflate(void *buf, int partition, MPI_Count count, MPI_Datatype dtype,
                       void *compr_buf, MPI_Aint * size, void *extra_req_state)
{
    for (int i = 0; i < count; i++) {
        double *_buf = (double *) buf;
        int *_compr_buf = (int *) compr_buf;

        _buf[i] = (double) _compr_buf[i];

        _compr_buf++;
        _buf++;
    }

    *size = count * sizeof(double);

    return MPI_SUCCESS;
}

int compressor_register(const char *name, MPI_Info info)
{
    MPIX_Register_compressor(name,
                             compressor_req_init,
                             MPIX_COMPRESSOR_REQ_FREE_FN_NULL,
                             compressor_deflate,
                             compressor_inflate,
                             MPIX_COMPRESSOR_DEREGISTER_FN_NULL, MPI_INFO_NULL, NULL);

    return MPI_SUCCESS;
}


#ifndef __WITHOUT_MAIN

#define partitions 2
#define total_count 32
double buffer[total_count];
const double const_double_value = 3.14159265;

int main(int argc, char *argv[])
{
    int errs = 0;
    MPI_Request request;
    MPI_Info info;
    int rank, size;

    MPI_Init(&argc, &argv);

    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    MPIX_Register_compressor("darexa-f",
                             compressor_req_init,
                             MPIX_COMPRESSOR_REQ_FREE_FN_NULL,
                             compressor_deflate, compressor_inflate,
                             MPIX_COMPRESSOR_DEREGISTER_FN_NULL, MPI_INFO_NULL, NULL);

    MPI_Info_create(&info);
    MPI_Info_set(info, "compressor", "darexa-f");
    MPI_Info_set(info, "compressor_plugin", "libmpix_compressor_d2i.so");

    if (rank == 0) {

        for (int j = 0; j < total_count; j++) {
            buffer[j] = const_double_value;
        }

        MPI_Psend_init(buffer, partitions, total_count / partitions, MPI_DOUBLE, 1, 0,
                       MPI_COMM_WORLD, info, &request);

        MPI_Start(&request);

        for (int i = 0; i < partitions; i++) {
            MPI_Pready(i, request);
        }

        MPI_Wait(&request, MPI_STATUS_IGNORE);

    } else if (rank == 1) {

        MPI_Precv_init(buffer, partitions, total_count / partitions, MPI_DOUBLE, 0, 0,
                       MPI_COMM_WORLD, info, &request);

        MPI_Start(&request);

        MPI_Wait(&request, MPI_STATUS_IGNORE);

        for (int j = 0; j < total_count; j++) {

            if (buffer[j] != (double) ((int) const_double_value)) {
                fprintf(stderr, "ERROR: Received %f at index %d but expected %f!\n", buffer[j], j,
                        const_double_value);
                errs++;
            }
        }
    }

    MPI_Request_free(&request);

    MPI_Info_free(&info);

    if (!errs) {
        if (!rank)
            printf(" No Errors\n");
        return 0;
    }
    return 1;
}

#endif /* !__WITHOUT_MAIN */
