/*
 * Copyright (c) 2019-2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cublasLt.h>
#include <cublas_v2.h>
#include <cuda_runtime.h>
#include <vector>
#include <fstream>
#include <iostream>
#include <string>
#ifdef SPARSITY_ENABLED
#include <cusparseLt.h>
#endif

namespace fastertransformer {

#define MAX_CONFIG_NUM 20
#define COL32_ 32
// workspace for cublas gemm : 32MB
#define CUBLAS_WORKSPACE_SIZE 33554432

/* **************************** type definition ***************************** */

enum CublasDataType
{
    FLOAT_DATATYPE = 0,
    HALF_DATATYPE = 1,
    INT8_DATATYPE = 2
};

enum class OperationType
{
    FP32,
    FP16
};

/* **************************** debug tools ********************************* */
static const char* _cudaGetErrorEnum(cudaError_t error)
{
    return cudaGetErrorString(error);
}

static const char* _cudaGetErrorEnum(cublasStatus_t error)
{
    switch (error) {
        case CUBLAS_STATUS_SUCCESS:
            return "CUBLAS_STATUS_SUCCESS";

        case CUBLAS_STATUS_NOT_INITIALIZED:
            return "CUBLAS_STATUS_NOT_INITIALIZED";

        case CUBLAS_STATUS_ALLOC_FAILED:
            return "CUBLAS_STATUS_ALLOC_FAILED";

        case CUBLAS_STATUS_INVALID_VALUE:
            return "CUBLAS_STATUS_INVALID_VALUE";

        case CUBLAS_STATUS_ARCH_MISMATCH:
            return "CUBLAS_STATUS_ARCH_MISMATCH";

        case CUBLAS_STATUS_MAPPING_ERROR:
            return "CUBLAS_STATUS_MAPPING_ERROR";

        case CUBLAS_STATUS_EXECUTION_FAILED:
            return "CUBLAS_STATUS_EXECUTION_FAILED";

        case CUBLAS_STATUS_INTERNAL_ERROR:
            return "CUBLAS_STATUS_INTERNAL_ERROR";

        case CUBLAS_STATUS_NOT_SUPPORTED:
            return "CUBLAS_STATUS_NOT_SUPPORTED";

        case CUBLAS_STATUS_LICENSE_ERROR:
            return "CUBLAS_STATUS_LICENSE_ERROR";
    }
    return "<unknown>";
}

template<typename T>
void check(T result, char const* const func, const char* const file, int const line)
{
    if (result) {
        throw std::runtime_error(std::string("[FT][ERROR] CUDA runtime error: ") + (_cudaGetErrorEnum(result)) + " "
                                 + file + ":" + std::to_string(line) + " \n");
    }
}

#define check_cuda_error(val) check((val), #val, __FILE__, __LINE__)
#define check_cuda_error_2(val, file, line) check((val), #val, file, line)

inline void syncAndCheck(const char* const file, int const line)
{
#ifndef NDEBUG
    cudaDeviceSynchronize();
    cudaError_t result = cudaGetLastError();
    if (result) {
        throw std::runtime_error(std::string("[FT][ERROR] CUDA runtime error: ") + (_cudaGetErrorEnum(result)) + " "
                                 + file + ":" + std::to_string(line) + " \n");
    }
#endif
}

#define sync_check_cuda_error() syncAndCheck(__FILE__, __LINE__)

template<typename T>
void print_to_file(const T* result, const int size, const char* file)
{
    cudaDeviceSynchronize();
    check_cuda_error(cudaGetLastError());
    printf("[INFO] file: %s \n", file);
    FILE* fd = fopen(file, "w");
    T* tmp = reinterpret_cast<T*>(malloc(sizeof(T) * size));
    check_cuda_error(cudaMemcpy(tmp, result, sizeof(T) * size, cudaMemcpyDeviceToHost));
    for (int i = 0; i < size; ++i) {
        float val;
        if (sizeof(T) == 2)
            val = (T)__half2float(tmp[i]);
        else
            val = (T)tmp[i];
        fprintf(fd, "%f\n", val);
    }
    free(tmp);
    fclose(fd);
    cudaDeviceSynchronize();
    check_cuda_error(cudaGetLastError());
}

