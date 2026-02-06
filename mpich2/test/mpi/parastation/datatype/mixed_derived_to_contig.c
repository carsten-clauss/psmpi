/*
 * ParaStation
 *
 * Copyright (C) 2026 ParTec AG, Munich
 *
 * This file may be distributed under the terms of the Q Public License
 * as defined in the file LICENSE.QPL included in the packaging of this
 * file.
 */

#include "mixed_datatype_test.h"

int main(int argc, char **argv)
{
    int errs = 0;

    MTest_Init(&argc, &argv);

    int rank, nprocs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    /* Init global vars */
    init_datatype(0, nprocs);

#if defined(ALLGATHER)
    sbuffer = malloc(factor * count * stride * basic_size);
    rbuffer = malloc((factor * count * blocklength + 1) * basic_size * nprocs);

    for (int i = 0; i < factor * count * stride; i++) {
        sbuffer[i] = (type_basic_t) i + rank;
    }

    test_handle(MPI_Allgather
                (sbuffer, factor, type_derived, rbuffer, 1, type_contig, MPI_COMM_WORLD));

    for (int i = 0; i < nprocs; i++) {
        for (int j = 0; j < factor * count * blocklength; j++) {
            int pos = j + factor * count * blocklength * i + 1;
            test_assert(rbuffer[pos] == 1.0 * j + (j / blocklength) * (stride - blocklength) + i,
                        "mismatch at position %d", pos);
        }
    }
#elif defined(ALLGATHERV)
    sbuffer = malloc(factor * count * stride * basic_size);
    rbuffer = malloc((factor * count * blocklength * nprocs) * basic_size * nprocs);

    for (int i = 0; i < nprocs; i++) {
        rcounts[i] = 1;
        rdispls[i] = i + i;
    }

    for (int i = 0; i < factor * count * stride; i++) {
        sbuffer[i] = (type_basic_t) i + rank;
    }

    test_handle(MPI_Allgatherv
                (sbuffer, factor, type_derived, rbuffer, rcounts, rdispls, type_contig,
                 MPI_COMM_WORLD));

    for (int i = 0; i < nprocs; i++) {
        for (int j = 0; j < factor * count * blocklength; j++) {
            int pos = j + factor * count * blocklength * i * 2 + 1;
            test_assert(rbuffer[pos] == 1.0 * j + (j / blocklength) * (stride - blocklength) + i,
                        "mismatch at position %d", pos);
        }
    }
#elif defined(ALLTOALL)
    sbuffer = malloc(factor * count * stride * basic_size * nprocs);
    rbuffer = malloc((factor * count * blocklength + 1) * basic_size * nprocs);

    for (int i = 0; i < nprocs; i++) {
        for (int j = 0; j < factor * count * stride; j++) {
            int pos = i * (factor * count * stride) + j;
            sbuffer[pos] = (type_basic_t) j + rank;
        }
    }

    test_handle(MPI_Alltoall
                (sbuffer, factor, type_derived, rbuffer, 1, type_contig, MPI_COMM_WORLD));

    for (int i = 0; i < nprocs; i++) {
        for (int j = 0; j < factor * count * blocklength; j++) {
            int pos = j + factor * count * blocklength * i + 1;
            test_assert(rbuffer[pos] == 1.0 * j + (j / blocklength) * (stride - blocklength) + i,
                        "mismatch at position %d", pos);
        }
    }
#elif defined(ALLTOALLV)
    sbuffer = malloc(factor * count * stride * basic_size * nprocs);
    rbuffer = malloc((factor * count * blocklength * nprocs) * basic_size * nprocs);

    for (int i = 0; i < nprocs; i++) {
        scounts[i] = factor;
        sdispls[i] = i * factor;
        rcounts[i] = 1;
        rdispls[i] = i + i;
    }

    for (int i = 0; i < nprocs; i++) {
        for (int j = 0; j < factor * count * stride; j++) {
            int pos = i * (factor * count * stride) + j;
            sbuffer[pos] = (type_basic_t) j + rank;
        }
    }

    test_handle(MPI_Alltoallv
                (sbuffer, scounts, sdispls, type_derived, rbuffer, rcounts, rdispls,
                 type_contig, MPI_COMM_WORLD));

    for (int i = 0; i < nprocs; i++) {
        for (int j = 0; j < factor * count * blocklength; j++) {
            int pos = j + factor * count * blocklength * i * 2 + 1;
            test_assert(rbuffer[pos] == 1.0 * j + (j / blocklength) * (stride - blocklength) + i,
                        "mismatch at position %d", pos);
        }
    }
#elif defined(BCAST)
    if (rank == root) {

        sbuffer = malloc(factor * count * stride * basic_size);

        for (int i = 0; i < factor * count * stride; i++) {
            sbuffer[i] = (type_basic_t) i;
        }

        MPI_Bcast(sbuffer, factor, type_derived, root, MPI_COMM_WORLD);

    } else {

        rbuffer = malloc((factor * count * blocklength + 1) * basic_size);

        test_handle(MPI_Bcast(rbuffer, 1, type_contig, root, MPI_COMM_WORLD));

        for (int i = 0; i < factor * count * blocklength; i++) {
            test_assert(rbuffer[i + 1] == 1.0 * i + (i / blocklength) * (stride - blocklength),
                        "mismatch at position %d", i + 1);
        }
    }
#elif defined(GATHER)
    if (rank == root) {

        sbuffer = malloc(factor * count * stride * basic_size);
        rbuffer = malloc((factor * count * blocklength + 1) * basic_size * nprocs);

        for (int i = 0; i < factor * count * stride; i++) {
            sbuffer[i] = (type_basic_t) i + rank;
        }

        test_handle(MPI_Gather
                    (sbuffer, factor, type_derived, rbuffer, 1, type_contig, root, MPI_COMM_WORLD));

        for (int i = 0; i < nprocs; i++) {
            for (int j = 0; j < factor * count * blocklength; j++) {
                int pos = j + factor * count * blocklength * i + 1;
                test_assert(rbuffer[pos] ==
                            1.0 * j + (j / blocklength) * (stride - blocklength) + i,
                            "mismatch at position %d", pos);
            }
        }

    } else {

        sbuffer = malloc(factor * count * stride * basic_size);

        for (int i = 0; i < factor * count * stride; i++) {
            sbuffer[i] = (type_basic_t) i + rank;
        }

        MPI_Gather(sbuffer, factor, type_derived, NULL, 0, MPI_DATATYPE_NULL, root, MPI_COMM_WORLD);
    }
#elif defined(GATHERV)
    if (rank == root) {

        sbuffer = malloc(factor * count * stride * basic_size);
        rbuffer = malloc((factor * count * blocklength * nprocs) * basic_size * nprocs);

        for (int i = 0; i < nprocs; i++) {
            rcounts[i] = 1;
            rdispls[i] = i + i;
        }

        for (int i = 0; i < factor * count * stride; i++) {
            sbuffer[i] = (type_basic_t) i + rank;
        }

        test_handle(MPI_Gatherv
                    (sbuffer, factor, type_derived, rbuffer, rcounts, rdispls, type_contig,
                     root, MPI_COMM_WORLD));

        for (int i = 0; i < nprocs; i++) {
            for (int j = 0; j < factor * count * blocklength; j++) {
                int pos = j + factor * count * blocklength * i * 2 + 1;
                test_assert(rbuffer[pos] ==
                            1.0 * j + (j / blocklength) * (stride - blocklength) + i,
                            "mismatch at position %d", pos);
            }
        }

    } else {

        sbuffer = malloc(factor * count * stride * basic_size);

        for (int i = 0; i < factor * count * stride; i++) {
            sbuffer[i] = (type_basic_t) i + rank;
        }

        MPI_Gatherv(sbuffer, factor, type_derived, NULL, NULL, NULL, MPI_DATATYPE_NULL, root,
                    MPI_COMM_WORLD);
    }
#elif defined(SCATTER)
    if (rank == root) {

        sbuffer = malloc(factor * count * stride * basic_size * nprocs);
        rbuffer = malloc(factor * count * blocklength * basic_size);

        for (int i = 0; i < nprocs; i++) {
            for (int j = 0; j < factor * count * stride; j++) {
                int pos = i * (factor * count * stride) + j;
                sbuffer[pos] = (type_basic_t) j + i;
            }
        }

        MPI_Scatter(sbuffer, factor, type_derived, rbuffer, factor * count * blocklength,
                    type_basic, root, MPI_COMM_WORLD);

    } else {

        rbuffer = malloc((factor * count * blocklength + 1) * basic_size);

        test_handle(MPI_Scatter
                    (NULL, 0, MPI_DATATYPE_NULL, rbuffer, 1, type_contig, root, MPI_COMM_WORLD));

        for (int i = 0; i < factor * count * blocklength; i++) {
            test_assert(rbuffer[i + 1] ==
                        1.0 * i + (i / blocklength) * (stride - blocklength) + rank,
                        "mismatch at position %d", i + 1);
        }
    }
#elif defined(SCATTERV)
    if (rank == root) {

        sbuffer = malloc(factor * count * stride * basic_size * nprocs);
        rbuffer = malloc(factor * count * blocklength * basic_size);

        for (int i = 0; i < nprocs; i++) {
            scounts[i] = factor;
            sdispls[i] = i * factor;
        }

        for (int i = 0; i < nprocs; i++) {
            for (int j = 0; j < factor * count * stride; j++) {
                int pos = i * (factor * count * stride) + j;
                sbuffer[pos] = (type_basic_t) j + i;
            }
        }

        MPI_Scatterv(sbuffer, scounts, sdispls, type_derived, rbuffer,
                     factor * count * blocklength, type_basic, root, MPI_COMM_WORLD);

    } else {

        rbuffer = malloc((factor * count * blocklength + 1) * basic_size);

        test_handle(MPI_Scatterv
                    (NULL, NULL, NULL, MPI_DATATYPE_NULL, rbuffer, 1, type_contig, root,
                     MPI_COMM_WORLD));

        for (int i = 0; i < factor * count * blocklength; i++) {
            test_assert(rbuffer[i + 1] ==
                        1.0 * i + (i / blocklength) * (stride - blocklength) + rank,
                        "mismatch at position %d", i + 1);
        }
    }
#else
#error "No collop selected!"
#endif

    /* Clean up global vars */
    cleanup_datatype();

    MTest_Finalize(errs);
    return MTestReturnValue(errs);
}
