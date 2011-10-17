#ifndef bit_tree_test_manager_cxx_
#define bit_tree_test_manager_cxx_
//:
// \file
#include "bit_tree_test_manager.h"
#include <testlib/testlib_root_dir.h>
#include <boxm/ocl/boxm_ocl_utils.h>
#include <bocl/bocl_utils.h>
#include <vcl_cstdio.h>
#include <vcl_string.h>


bool bit_tree_test_manager::init_arrays()
{
  //set up buffers
  bit_tree_ = (cl_uchar*) boxm_ocl_utils::alloc_aligned(16,sizeof(cl_uchar),16);
  output_   = (cl_int*)   boxm_ocl_utils::alloc_aligned(16,sizeof(cl_float4),16);
  bbox_output_ = (cl_float*)  boxm_ocl_utils::alloc_aligned(16,sizeof(cl_float4),16);
  bit_lookup_ = (cl_uchar*) boxm_ocl_utils::alloc_aligned(256,sizeof(cl_uchar),16);
  unsigned char bits[] = { 0,   1,   1,   2,   1,   2,   2,   3,   1,   2,   2,   3,   2,   3,   3,   4,
                           1,   2,   2,   3,   2,   3,   3,   4,   2,   3,   3,   4,   3,   4,   4,   5 ,
                           1,   2,   2,   3,   2,   3,   3,   4,   2,   3,   3,   4,   3,   4,   4,   5  ,
                           2,   3,   3,   4,   3,   4,   4,   5,   3,   4,   4,   5,   4,   5,   5,   6  ,
                           1,   2,   2,   3,   2,   3,   3,   4,   2,   3,   3,   4,   3,   4,   4,   5  ,
                           2,   3,   3,   4,   3,   4,   4,   5,   3,   4,   4,   5,   4,   5,   5,   6  ,
                           2,   3,   3,   4,   3,   4,   4,   5,   3,   4,   4,   5,   4,   5,   5,   6  ,
                           3,   4,   4,   5,   4,   5,   5,   6,   4,   5,   5,   6,   5,   6,   6,   7  ,
                           1,   2,   2,   3,   2,   3,   3,   4,   2,   3,   3,   4,   3,   4,   4,   5  ,
                           2,   3,   3,   4,   3,   4,   4,   5,   3,   4,   4,   5,   4,   5,   5,   6  ,
                           2,   3,   3,   4,   3,   4,   4,   5,   3,   4,   4,   5,   4,   5,   5,   6  ,
                           3,   4,   4,   5,   4,   5,   5,   6,   4,   5,   5,   6,   5,   6,   6,   7  ,
                           2,   3,   3,   4,   3,   4,   4,   5,   3,   4,   4,   5,   4,   5,   5,   6  ,
                           3,   4,   4,   5,   4,   5,   5,   6,   4,   5,   5,   6,   5,   6,   6,   7  ,
                           3,   4,   4,   5,   4,   5,   5,   6,   4,   5,   5,   6,   5,   6,   6,   7  ,
                           4,   5,   5,   6,   5,   6,   6,   7,   5,   6,   6,   7,   6,   7,   7,   8 };
  for (int i=0; i<256; i++) bit_lookup_[i] = bits[i];
  return true;
}

//: init manager - initializes all opencl stuff (command queue, program, kernels... )
bool bit_tree_test_manager::init_manager()
{
  //create command queue
  bool good = this->create_command_queue();
  //build program from source
  good = this->build_test_program();
  //create and set kernels using program
  good = good && this->set_test_kernels();
  //prepare cl_mem buffers
  good = good && this->set_buffers();
  return good;
}

//: Builds the test program from the two cl files
bool bit_tree_test_manager::build_test_program()
{
  vcl_string source_dir = testlib_root_dir()+"/contrib/brl/bseg/boxm/ocl/cl/";
  bool locc = this->load_kernel_source(source_dir+"loc_code_library_functions.cl");
  bool cell = this->append_process_kernels(source_dir+"cell_utils.cl");
  bool bitr = this->append_process_kernels(source_dir+"bit_tree_library_functions.cl");
  bool test = this->append_process_kernels(source_dir+"../tests/bit_tree_test_kernels.cl");

  if (!bitr || !test || !locc || !cell) {
    vcl_cerr << "Error: bit_tree_test_manager : failed to load kernel source (helper functions)\n";
    return false;
  }
  return this->build_kernel_program(program_)==SDK_SUCCESS;
}