template<typename T>
void print_to_file(const T* result,
                   const int size,
                   const char* file,
                   cudaStream_t stream,
                   std::ios::openmode open_mode = std::ios::out)
{
    cudaDeviceSynchronize();
    check_cuda_error(cudaGetLastError());
    printf("[INFO] file: %s with size %d.\n", file, size);
    std::ofstream outFile(file, open_mode);
    if (outFile) {
        T* tmp = new T[size];
        check_cuda_error(cudaMemcpyAsync(tmp, result, sizeof(T) * size, cudaMemcpyDeviceToHost, stream));
        for (int i = 0; i < size; ++i) {
            float val;
            if (sizeof(T) == 2)
                val = (T)__half2float(tmp[i]);
            else
                val = (T)tmp[i];
            outFile << val << std::endl;
        }
        delete[] tmp;
    }
    else {
        throw std::runtime_error(std::string("[FT][ERROR] Cannot open file: ") + file + "\n");
    }
    cudaDeviceSynchronize();
    check_cuda_error(cudaGetLastError());
}

template<typename T>
void print_abs_mean(const T* buf, unsigned int size, cudaStream_t stream, std::string name = "")
{
    if (buf == nullptr) {
        printf("[WARNING] It is an nullptr, skip!");
        return;
    }
    cudaDeviceSynchronize();
    check_cuda_error(cudaGetLastError());
    T* h_tmp = new T[size];
    cudaMemcpyAsync(h_tmp, buf, sizeof(T) * size, cudaMemcpyDeviceToHost, stream);
    cudaDeviceSynchronize();
    check_cuda_error(cudaGetLastError());
    double sum = 0.0f;
    uint64_t zero_count = 0;
    float max_val = -1e10;
    for (unsigned int i = 0; i < size; i++) {
        sum += abs((double)h_tmp[i]);
        if ((float)h_tmp[i] == 0.0f) {
            zero_count ++;
        }
        max_val = max_val > abs(float(h_tmp[i])) ? max_val : abs(float(h_tmp[i]));
    }
    printf("[INFO][FT] %s size: %u, abs mean: %f, abs sum: %f, abs max: %f", name.c_str(), size, sum / size, sum, max_val);
    std::cout << std::endl;
    delete [] h_tmp;
    cudaDeviceSynchronize();
    check_cuda_error(cudaGetLastError());
}

template<typename T>
void print_to_screen(const T* result, const int size)
{
    if (result == nullptr) {
        printf("[WARNING] It is an nullptr, skip!");
        return;
    }
    T* tmp = reinterpret_cast<T*>(malloc(sizeof(T) * size));
    check_cuda_error(cudaMemcpy(tmp, result, sizeof(T) * size, cudaMemcpyDeviceToHost));
    for (int i = 0; i < size; ++i) {
        printf("%d, %f\n", i, static_cast<float>(tmp[i]));
    }
    free(tmp);
}

template<typename T>
void check_max_val(const T* result, const int size)
{
    T* tmp = new T[size];
    cudaMemcpy(tmp, result, sizeof(T) * size, cudaMemcpyDeviceToHost);
    float max_val = -100000;
    for (int i = 0; i < size; i++) {
        float val = static_cast<float>(tmp[i]);
        if (val > max_val)
            max_val = val;
    }
    delete tmp;
    printf("[INFO][CUDA] addr %p max val: %f \n", result, max_val);
}

template<typename T>
void check_abs_mean_val(const T* result, const int size)
{
    T* tmp = new T[size];
    cudaMemcpy(tmp, result, sizeof(T) * size, cudaMemcpyDeviceToHost);
    float sum = 0.0f;
    for (int i = 0; i < size; i++) {
        sum += abs(static_cast<float>(tmp[i]));
    }
    delete tmp;
    printf("[INFO][CUDA] addr %p abs mean val: %f \n", result, sum / size);
}

#define PRINT_FUNC_NAME_()                                                                                             \
    do {                                                                                                               \
        std::cout << "[FT][CALL] " << __FUNCTION__ << " " << std::endl;                                                \
    } while (0)

