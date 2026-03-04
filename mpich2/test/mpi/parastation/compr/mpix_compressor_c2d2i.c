/*
 * ParaStation
 *
 * Copyright (C) 2025-2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "mpi.h"
#include <stdlib.h>
#include <assert.h>
#ifndef WITH_COMPRESSOR_PLUGIN
#include "mpitest.h"
#include "mpitestconf.h"
#include <stdio.h>
#endif

#ifdef WITH_COMPRESSOR_PLUGIN
/* In this plugin mode, only the callbacks are compiled and the main
 * function is omitted. This allows to generate a compressor plugin
 * with these callbacks that can then be used by a separate test
 * (see mpix_compressor_plg.c).
 */
#define __WITHOUT_MAIN
#endif /* WITH_COMPRESSOR_PLUGIN */

typedef struct {
    double r, i;
} complex_t;

void *compress_from_double_to_int(double *from, int *to, int count, int stride)
{
    int i, j;
    /* compress values by casting from double to int */
    for (i = 0, j = 0; i < count; i++, j += stride) {
        to[i] = (int) from[j];
    }
    return &(to[i]);
}

void *decompress_from_int_to_double(int *from, double *to, int count, int stride)
{
    int i, j;
    /* decompress values by casting back from int to double */
    for (i = 0, j = 0; j < count; i += stride, j++) {
        to[i] = (double) from[j];
    }
    return &(from[j]);
}


int compressor_req_init(void *buf, int *partitions, MPI_Count * count, MPI_Datatype * dtype,
                        MPI_Info info, void *temp_buf, MPI_Aint * temp_buf_size,
                        void *extra_state, void *extra_req_state)
{
    assert(*dtype == MPI_DOUBLE_COMPLEX);

    *(void **) temp_buf = MPI_BUFFER_AUTOMATIC;

    /* changing the data type and adjusting the number of elements accordingly */
    *dtype = MPI_DOUBLE;
    *count *= 2;

    return MPI_SUCCESS;
}

int compressor_deflate(void *buf, int partition, MPI_Count count, MPI_Datatype dtype,
                       void *compr_buf, MPI_Aint * size, void *extra_req_state)
{
    assert(dtype == MPI_DOUBLE);

    compress_from_double_to_int((double *) buf, (int *) compr_buf, count, 1);

    /* return size of compressed partition buffer */
    *size = count * sizeof(int);

    return MPI_SUCCESS;
}

int compressor_inflate(void *buf, int partition, MPI_Count count, MPI_Datatype dtype,
                       void *compr_buf, MPI_Aint * size, void *extra_req_state)
{
    assert(dtype == MPI_DOUBLE);

    decompress_from_int_to_double((int *) compr_buf, (double *) buf, count, 1);

    /* return size of decompressed partition buffer */
    *size = count * sizeof(double);

    return MPI_SUCCESS;
}

#ifdef WITH_COMPRESSOR_PLUGIN
/* entry function for compressor plugin */
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
#endif


#ifndef __WITHOUT_MAIN

#define partitions 2
#define total_count 10
complex_t buffer[total_count] = { };

const double const_double_value1 = 3.14159265;
const double const_double_value2 = 2.71828182;

/* This test program shows another example of how complex data can be handled in callback
 * functions for the proposed `MPIX_Register_compressor()`.
 *
 * In contrast to the `mpix_compressor_d2i.c` test, the routines for compression/decompression
 * handle here the real and imaginary parts as a single stream of `double` data.
 * For this, the `compressor_req_init` callback alters the given datatype from MPI_COMPLEX to
 * MPI_DOUBLE and just doubles the number of elements.
 *
 * The compression itself is then again based on a simple cast from double to integer (see also
 * the comments in `mpix_compressor_d2i.c`).
 */

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

#ifdef WITH_INFO_VIA_COMM
    MPI_Comm_set_info(MPI_COMM_WORLD, info);
    MPI_Info_free(&info);
#endif

    if (rank == 0) {

        for (int j = 0; j < total_count; j++) {
            buffer[j].r = const_double_value1;
            buffer[j].i = const_double_value2;
        }

        MPI_Psend_init(buffer, partitions, total_count / partitions, MPI_DOUBLE_COMPLEX, 1, 0,
                       MPI_COMM_WORLD, info, &request);

        MPI_Start(&request);

        for (int i = 0; i < partitions; i++) {
            MPI_Pready(i, request);
        }

        MPI_Wait(&request, MPI_STATUS_IGNORE);

    } else if (rank == 1) {

        MPI_Precv_init(buffer, partitions, total_count / partitions, MPI_DOUBLE_COMPLEX, 0, 0,
                       MPI_COMM_WORLD, info, &request);

        MPI_Start(&request);

        MPI_Wait(&request, MPI_STATUS_IGNORE);

        for (int j = 0; j < total_count; j++) {

            if ((buffer[j].r != (double) ((int) const_double_value1)) ||
                (buffer[j].i != (double) ((int) const_double_value2))) {
                fprintf(stderr, "ERROR: Received %f|%f at index %d but expected %f|%f\n",
                        buffer[j].r, buffer[j].i, j, (double) ((int) const_double_value1),
                        (double) ((int) const_double_value1));
                errs++;
            }
        }
    }

    MPI_Request_free(&request);

    if (info != MPI_INFO_NULL) {
        MPI_Info_free(&info);
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
