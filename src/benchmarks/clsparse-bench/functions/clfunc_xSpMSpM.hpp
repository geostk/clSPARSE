/* ************************************************************************
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * ************************************************************************ */

#pragma once
#ifndef CLSPARSE_BENCHMARK_SpM_SpM_HXX
#define CLSPARSE_BENCHMARK_SpM_SpM_HXX

#include "clSPARSE.h"
#include "clfunc_common.hpp"

// Dummy Code to be deleted later
clsparseStatus clsparseDcsrSpGemm(const clsparseCsrMatrix* sparseMatA, const clsparseCsrMatrix* sparseMatB, clsparseCsrMatrix* sparseMatC, const clsparseControl control)
{
    std::cout << "sparseMat A dimensions = \n";
    std::cout << " rows = " << sparseMatA->num_rows << std::endl;
    std::cout << " cols = " << sparseMatA->num_cols << std::endl;
    std::cout << "nnz = " << sparseMatA->num_nonzeros << std::endl;

    std::cout << "sparseMat B dimensions = \n";
    std::cout << " rows = " << sparseMatB->num_rows << std::endl;
    std::cout << " cols = " << sparseMatB->num_cols << std::endl;
    std::cout << "nnz = " << sparseMatB->num_nonzeros << std::endl;

    return clsparseSuccess;
}//
// Dummy code ends

template <typename T>
class xSpMSpM : public clsparseFunc {
public:
    xSpMSpM(PFCLSPARSETIMER sparseGetTimer, size_t profileCount, cl_device_type devType, cl_bool keep_explicit_zeroes = true) : clsparseFunc(devType, CL_QUEUE_PROFILING_ENABLE), gpuTimer(nullptr), cpuTimer(nullptr)
    {
        //	Create and initialize our timer class, if the external timer shared library loaded
        if (sparseGetTimer)
        {
            gpuTimer = sparseGetTimer(CLSPARSE_GPU);
            gpuTimer->Reserve(1, profileCount);
            gpuTimer->setNormalize(true);

            cpuTimer = sparseGetTimer(CLSPARSE_CPU);
            cpuTimer->Reserve(1, profileCount);
            cpuTimer->setNormalize(true);

            gpuTimerID = gpuTimer->getUniqueID("GPU xSpMSpM", 0);
            cpuTimerID = cpuTimer->getUniqueID("CPU xSpMSpM", 0);
        }
        clsparseEnableAsync(control, false);
        explicit_zeroes = keep_explicit_zeroes;
    }

    ~xSpMSpM() {}

    void call_func()
    {
        if (gpuTimer && cpuTimer)
        {
            gpuTimer->Start(gpuTimerID);
            cpuTimer->Start(cpuTimerID);

            xSpMSpM_Function(false);

            cpuTimer->Stop(cpuTimerID);
            gpuTimer->Stop(gpuTimerID);
        }
        else
        {
            xSpMSpM_Function(false);
        }
    }//

    double gflops()
    {
        return 0.0;
    }

    std::string gflops_formula()
    {
        return "N/A";
    }

    double bandwidth() // Need to modify this later **********
    {
        //  Assuming that accesses to the vector always hit in the cache after the first access
        //  There are NNZ integers in the cols[ ] array
        //  You access each integer value in row_delimiters[ ] once.
        //  There are NNZ float_types in the vals[ ] array
        //  You read num_cols floats from the vector, afterwards they cache perfectly.
        //  Finally, you write num_rows floats out to DRAM at the end of the kernel.
        return (sizeof(clsparseIdx_t)*(csrMtx.num_nonzeros + csrMtx.num_rows) + sizeof(T) * (csrMtx.num_nonzeros + csrMtx.num_cols + csrMtx.num_rows)) / time_in_ns();
    } // end of function

    std::string bandwidth_formula()
    {
        return "GiB/s";
    }// end of function