// sets the vector of test kernels to be executed
bool bit_tree_test_manager::set_test_kernels()
{
  cl_int status = CL_SUCCESS;
  int CHECK_SUCCESS = 1;
  if (!this->release_kernels())
    return false;

  //5 kernels are:

  // test_loc_code
  cl_kernel kernel = clCreateKernel(program_,"test_loc_code", &status);
  if (check_val(status,CL_SUCCESS,error_to_string(status))!=CHECK_SUCCESS)
    return false;
  kernels_.push_back(kernel);

  //test_bit_at
  kernel = clCreateKernel(program_,"test_bit_at",&status);
  if (check_val(status,CL_SUCCESS,error_to_string(status))!=CHECK_SUCCESS)
    return false;
  kernels_.push_back(kernel);

  //test traverse
  kernel = clCreateKernel(program_,"test_traverse",&status);
  if (check_val(status,CL_SUCCESS,error_to_string(status))!=CHECK_SUCCESS)
    return false;
  kernels_.push_back(kernel);

  // test_traverse_force
  kernel = clCreateKernel(program_, "test_traverse_force", &status);
  if (check_val(status,CL_SUCCESS,error_to_string(status))!=CHECK_SUCCESS)
    return false;
  kernels_.push_back(kernel);

  //// test_traverse_force_local
  //kernel = clCreateKernel(program_, "test_traverse_force_local", &status);
  //if (check_val(status,CL_SUCCESS,error_to_string(status))!=CHECK_SUCCESS)
    //return false;
  //kernels_.push_back(kernel);


  return true;
}


bool bit_tree_test_manager::set_kernel_args(unsigned pass)
{
  //3 args in the first four kernels,:
  //__global uchar16* cells,
  //__global float2* cell_data,
  //__global int4* results
  int CHECK_SUCCESS = 1;
  cl_int status = SDK_SUCCESS;
  int i=0;

  status = clSetKernelArg(kernels_[pass], i++,
                          sizeof(cl_mem), (void *) &bit_tree_buf_);
  if (check_val(status, CL_SUCCESS, "clSetKernelArg failed. (cell_data_buf_)")!=CHECK_SUCCESS)
    return false;

  status = clSetKernelArg(kernels_[pass], i++,
                          sizeof(cl_mem), (void *) &output_buf_);
  if (check_val(status, CL_SUCCESS, "clSetKernelArg failed. (data array )")!=CHECK_SUCCESS)
    return false;

  //if you're doing the fourth pass, add local buffer too
  if (pass==1 || pass==2 || pass==3) {
    status = clSetKernelArg(kernels_[pass], i++, 16*sizeof(cl_uchar), 0);
    if (check_val(status, CL_SUCCESS, "clSetKernelArg failed. (local tree)")!=CHECK_SUCCESS)
      return false;
    status = clSetKernelArg(kernels_[pass], i++, sizeof(cl_mem), (void *)& bit_lookup_buf_);
    if (check_val(status, CL_SUCCESS, "clSetKernelArg failed. (bit lookup tree)")!=CHECK_SUCCESS)
      return false;
  }
  else if (pass == 4) {
    status = clSetKernelArg(kernels_[pass], i++, 73*sizeof(cl_int4), 0);
    if (check_val(status, CL_SUCCESS, "clSetKernelArg failed. (data array size)")!=CHECK_SUCCESS)
      return false;
  }
  return true;
}

