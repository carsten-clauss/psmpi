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
#include <string.h>
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

int compressor_deflate_called_counter = 0;

static int compressor_deflate(void *buf, int partition, MPI_Count count, MPI_Datatype dtype,
                              void *compr_buf, MPI_Aint * size, void *extra_req_state)
{
    /* just copy the buffer (effectively a no-op) */
    memcpy(compr_buf, buf, *size);

    compressor_deflate_called_counter++;
    return MPI_SUCCESS;
}

int compressor_inflate_called_counter = 0;

static int compressor_inflate(void *buf, int partition, MPI_Count count, MPI_Datatype dtype,
                              void *compr_buf, MPI_Aint * size, void *extra_req_state)
{
    /* just copy the buffer (effectively a no-op) */
    memcpy(buf, compr_buf, *size);

    compressor_inflate_called_counter++;
    return MPI_SUCCESS;
}

#ifdef WITH_COMPRESSOR_PLUGIN
/* entry function for compressor plugin */
int compressor_register(const char *name, MPI_Info info)
{
    MPIX_Register_compressor(name,
                             MPIX_COMPRESSOR_REQ_INIT_FN_NULL,
                             MPIX_COMPRESSOR_REQ_FREE_FN_NULL,
                             compressor_deflate,
                             compressor_inflate,
                             MPIX_COMPRESSOR_DEREGISTER_FN_NULL, MPI_INFO_NULL, NULL);

    return MPI_SUCCESS;
}
#endif


#ifndef __WITHOUT_MAIN

#define partitions 2
#define total_count 32
double buffer[total_count];
const double const_double_value = 3.14159265;

/* This test program is to check the envisioned functionality of the proposed API extension
 * of the MPI interface by `MPIX_Register_compressor()`.
 *
 * For this, only two callbacks are registered for the compression/decompression routines,
 * which, however, do nothing more than just copying the payload (which is thus a no-op).
 * For all the other callbacks, the respective wildcards are used.
 *
 * The test is passed successfully if the received data equals exactly the sent data.
 * However, in addition it is checked whether the two callbacks were actually called.
 */

int main(int argc, char *argv[])
{
    int errs = 0;
    MPI_Request request;
    MPI_Info info;
    int rank, size;

    MTest_Init(&argc, &argv);

    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    MPIX_Register_compressor("darexa-f",
                             MPIX_COMPRESSOR_REQ_INIT_FN_NULL,
                             MPIX_COMPRESSOR_REQ_FREE_FN_NULL,
                             compressor_deflate, compressor_inflate,
                             MPIX_COMPRESSOR_DEREGISTER_FN_NULL, MPI_INFO_NULL, NULL);

    MPI_Info_create(&info);
    MPI_Info_set(info, "compressor", "darexa-f");

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

        if (compressor_deflate_called_counter != partitions) {
            fprintf(stderr,
                    "ERROR: Callback compressor_deflate() has been invoked %d times instead of %d.\n",
                    compressor_deflate_called_counter, partitions);
            errs++;
        }

    } else if (rank == 1) {

        MPI_Precv_init(buffer, partitions, total_count / partitions, MPI_DOUBLE, 0, 0,
                       MPI_COMM_WORLD, info, &request);

        MPI_Start(&request);

        MPI_Wait(&request, MPI_STATUS_IGNORE);

        if (compressor_inflate_called_counter != partitions) {
            fprintf(stderr,
                    "ERROR: Callback compressor_deflate() has been invoked %d times instead of %d.\n",
                    compressor_inflate_called_counter, partitions);
            errs++;
        }

        for (int j = 0; j < total_count; j++) {

            if (buffer[j] != const_double_value) {
                fprintf(stderr, "ERROR: Received %f at index %d but expected %f\n", buffer[j], j,
                        const_double_value);
                errs++;
            }
        }
    }

    MPI_Request_free(&request);

    MPI_Info_free(&info);

    MTest_Finalize(errs);
    return MTestReturnValue(errs);
}

#endif /* !__WITHOUT_MAIN */