    void setup_buffer(double pAlpha, double pBeta, const std::string& path)
    {
        sparseFile = path;

        alpha = static_cast<T>(pAlpha);
        beta = static_cast<T>(pBeta);

        // Read sparse data from file and construct a COO matrix from it
        clsparseIdx_t nnz, row, col;
        clsparseStatus fileError = clsparseHeaderfromFile(&nnz, &row, &col, sparseFile.c_str());
        if (fileError != clsparseSuccess)
            throw clsparse::io_exception("Could not read matrix market header from disk");

        // Now initialize a CSR matrix from the COO matrix
        clsparseInitCsrMatrix(&csrMtx);
        csrMtx.num_nonzeros = nnz;
        csrMtx.num_rows = row;
        csrMtx.num_cols = col;
        //clsparseCsrMetaSize(&csrMtx, control);

        cl_int status;
        csrMtx.values = ::clCreateBuffer(ctx, CL_MEM_READ_ONLY,
            csrMtx.num_nonzeros * sizeof(T), NULL, &status);
        CLSPARSE_V(status, "::clCreateBuffer csrMtx.values");

        csrMtx.col_indices = ::clCreateBuffer(ctx, CL_MEM_READ_ONLY,
            csrMtx.num_nonzeros * sizeof(clsparseIdx_t), NULL, &status);
        CLSPARSE_V(status, "::clCreateBuffer csrMtx.col_indices");

        csrMtx.row_pointer = ::clCreateBuffer(ctx, CL_MEM_READ_ONLY,
            (csrMtx.num_rows + 1) * sizeof(clsparseIdx_t), NULL, &status);
        CLSPARSE_V(status, "::clCreateBuffer csrMtx.row_pointer");
#if 0
        csrMtx.rowBlocks = ::clCreateBuffer(ctx, CL_MEM_READ_ONLY,
            csrMtx.rowBlockSize * sizeof(cl_ulong), NULL, &status);
        CLSPARSE_V(status, "::clCreateBuffer csrMtx.rowBlocks");
#endif

        if (typeid(T) == typeid(float))
            fileError = clsparseSCsrMatrixfromFile( &csrMtx, sparseFile.c_str(), control, explicit_zeroes );
        else if (typeid(T) == typeid(double))
            fileError = clsparseDCsrMatrixfromFile( &csrMtx, sparseFile.c_str(), control, explicit_zeroes );
        else
            fileError = clsparseInvalidType;

        if (fileError != clsparseSuccess)
            throw clsparse::io_exception("Could not read matrix market data from disk");

        // Initilize the output CSR Matrix
        clsparseInitCsrMatrix(&csrMtxC);

        // Initialize the scalar alpha & beta parameters
        clsparseInitScalar(&a);
        a.value = ::clCreateBuffer(ctx, CL_MEM_READ_ONLY,
                             1 * sizeof(T), NULL, &status);
        CLSPARSE_V(status, "::clCreateBuffer a.value");

        clsparseInitScalar(&b);
        b.value = ::clCreateBuffer(ctx, CL_MEM_READ_ONLY,
            1 * sizeof(T), NULL, &status);
        CLSPARSE_V(status, "::clCreateBuffer b.value");

        //std::cout << "Flops = " << xSpMSpM_Getflopcount() << std::endl;
        flopCnt = xSpMSpM_Getflopcount();

    }// end of function

    void initialize_cpu_buffer()
    {
    }

    void initialize_gpu_buffer()
    {
        CLSPARSE_V(::clEnqueueFillBuffer(queue, a.value, &alpha, sizeof(T), 0,
            sizeof(T) * 1, 0, NULL, NULL), "::clEnqueueFillBuffer alpha.value");

        CLSPARSE_V(::clEnqueueFillBuffer(queue, b.value, &beta, sizeof(T), 0,
            sizeof(T) * 1, 0, NULL, NULL), "::clEnqueueFillBuffer beta.value");

    }// end of function

    void reset_gpu_write_buffer()
    {
        // Every call to clsparseScsrSpGemm() allocates memory to csrMtxC, therefore freeing the memory
        CLSPARSE_V(::clReleaseMemObject(csrMtxC.values), "clReleaseMemObject csrMtxC.values");
        CLSPARSE_V(::clReleaseMemObject(csrMtxC.col_indices), "clReleaseMemObject csrMtxC.col_indices");
        CLSPARSE_V(::clReleaseMemObject(csrMtxC.row_pointer), "clReleaseMemObject csrMtxC.row_pointer");

        // Initilize the output CSR Matrix
        clsparseInitCsrMatrix(&csrMtxC);

    }// end of function

    void read_gpu_buffer()
    {
    }