bool bit_tree_test_manager::set_buffers()
{
  cl_int status = 0;

  //bit tree buffer
  bit_tree_buf_ = clCreateBuffer(this->context_,CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                                 sizeof(cl_uchar16),bit_tree_,&status);
  if (!check_val(status,CL_SUCCESS,"clCreateBuffer (bit_tree_buf) failed."))
    return false;

  //bit lookup buffer
  bit_lookup_buf_ = clCreateBuffer(this->context_, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                   256*sizeof(cl_uchar),bit_lookup_,&status);
  if (!check_val(status,CL_SUCCESS,"clCreateBuffer (bit_lookup) failed."))
    return false;

  //output_buf_
  output_buf_ = clCreateBuffer(this->context_,CL_MEM_READ_WRITE,
                               16*sizeof(cl_int4), NULL , &status);
  if (!check_val(status,CL_SUCCESS,"clCreateBuffer (output_buf) failed."))
    return false;

  //output_buf_
  bbox_output_buf_ = clCreateBuffer(this->context_,CL_MEM_READ_WRITE,
                                    16*sizeof(cl_float4), NULL , &status);
  if (!check_val(status,CL_SUCCESS,"clCreateBuffer (bbox_output_buf) failed."))
    return false;
  return true;
}

bool bit_tree_test_manager::release_buffers()
{
  //bit tree
  cl_int status = clReleaseMemObject(bit_tree_buf_);
  if (!check_val(status,CL_SUCCESS,"clReleaseMemObject (bit_tree_buf) failed."))
    return false;

  //data buffer
  status = clReleaseMemObject(bit_lookup_buf_);
  if (!check_val(status,CL_SUCCESS,"clReleaseMemObject (bit_lookup) failed."))
    return false;

  //output buffer
  status = clReleaseMemObject(output_buf_);
  if (!check_val(status,CL_SUCCESS,"clReleaseMemObject (output_Buf) failed."))
    return false;

  return true;
}

//---------------------------------------------------------------------
// Run Test Kernel functions -
////---------------------------------------------------------------------

bool bit_tree_test_manager::run_test(unsigned pass)
{
  int CHECK_SUCCESS = 1;
  cl_int status = SDK_SUCCESS;
  this->set_kernel_args(pass);

  // check the local memeory
  cl_ulong used_local_memory;
  status = clGetKernelWorkGroupInfo(kernels_[pass],
                                    this->devices()[0],
                                    CL_KERNEL_LOCAL_MEM_SIZE,
                                    sizeof(cl_ulong),
                                    &used_local_memory,
                                    NULL);
  if (check_val(status,CL_SUCCESS,"clGetKernelWorkGroupInfo CL_KERNEL_LOCAL_MEM_SIZE failed.")!=CHECK_SUCCESS)
    return false;

  // determine the work group size
  cl_ulong kernel_work_group_size;
  status = clGetKernelWorkGroupInfo(kernels_[pass],this->devices()[0],CL_KERNEL_WORK_GROUP_SIZE,
                                    sizeof(cl_ulong),&kernel_work_group_size,NULL);
  if (check_val(status,CL_SUCCESS,"clGetKernelWorkGroupInfo CL_KERNEL_WORK_GROUP_SIZE, failed.")!=CHECK_SUCCESS)
    return false;
  if (used_local_memory > this->total_local_memory())
  {
    vcl_cout << "Unsupported: Insufficient local memory on device.\n";
    return false;
  }

  vcl_size_t globalThreads[]= {1};
  vcl_size_t localThreads[] = {1};

  //run it
  cl_event ceEvent;
  status = clEnqueueNDRangeKernel(command_queue_, kernels_[pass], 1,
                                  NULL,globalThreads,localThreads,0,
                                  NULL,&ceEvent);
  if (check_val(status,CL_SUCCESS,"clEnqueueNDRangeKernel failed. "+error_to_string(status))!=CHECK_SUCCESS)
    return false;
  status = clFinish(command_queue_);
  if (check_val(status,CL_SUCCESS,"clFinish failed."+error_to_string(status))!=CHECK_SUCCESS)
    return false;

  //run timing
  cl_ulong tstart,tend;
  status = clGetEventProfilingInfo(ceEvent,CL_PROFILING_COMMAND_END,sizeof(cl_ulong), &tend,0);
  status = clGetEventProfilingInfo(ceEvent,CL_PROFILING_COMMAND_START,sizeof(cl_ulong),&tstart,0);
  gpu_time_ = (double)1.0e-6 * (tend - tstart); // convert nanoseconds to milliseconds

  return true;
}