inline void myAssert(bool result, const char* const file, int const line)
{
    if (result != true) {
        throw std::runtime_error(std::string("[FT][ERROR] Assertion fail: ") + +file + ":" + std::to_string(line)
                                 + " \n");
    }
}

#define FT_CHECK(val) myAssert(val, __FILE__, __LINE__)

#ifdef SPARSITY_ENABLED
#define CHECK_CUSPARSE(func)                                                                                                      \
{                                                                                                                                 \
    cusparseStatus_t status = (func);                                                                                             \
    if (status != CUSPARSE_STATUS_SUCCESS) {                                                                                      \
      throw std::runtime_error(std::string("[FT][ERROR] CUSPARSE API failed at line ") + std::to_string(__LINE__) +               \
            " in file " + __FILE__ + ": " + cusparseGetErrorString(status) + " " + std::to_string(status));                       \
    }                                                                                                                             \
}
#endif

/*************Time Handling**************/
// class CudaTimer {
// private:
//     cudaEvent_t event_start_;
//     cudaEvent_t event_stop_;
//     cudaStream_t stream_;

// public:
//     explicit CudaTimer(cudaStream_t stream = 0)
//     {
//         stream_ = stream;
//     }
//     void start()
//     {
//         check_cuda_error(cudaEventCreate(&event_start_));
//         check_cuda_error(cudaEventCreate(&event_stop_));
//         check_cuda_error(cudaEventRecord(event_start_, stream_));
//     }
//     float stop()
//     {
//         float time;
//         check_cuda_error(cudaEventRecord(event_stop_, stream_));
//         check_cuda_error(cudaEventSynchronize(event_stop_));
//         check_cuda_error(cudaEventElapsedTime(&time, event_start_, event_stop_));
//         check_cuda_error(cudaEventDestroy(event_start_));
//         check_cuda_error(cudaEventDestroy(event_stop_));
//         return time;
//     }
//     ~CudaTimer() {}
// };

// static double diffTime(timeval start, timeval end)
// {
//     return (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) * 0.001;
// }

/* ***************************** common utils ****************************** */

inline void print_mem_usage()
{
    size_t free_bytes, total_bytes;
    check_cuda_error(cudaMemGetInfo(&free_bytes, &total_bytes));
    float free = static_cast<float>(free_bytes) / 1024.0f / 1024.0f / 1024.0f;
    float total = static_cast<float>(total_bytes) / 1024.0f / 1024.0f / 1024.0f;
    printf("after allocation, free %.2f GB total %.2f GB\n", free, total);
}

inline int getSMVersion()
{
    int device{-1};
    check_cuda_error(cudaGetDevice(&device));
    cudaDeviceProp props;
    check_cuda_error(cudaGetDeviceProperties(&props, device));
    return props.major * 10 + props.minor;
}

inline std::string getDeviceName()
{
    int device{-1};
    check_cuda_error(cudaGetDevice(&device));
    cudaDeviceProp props;
    check_cuda_error(cudaGetDeviceProperties(&props, device));
    return std::string(props.name);
}

inline int div_up(int a, int n)
{
    return (a + n - 1) / n;
}

inline cudaError_t getSetDevice(int i_device, int* o_device = NULL)
{
    int current_dev_id = 0;
    cudaError_t err = cudaSuccess;

    if (o_device != NULL) {
        err = cudaGetDevice(&current_dev_id);
        if (err != cudaSuccess)
            return err;
        if (current_dev_id == i_device) {
            *o_device = i_device;
        }
        else {
            err = cudaSetDevice(i_device);
            if (err != cudaSuccess) {
                return err;
            }
            *o_device = current_dev_id;
        }
    }
    else {
        err = cudaSetDevice(i_device);
        if (err != cudaSuccess) {
            return err;
        }
    }

    return cudaSuccess;
}

inline int getDevice()
{
    int current_dev_id = 0;
    check_cuda_error(cudaGetDevice(&current_dev_id));
    return current_dev_id;
}

inline int getDeviceCount()
{
    int count = 0;
    check_cuda_error(cudaGetDeviceCount(&count));
    return count;
}

/* ************************** end of common utils ************************** */
}  // namespace fastertransformer
