/*
 * Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
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

namespace fastertransformer {

template<typename T>
void invokeTopkSoftMax(const T* log_probs,
                       const T* bias,
                       const bool* finished,
                       float* cum_log_probs,
                       int* ids,
                       void* tmp_storage,
                       const int temp_storage_size,
                       const int batch_size,
                       const int beam_width,
                       const int vocab_size,
                       const int end_id,
                       const float diversity_rate,
                       cudaStream_t stream);

}  // namespace fastertransformer