cl_int* bit_tree_test_manager::get_output()
{
  cl_event events[1];
  cl_int status = 0;

  // Enqueue readBuffers
  status = clEnqueueReadBuffer(command_queue_, output_buf_, CL_TRUE,
                               0, 16*sizeof(cl_int4),
                               output_, 0, NULL, &events[0]);

  if (!check_val(status, CL_SUCCESS, "clEnqueueBuffer (output)failed."))
    return 0;

  // Wait for the read buffer to finish execution
  status = clWaitForEvents(1, &events[0]);
  if (!check_val(status, CL_SUCCESS, "clWaitForEvents failed."))
    return 0;
  status = clReleaseEvent(events[0]);
  if (!check_val(status,CL_SUCCESS,"clReleaseEvent failed."))
    return 0;

  return output_;
}

cl_float* bit_tree_test_manager::get_bbox_output()
{
  cl_event events[1];
  cl_int status = 0;

  // Enqueue readBuffers
  status = clEnqueueReadBuffer(command_queue_, bbox_output_buf_, CL_TRUE,
                               0, 16*sizeof(cl_float4),
                               bbox_output_, 0, NULL, &events[0]);
  if (!check_val(status, CL_SUCCESS, "clEnqueueBuffer (bbox_output)failed."))
    return 0;

  // Wait for the read buffer to finish execution
  status = clWaitForEvents(1, &events[0]);
  if (!check_val(status, CL_SUCCESS, "clWaitForEvents failed."))
    return 0;
  status = clReleaseEvent(events[0]);
  if (!check_val(status,CL_SUCCESS,"clReleaseEvent failed."))
    return 0;

  return bbox_output_;
}

//---------------------------------------------------------------------
// Below are standard OpenCL Functions (that may belong in BOCL_manger
//---------------------------------------------------------------------

/*******************************************
 * build_kernel_program - builds kernel program
 * from source (a vcl string)
 *******************************************/
int bit_tree_test_manager::build_kernel_program(cl_program & program)
{
  cl_int status = CL_SUCCESS;
  vcl_size_t sourceSize[] = { this->prog_.size() };
  if (!sourceSize[0]) return SDK_FAILURE;
  if (program) {
    status = clReleaseProgram(program);
    program = 0;
    if (!check_val(status,
      CL_SUCCESS,
      "clReleaseProgram failed."))
      return SDK_FAILURE;
  }
  const char * source = this->prog_.c_str();

  program = clCreateProgramWithSource(this->context_,
                                      1,
                                      &source,
                                      sourceSize,
                                      &status);
  if (!check_val(status,
                       CL_SUCCESS,
                       "clCreateProgramWithSource failed."))
    return SDK_FAILURE;

  // create a cl program executable for all the devices specified
  status = clBuildProgram(program,
                          1,
                          this->devices(),
                          "",
                          NULL,
                          NULL);
  if (!check_val(status, CL_SUCCESS, error_to_string(status)))
  {
    vcl_size_t len;
    char buffer[2048];
    clGetProgramBuildInfo(program,
                          this->devices()[0],
                          CL_PROGRAM_BUILD_LOG,
                          sizeof(buffer),
                          buffer,
                          &len);
    vcl_printf("%s\n", buffer);
    return SDK_FAILURE;
  }
  else
    return SDK_SUCCESS;
}

bool bit_tree_test_manager::release_kernels()
{
  cl_int status = CL_SUCCESS;
  int CHECK_SUCCESS = 1;
  for (unsigned i = 0; i<kernels_.size(); ++i) {
    if (kernels_[i]) {
      status = clReleaseKernel(kernels_[i]);
      if (check_val(status,CL_SUCCESS,"clReleaseKernel failed.")!=CHECK_SUCCESS)
        return false;
    }
  }
  kernels_.clear();
  return true;
}

bool bit_tree_test_manager::create_command_queue()
{
  cl_int status = SDK_SUCCESS;
  // set up a command queue
  command_queue_ = clCreateCommandQueue(this->context(),this->devices()[0],CL_QUEUE_PROFILING_ENABLE,&status);
  return check_val(status,CL_SUCCESS,"Falied in command queue creation" + error_to_string(status))==1;
}

bool bit_tree_test_manager::release_command_queue()
{
  cl_int status = clReleaseCommandQueue(command_queue_);
  return check_val(status,CL_SUCCESS,"clReleaseCommandQueue failed.") == 1;
}

#endif