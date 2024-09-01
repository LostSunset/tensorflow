/* Copyright 2024 The OpenXLA Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef XLA_BACKENDS_CPU_RUNTIME_CONVOLUTION_THUNK_INTERNAL_H_
#define XLA_BACKENDS_CPU_RUNTIME_CONVOLUTION_THUNK_INTERNAL_H_

#define EIGEN_USE_THREADS

#include <functional>
#include <optional>

#include "Eigen/Core"
#include "unsupported/Eigen/CXX11/Tensor"
#include "xla/tsl/framework/convolution/eigen_spatial_convolutions.h"  // IWYU pragma: keep

namespace xla::cpu::internal {

// TODO(ezhulenev): Make internal implementation a private static method of
// ConvolutionThunk (for consistency with DotThunk). Today we keep it as a free
// function to use it in the legacy XLA CPU runtime.

template <typename EigenDevice, typename ScalarType>
void EigenConv2D(const EigenDevice& device, ScalarType* out, ScalarType* lhs,
                 ScalarType* rhs, Eigen::Index input_batch,
                 Eigen::Index input_x, Eigen::Index input_y,
                 Eigen::Index input_channels, Eigen::Index kernel_x,
                 Eigen::Index kernel_y, Eigen::Index kernel_channels,
                 Eigen::Index kernel_filters, Eigen::Index output_x,
                 Eigen::Index output_y, Eigen::Index x_stride,
                 Eigen::Index y_stride, Eigen::Index padding_x_before,
                 Eigen::Index padding_x_after, Eigen::Index padding_y_before,
                 Eigen::Index padding_y_after, Eigen::Index lhs_x_dilation,
                 Eigen::Index lhs_y_dilation, Eigen::Index rhs_x_dilation,
                 Eigen::Index rhs_y_dilation, Eigen::Index feature_group_count,
                 std::optional<std::function<void()>> done_callback) {
  const Eigen::TensorMap<Eigen::Tensor<const ScalarType, 4, Eigen::RowMajor>,
                         Eigen::Aligned>
      input(lhs, input_batch, input_x, input_y, input_channels);

  const Eigen::TensorMap<Eigen::Tensor<const ScalarType, 4, Eigen::RowMajor>,
                         Eigen::Aligned>
      kernel(rhs, kernel_x, kernel_y, kernel_channels, kernel_filters);

  Eigen::TensorMap<Eigen::Tensor<ScalarType, 4, Eigen::RowMajor>,
                   Eigen::Aligned>
      output(out, input_batch, output_x, output_y, kernel_filters);

  Eigen::array<Eigen::IndexPair<Eigen::Index>, 1> contract_dims;
  contract_dims[0] = Eigen::IndexPair<Eigen::Index>(1, 0);

  Eigen::DSizes<Eigen::Index, 5> input_reshaped_dims;
  input_reshaped_dims[0] = input_batch;
  input_reshaped_dims[1] = input_x;
  input_reshaped_dims[2] = input_y;
  input_reshaped_dims[3] = feature_group_count;
  input_reshaped_dims[4] = input_channels / feature_group_count;

  Eigen::DSizes<Eigen::Index, 5> output_reshaped_dims;
  output_reshaped_dims[0] = input_batch;
  output_reshaped_dims[1] = output_x;
  output_reshaped_dims[2] = output_y;
  output_reshaped_dims[3] = feature_group_count;
  output_reshaped_dims[4] = kernel_filters / feature_group_count;

  // Molds the output of the patch extraction code into a 2d tensor:
  // - the first dimension (dims[0]): the patch values to be multiplied with the
  //   kernels
  // - the second dimension (dims[1]): everything else
  Eigen::DSizes<Eigen::Index, 2> pre_contract_dims;
  pre_contract_dims[0] = output_y * output_x * input_batch;
  pre_contract_dims[1] = kernel_channels * kernel_y * kernel_x;

  // Molds the output of the contraction into the shape expected by the user:
  Eigen::DSizes<Eigen::Index, 4> post_contract_dims;
  post_contract_dims[0] = input_batch;
  post_contract_dims[1] = output_x;
  post_contract_dims[2] = output_y;
  post_contract_dims[3] = kernel_filters / feature_group_count;

  Eigen::DSizes<Eigen::Index, 3> kernel_dims;
  kernel_dims[0] = kernel_channels * kernel_y * kernel_x;
  kernel_dims[1] = feature_group_count;
  kernel_dims[2] = kernel_filters / feature_group_count;

  for (Eigen::Index i = 0; i < feature_group_count; ++i) {
    // The row and column dimensions must be flipped when passed to Eigen.
    auto convolved =
        input.reshape(input_reshaped_dims)
            .chip(i, 3)
            .extract_image_patches(
                kernel_y, kernel_x, y_stride, x_stride, rhs_y_dilation,
                rhs_x_dilation, lhs_y_dilation, lhs_x_dilation,
                padding_y_before, padding_y_after, padding_x_before,
                padding_x_after, static_cast<ScalarType>(0.0f))
            .reshape(pre_contract_dims)
            .contract(kernel.reshape(kernel_dims).chip(i, 1), contract_dims)
            .reshape(post_contract_dims);
    auto output_reshaped = output.reshape(output_reshaped_dims).chip(i, 3);
    if (done_callback.has_value()) {
      output_reshaped.device(device, done_callback.value()) = convolved;
    } else {
      output_reshaped.device(device) = convolved;
    }
  }
}

template <typename EigenDevice, typename ScalarType>
void EigenConv3D(const EigenDevice& device, ScalarType* out, ScalarType* lhs,
                 ScalarType* rhs, Eigen::Index input_batch,
                 Eigen::Index input_x, Eigen::Index input_y,
                 Eigen::Index input_z, Eigen::Index input_channels,
                 Eigen::Index kernel_x, Eigen::Index kernel_y,
                 Eigen::Index kernel_z, Eigen::Index kernel_channels,
                 Eigen::Index kernel_filters, Eigen::Index output_x,
                 Eigen::Index output_y, Eigen::Index output_z,
                 Eigen::Index x_stride, Eigen::Index y_stride,
                 Eigen::Index z_stride, Eigen::Index padding_x_before,
                 Eigen::Index padding_x_after, Eigen::Index padding_y_before,
                 Eigen::Index padding_y_after, Eigen::Index padding_z_before,
                 Eigen::Index padding_z_after, Eigen::Index lhs_x_dilation,
                 Eigen::Index lhs_y_dilation, Eigen::Index lhs_z_dilation,
                 Eigen::Index rhs_x_dilation, Eigen::Index rhs_y_dilation,
                 Eigen::Index rhs_z_dilation, Eigen::Index feature_group_count,
                 std::optional<std::function<void()>> done_callback) {
  using ConstTType =
      Eigen::TensorMap<Eigen::Tensor<const ScalarType, 5, Eigen::RowMajor>,
                       Eigen::Aligned>;
  const ConstTType input(lhs, input_batch, input_x, input_y, input_z,
                         input_channels);

  const ConstTType kernel(rhs, kernel_x, kernel_y, kernel_z, kernel_channels,
                          kernel_filters);

  Eigen::TensorMap<Eigen::Tensor<ScalarType, 5, Eigen::RowMajor>,
                   Eigen::Aligned>
      output(out, input_batch, output_x, output_y, output_z, kernel_filters);

  Eigen::DSizes<Eigen::Index, 6> input_reshaped_dims;
  input_reshaped_dims[0] = input_batch;
  input_reshaped_dims[1] = input_x;
  input_reshaped_dims[2] = input_y;
  input_reshaped_dims[3] = input_z;
  input_reshaped_dims[4] = feature_group_count;
  input_reshaped_dims[5] = input_channels / feature_group_count;

  Eigen::DSizes<Eigen::Index, 6> output_reshaped_dims;
  output_reshaped_dims[0] = input_batch;
  output_reshaped_dims[1] = output_x;
  output_reshaped_dims[2] = output_y;
  output_reshaped_dims[3] = output_z;
  output_reshaped_dims[4] = feature_group_count;
  output_reshaped_dims[5] = kernel_filters / feature_group_count;

  Eigen::array<Eigen::IndexPair<Eigen::Index>, 1> contract_dims;
  contract_dims[0] = Eigen::IndexPair<Eigen::Index>(1, 0);

  // Molds the output of the patch extraction code into a 2d tensor:
  // - the first dimension (dims[0]): the patch values to be multiplied with the
  //   kernels
  // - the second dimension (dims[1]): everything else
  Eigen::DSizes<Eigen::Index, 2> pre_contract_dims;
  pre_contract_dims[0] = output_x * output_y * output_z * input_batch;
  pre_contract_dims[1] = kernel_channels * kernel_x * kernel_y * kernel_z;

  // Molds the output of the contraction into the shape expected by the user:
  Eigen::DSizes<Eigen::Index, 5> post_contract_dims;
  post_contract_dims[0] = input_batch;
  post_contract_dims[1] = output_x;
  post_contract_dims[2] = output_y;
  post_contract_dims[3] = output_z;
  post_contract_dims[4] = kernel_filters / feature_group_count;

  Eigen::DSizes<Eigen::Index, 3> kernel_dims;
  kernel_dims[0] = kernel_channels * kernel_x * kernel_y * kernel_z;
  kernel_dims[1] = feature_group_count;
  kernel_dims[2] = kernel_filters / feature_group_count;

  for (Eigen::Index i = 0; i < feature_group_count; ++i) {
    // The dimension order must be flipped when passed to Eigen.
    auto input_chip = input.reshape(input_reshaped_dims).chip(i, 4);
    auto patches =
        Eigen::TensorVolumePatchOp<Eigen::Dynamic, Eigen::Dynamic,
                                   Eigen::Dynamic, decltype(input_chip)>(
            input_chip, kernel_z, kernel_y, kernel_x, z_stride, y_stride,
            x_stride, rhs_z_dilation, rhs_y_dilation, rhs_x_dilation,
            lhs_z_dilation, lhs_y_dilation, lhs_x_dilation, padding_z_before,
            padding_z_after, padding_y_before, padding_y_after,
            padding_x_before, padding_x_after, static_cast<ScalarType>(0.0f));

    auto convolved =
        patches.reshape(pre_contract_dims)
            .contract(kernel.reshape(kernel_dims).chip(i, 1), contract_dims)
            .reshape(post_contract_dims);

    auto output_reshaped = output.reshape(output_reshaped_dims).chip(i, 4);
    if (done_callback.has_value()) {
      output_reshaped.device(device, done_callback.value()) = convolved;
    } else {
      output_reshaped.device(device) = convolved;
    }
  }
}

// Extern Conv2D template for all supported devices and data types.
#define CONV2D_EXTERN_TEMPLATE(DEVICE, SCALAR_TYPE)                        \
  extern template void EigenConv2D<DEVICE, SCALAR_TYPE>(                   \
      const DEVICE& device, SCALAR_TYPE* out, SCALAR_TYPE* lhs,            \
      SCALAR_TYPE* rhs, Eigen::Index input_batch, Eigen::Index input_x,    \
      Eigen::Index input_y, Eigen::Index input_channels,                   \
      Eigen::Index kernel_x, Eigen::Index kernel_y,                        \
      Eigen::Index kernel_channels, Eigen::Index kernel_filters,           \
      Eigen::Index output_x, Eigen::Index output_y, Eigen::Index x_stride, \
      Eigen::Index y_stride, Eigen::Index padding_x_before,                \
      Eigen::Index padding_x_after, Eigen::Index padding_y_before,         \
      Eigen::Index padding_y_after, Eigen::Index lhs_x_dilation,           \
      Eigen::Index lhs_y_dilation, Eigen::Index rhs_x_dilation,            \
      Eigen::Index rhs_y_dilation, Eigen::Index feature_group_count,       \
      std::optional<std::function<void()>> done_callback)

CONV2D_EXTERN_TEMPLATE(Eigen::DefaultDevice, Eigen::half);
CONV2D_EXTERN_TEMPLATE(Eigen::DefaultDevice, float);
CONV2D_EXTERN_TEMPLATE(Eigen::ThreadPoolDevice, Eigen::half);
CONV2D_EXTERN_TEMPLATE(Eigen::ThreadPoolDevice, float);

#undef CONV2D_EXTERN_TEMPLATE

// Extern Conv3D template for all supported devices and data types.
#define CONV3D_EXTERN_TEMPLATE(DEVICE, SCALAR_TYPE)                            \
  extern template void EigenConv3D<DEVICE, SCALAR_TYPE>(                       \
      const DEVICE& device, SCALAR_TYPE* out, SCALAR_TYPE* lhs,                \
      SCALAR_TYPE* rhs, Eigen::Index input_batch, Eigen::Index input_x,        \
      Eigen::Index input_y, Eigen::Index input_z, Eigen::Index input_channels, \
      Eigen::Index kernel_x, Eigen::Index kernel_y, Eigen::Index kernel_z,     \
      Eigen::Index kernel_channels, Eigen::Index kernel_filters,               \
      Eigen::Index output_x, Eigen::Index output_y, Eigen::Index output_z,     \
      Eigen::Index x_stride, Eigen::Index y_stride, Eigen::Index z_stride,     \
      Eigen::Index padding_x_before, Eigen::Index padding_x_after,             \
      Eigen::Index padding_y_before, Eigen::Index padding_y_after,             \
      Eigen::Index padding_z_before, Eigen::Index padding_z_after,             \
      Eigen::Index lhs_x_dilation, Eigen::Index lhs_y_dilation,                \
      Eigen::Index lhs_z_dilation, Eigen::Index rhs_x_dilation,                \
      Eigen::Index rhs_y_dilation, Eigen::Index rhs_z_dilation,                \
      Eigen::Index feature_group_count,                                        \
      std::optional<std::function<void()>> done_callback)

CONV3D_EXTERN_TEMPLATE(Eigen::DefaultDevice, Eigen::half);
CONV3D_EXTERN_TEMPLATE(Eigen::DefaultDevice, float);
CONV3D_EXTERN_TEMPLATE(Eigen::ThreadPoolDevice, Eigen::half);
CONV3D_EXTERN_TEMPLATE(Eigen::ThreadPoolDevice, float);

#undef CONV3D_EXTERN_TEMPLATE

}  // namespace xla::cpu::internal

#define CONV2D_INSTANTIATE_TEMPLATE(DEVICE, SCALAR_TYPE)                   \
  template void xla::cpu::internal::EigenConv2D<DEVICE, SCALAR_TYPE>(      \
      const DEVICE& device, SCALAR_TYPE* out, SCALAR_TYPE* lhs,            \
      SCALAR_TYPE* rhs, Eigen::Index input_batch, Eigen::Index input_x,    \
      Eigen::Index input_y, Eigen::Index input_channels,                   \
      Eigen::Index kernel_x, Eigen::Index kernel_y,                        \
      Eigen::Index kernel_channels, Eigen::Index kernel_filters,           \
      Eigen::Index output_x, Eigen::Index output_y, Eigen::Index x_stride, \
      Eigen::Index y_stride, Eigen::Index padding_x_before,                \
      Eigen::Index padding_x_after, Eigen::Index padding_y_before,         \
      Eigen::Index padding_y_after, Eigen::Index lhs_x_dilation,           \
      Eigen::Index lhs_y_dilation, Eigen::Index rhs_x_dilation,            \
      Eigen::Index rhs_y_dilation, Eigen::Index feature_group_count,       \
      std::optional<std::function<void()>> done_callback)

#define CONV3D_INSTANTIATE_TEMPLATE(DEVICE, SCALAR_TYPE)                       \
  template void xla::cpu::internal::EigenConv3D<DEVICE, SCALAR_TYPE>(          \
      const DEVICE& device, SCALAR_TYPE* out, SCALAR_TYPE* lhs,                \
      SCALAR_TYPE* rhs, Eigen::Index input_batch, Eigen::Index input_x,        \
      Eigen::Index input_y, Eigen::Index input_z, Eigen::Index input_channels, \
      Eigen::Index kernel_x, Eigen::Index kernel_y, Eigen::Index kernel_z,     \
      Eigen::Index kernel_channels, Eigen::Index kernel_filters,               \
      Eigen::Index output_x, Eigen::Index output_y, Eigen::Index output_z,     \
      Eigen::Index x_stride, Eigen::Index y_stride, Eigen::Index z_stride,     \
      Eigen::Index padding_x_before, Eigen::Index padding_x_after,             \
      Eigen::Index padding_y_before, Eigen::Index padding_y_after,             \
      Eigen::Index padding_z_before, Eigen::Index padding_z_after,             \
      Eigen::Index lhs_x_dilation, Eigen::Index lhs_y_dilation,                \
      Eigen::Index lhs_z_dilation, Eigen::Index rhs_x_dilation,                \
      Eigen::Index rhs_y_dilation, Eigen::Index rhs_z_dilation,                \
      Eigen::Index feature_group_count,                                        \
      std::optional<std::function<void()>> done_callback)

#endif  // XLA_BACKENDS_CPU_RUNTIME_CONVOLUTION_THUNK_INTERNAL_H_
