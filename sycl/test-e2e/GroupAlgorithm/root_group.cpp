// Fails with opencl non-cpu, enable when fixed.
// XFAIL: (opencl && !cpu && !accelerator)
// RUN: %{build} -I . -o %t.out %if any-device-is-cuda %{ -Xsycl-target-backend=nvptx64-nvidia-cuda --cuda-gpu-arch=sm_70 %}
// RUN: %{run} %t.out

// Disabled temporarily while investigation into the failure is ongoing.
// UNSUPPORTED: gpu-intel-dg2

#include <cassert>
#include <cstdlib>
#include <type_traits>

#include <sycl/builtins.hpp>
#include <sycl/detail/core.hpp>
#include <sycl/ext/oneapi/experimental/root_group.hpp>
#include <sycl/group_barrier.hpp>
#include <sycl/kernel_bundle.hpp>

static constexpr int WorkGroupSize = 32;

void testFeatureMacro() {
  static_assert(SYCL_EXT_ONEAPI_ROOT_GROUP == 1,
                "SYCL_EXT_ONEAPI_ROOT_GROUP must have a value of 1");
}

void testQueriesAndProperties() {
  sycl::queue q;
  const auto bundle =
      sycl::get_kernel_bundle<sycl::bundle_state::executable>(q.get_context());
  const auto kernel = bundle.get_kernel<class QueryKernel>();
  const auto maxWGs = kernel.ext_oneapi_get_info<
      sycl::ext::oneapi::experimental::info::kernel_queue_specific::
          max_num_work_group_sync>(q);
  const auto wgRange = sycl::range{WorkGroupSize, 1, 1};
  const auto maxWGsWithLimits = kernel.ext_oneapi_get_info<
      sycl::ext::oneapi::experimental::info::kernel_queue_specific::
          max_num_work_group_sync>(q, wgRange, wgRange.size() * sizeof(int));
  const auto props = sycl::ext::oneapi::experimental::properties{
      sycl::ext::oneapi::experimental::use_root_sync};
  q.single_task<class QueryKernel>(props, []() {});

  static auto check_max_num_work_group_sync = [](auto Result) {
    static_assert(std::is_same_v<std::remove_cv_t<decltype(Result)>, size_t>,
                  "max_num_work_group_sync query must return size_t");
    assert(Result >= 1 && "max_num_work_group_sync query failed");
  };
  check_max_num_work_group_sync(maxWGs);
  check_max_num_work_group_sync(maxWGsWithLimits);
}

void testRootGroup() {
  sycl::queue q;
  const auto bundle =
      sycl::get_kernel_bundle<sycl::bundle_state::executable>(q.get_context());
  const auto kernel = bundle.get_kernel<class RootGroupKernel>();
  const auto maxWGs = kernel.ext_oneapi_get_info<
      sycl::ext::oneapi::experimental::info::kernel_queue_specific::
          max_num_work_group_sync>(q);
  const auto props = sycl::ext::oneapi::experimental::properties{
      sycl::ext::oneapi::experimental::use_root_sync};
  sycl::buffer<int> dataBuf{sycl::range{maxWGs * WorkGroupSize}};
  const auto range = sycl::nd_range<1>{maxWGs * WorkGroupSize, WorkGroupSize};
  q.submit([&](sycl::handler &h) {
    sycl::accessor data{dataBuf, h};
    h.parallel_for<
        class RootGroupKernel>(range, props, [=](sycl::nd_item<1> it) {
      volatile float X = 1.0f;
      volatile float Y = 1.0f;
      auto root = it.ext_oneapi_get_root_group();
      data[root.get_local_id()] = root.get_local_id();
      sycl::group_barrier(root);
      // Delay half of the workgroups with extra work to check that the barrier
      // synchronizes the whole device.
      if (it.get_group(0) % 2 == 0) {
        X += sycl::sin(X);
        Y += sycl::cos(Y);
      }
      root =
          sycl::ext::oneapi::experimental::this_work_item::get_root_group<1>();
      int sum = data[root.get_local_id()] +
                data[root.get_local_range() - root.get_local_id() - 1];
      sycl::group_barrier(root);
      data[root.get_local_id()] = sum;
    });
  });
  sycl::host_accessor data{dataBuf};
  const int workItemCount = static_cast<int>(range.get_global_range().size());
  for (int i = 0; i < workItemCount; i++) {
    assert(data[i] == (workItemCount - 1));
  }
}

void testRootGroupFunctions() {
  sycl::queue q;
  const auto bundle =
      sycl::get_kernel_bundle<sycl::bundle_state::executable>(q.get_context());
  const auto kernel = bundle.get_kernel<class RootGroupFunctionsKernel>();
  const auto maxWGs = kernel.ext_oneapi_get_info<
      sycl::ext::oneapi::experimental::info::kernel_queue_specific::
          max_num_work_group_sync>(q);
  const auto props = sycl::ext::oneapi::experimental::properties{
      sycl::ext::oneapi::experimental::use_root_sync};

  constexpr int testCount = 9;
  sycl::buffer<bool> testResultsBuf{sycl::range{testCount}};
  const auto range = sycl::nd_range<1>{maxWGs * WorkGroupSize, WorkGroupSize};
  q.submit([&](sycl::handler &h) {
    sycl::accessor testResults{testResultsBuf, h};
    h.parallel_for<class RootGroupFunctionsKernel>(
        range, props, [=](sycl::nd_item<1> it) {
          const auto root = it.ext_oneapi_get_root_group();
          if (root.leader() || root.get_local_id() == 3) {
            testResults[0] = root.get_group_id() == sycl::id<1>(0);
            testResults[1] = root.leader()
                                 ? root.get_local_id() == sycl::id<1>(0)
                                 : root.get_local_id() == sycl::id<1>(3);
            testResults[2] = root.get_group_range() == sycl::range<1>(1);
            testResults[3] = root.get_local_range() == it.get_global_range();
            testResults[4] =
                root.get_max_local_range() == root.get_local_range();
            testResults[5] = root.get_group_linear_id() == 0;
            testResults[6] =
                root.get_local_linear_id() == root.get_local_id().get(0);
            testResults[7] = root.get_group_linear_range() == 1;
            testResults[8] =
                root.get_local_linear_range() == root.get_local_range().size();
          }
        });
  });
  sycl::host_accessor testResults{testResultsBuf};
  for (int i = 0; i < testCount; i++) {
    assert(testResults[i]);
  }
}

int main() {
  testFeatureMacro();
  testQueriesAndProperties();
  testRootGroup();
  testRootGroupFunctions();
  return EXIT_SUCCESS;
}
