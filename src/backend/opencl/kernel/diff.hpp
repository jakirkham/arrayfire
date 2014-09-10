#pragma once

#include <af/defines.h>
#include <kernel_headers/diff.hpp>
#include <cl.hpp>
#include <platform.hpp>
#include <traits.hpp>
#include <sstream>
#include <string>
#include <mutex>
#include <dispatch.hpp>

typedef struct
{
    dim_type dim[4];
} dims_t;

using cl::Buffer;
using cl::Program;
using cl::Kernel;
using cl::make_kernel;
using cl::EnqueueArgs;
using cl::NDRange;
using std::string;

namespace opencl
{
    namespace kernel
    {
        static const dim_type TX = 16;
        static const dim_type TY = 16;

        template<typename T, unsigned dim, bool isDiff2>
        void diff(Buffer out, const Buffer in,
                  const unsigned oElem, const unsigned ondims,
                  const dim_type *odims, const dim_type *ostrides,
                  const unsigned iElem, const unsigned indims,
                  const dim_type *idims, const dim_type *istrides, dim_type offset)
        {
            static std::once_flag compileFlags[DeviceManager::MAX_DEVICES];
            static Program            difProgs[DeviceManager::MAX_DEVICES];
            static Kernel           difKernels[DeviceManager::MAX_DEVICES];

            int device = getActiveDeviceId();

            std::call_once( compileFlags[device], [device] () {
                        Program::Sources setSrc;
                        setSrc.emplace_back(diff_cl, diff_cl_len);

                        difProgs[device] = Program(getContext(), setSrc);

                        std::ostringstream options;
                        options << " -D T="        << dtype_traits<T>::getName()
                                << " -D DIM="      << dim
                                << " -D isDiff2="  << isDiff2
                                << " -D dim_type=" << dtype_traits<dim_type>::getName();
                        difProgs[device].build(options.str().c_str());

                        difKernels[device] = Kernel(difProgs[device], "diff_kernel");
                    });

            auto diffOp = make_kernel<Buffer, const Buffer,
                                      const unsigned, const dims_t,
                                      const dims_t, const dims_t,
                                      dim_type, const unsigned, const unsigned>
                                      (difKernels[device]);

            NDRange local(TX, TY, 1);
            if(dim == 0 && indims == 1) {
                local = NDRange(TX * TY, 1, 1);
            }

            unsigned blocksPerMatX = divup(odims[0], local[0]);
            unsigned blocksPerMatY = divup(odims[1], local[1]);
            NDRange global(local[0] * blocksPerMatX * odims[2],
                           local[1] * blocksPerMatY * odims[3],
                           1);

            dims_t _odims = {{odims[0], odims[1], odims[2], odims[3]}};
            dims_t _ostrides = {{ostrides[0], ostrides[1], ostrides[2], ostrides[3]}};
            dims_t _istrides = {{istrides[0], istrides[1], istrides[2], istrides[3]}};

            diffOp(EnqueueArgs(getQueue(), global, local),
                   out, in, oElem, _odims, _ostrides, _istrides, offset, blocksPerMatX, blocksPerMatY);

        }
}
}