    void cleanup()
    {
        if (gpuTimer && cpuTimer)
        {
            std::cout << "clSPARSE matrix: " << sparseFile << std::endl;
            cpuTimer->pruneOutliers(3.0);
            cpuTimer->Print(flopCnt, "GFlop/s");
            cpuTimer->Reset();

            gpuTimer->pruneOutliers(3.0);
            gpuTimer->Print(flopCnt, "GFlop/s");
            gpuTimer->Reset();
        }

        //this is necessary since we are running a iteration of tests and calculate the average time. (in client.cpp)
        //need to do this before we eventually hit the destructor
        CLSPARSE_V(::clReleaseMemObject(csrMtx.values),     "clReleaseMemObject csrMtx.values");
        CLSPARSE_V(::clReleaseMemObject(csrMtx.col_indices), "clReleaseMemObject csrMtx.col_indices");
        CLSPARSE_V(::clReleaseMemObject(csrMtx.row_pointer), "clReleaseMemObject csrMtx.row_pointer");
        //CLSPARSE_V(::clReleaseMemObject(csrMtx.rowBlocks),  "clReleaseMemObject csrMtx.rowBlocks");

        if (csrMtxC.values != nullptr)
        CLSPARSE_V(::clReleaseMemObject(csrMtxC.values),     "clReleaseMemObject csrMtxC.values");

        if (csrMtxC.col_indices != nullptr)
        CLSPARSE_V(::clReleaseMemObject(csrMtxC.col_indices), "clReleaseMemObject csrMtxC.col_indices");

        if (csrMtxC.row_pointer != nullptr)
        CLSPARSE_V(::clReleaseMemObject(csrMtxC.row_pointer), "clReleaseMemObject csrMtxC.row_pointer");
        //CLSPARSE_V(::clReleaseMemObject(csrMtxC.rowBlocks),  "clReleaseMemObject csrMtxC.rowBlocks");

        CLSPARSE_V(::clReleaseMemObject(a.value), "clReleaseMemObject alpha.value");
        CLSPARSE_V(::clReleaseMemObject(b.value), "clReleaseMemObject beta.value");
    }

private:
    void xSpMSpM_Function(bool flush);
    clsparseIdx_t xSpMSpM_Getflopcount(void)
    {
        // C = A * B
        // But here C = A* A, the A & B matrices are same
        clsparseIdx_t nnzA = csrMtx.num_nonzeros;
        clsparseIdx_t Browptrlen = csrMtx.num_rows + 1; // Number of row offsets

        std::vector<clsparseIdx_t> colIdxA(nnzA, 0);
        std::vector<clsparseIdx_t> rowptrB(Browptrlen, 0);

        cl_int run_status = 0;

        run_status = clEnqueueReadBuffer(queue, 
                                          csrMtx.col_indices, 
                                          CL_TRUE, 0, 
                                          nnzA*sizeof(clsparseIdx_t),
                                          colIdxA.data(), 0, nullptr, nullptr);
        CLSPARSE_V(run_status, "Reading col_indices from GPU failed");

        // copy rowptrs

        run_status = clEnqueueReadBuffer(queue,
                                            csrMtx.row_pointer,
                                            CL_TRUE, 0,
                                            Browptrlen*sizeof(clsparseIdx_t),
                                            rowptrB.data(), 0, nullptr, nullptr);

        CLSPARSE_V(run_status, "Reading row offsets from GPU failed");

        clsparseIdx_t flop = 0;
        for (clsparseIdx_t i = 0; i < nnzA; i++)
        {
            clsparseIdx_t colIdx = colIdxA[i]; // Get colIdx of A
            flop += rowptrB[colIdx + 1] - rowptrB[colIdx]; // nnz in 'colIdx'th row of B
        }

        flop = 2 * flop; // Two operations - Multiply & Add

        return flop;
    }// end of function

    //  Timers we want to keep
    clsparseTimer* gpuTimer;
    clsparseTimer* cpuTimer;
    size_t gpuTimerID;
    size_t cpuTimerID;

    std::string sparseFile;

    //device values
    clsparseCsrMatrix csrMtx;   // input matrix
    clsparseCsrMatrix csrMtxC;  // Output CSR MAtrix
    clsparseScalar a;
    clsparseScalar b;

    // host values
    T alpha;
    T beta;
    clsparseIdx_t flopCnt; // Indicates total number of floating point operations
    cl_bool explicit_zeroes;
    //  OpenCL state
    //cl_command_queue_properties cqProp;
}; // class xSpMSpM

template<> void
xSpMSpM<float>::xSpMSpM_Function(bool flush)
{
    clsparseStatus status = clsparseScsrSpGemm(&csrMtx, &csrMtx, &csrMtxC, control);

    if (flush)
        clFinish(queue);
}// end of single precision function


template<> void
xSpMSpM<double>::xSpMSpM_Function(bool flush)
{
    clsparseStatus status = clsparseDcsrSpGemm(&csrMtx, &csrMtx, &csrMtxC, control);

    if (flush)
        clFinish(queue);
}

#endif // CLSPARSE_BENCHMARK_SpM_SpM_HXX
