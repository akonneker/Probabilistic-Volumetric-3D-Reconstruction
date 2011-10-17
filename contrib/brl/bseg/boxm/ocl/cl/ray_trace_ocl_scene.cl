#if NVIDIA
#pragma OPENCL EXTENSION cl_khr_gl_sharing : enable
#endif

//RAY_TRACE_OCL_SCENE_OPT
//uses int2 tree cells and uchar8 mixture cells
__kernel
void
ray_trace_ocl_scene_opt(__global int4    * scene_dims,  // level of the root.
                        __global float4  * scene_origin,
                        __global float4  * block_dims,
                        __global int4    * block_ptrs,
                        __global int     * root_level,
                        __global int     * num_buffer,
                        __global int     * len_buffer,
                        __global int2    * tree_array,
                        __global float   * alpha_array,
                        __global uchar8  * mixture_array,
                        __global float16 * persp_cam, // camera orign and SVD of inverse of camera matrix
                        __global uint4   * imgdims,   // dimensions of the image
                        __local  float16 * local_copy_cam,
                        __local  uint4   * local_copy_imgdims,
                        //__global float   * in_image,
                        __global uint    * gl_image)    // input image and store vis_inf and pre_inf
{
  uchar llid = (uchar)(get_local_id(0) + get_local_size(0)*get_local_id(1));

  if (llid == 0 )
  {
    local_copy_cam[0]=persp_cam[0];  // conjugate transpose of U
    local_copy_cam[1]=persp_cam[1];  // V
    local_copy_cam[2]=persp_cam[2];  // Winv(first4) and ray_origin(last four)
    (*local_copy_imgdims)=(*imgdims);
  }
  barrier(CLK_LOCAL_MEM_FENCE);

  // camera origin
  float4 ray_o=(float4)local_copy_cam[2].s4567;
  ray_o.w=1.0f;
  int rootlevel=(*root_level);

  //cell size of what?
  float cellsize=(float)(1<<rootlevel);
  cellsize=1/cellsize;
  short4 root = (short4)(0,0,0,rootlevel);
  short4 exit_face=(short4)-1;
  int curr_cell_ptr=-1;

  // get image coordinates
  int i=0,j=0;  map_work_space_2d(&i,&j);
  int lenbuffer =(*len_buffer);
  //in_image[j*get_global_size(0)+i]=0.0f;
  // rootlevel of the trees.

  // check to see if the thread corresponds to an actual pixel as in some cases #of threads will be more than the pixels.
  if (i>=(*local_copy_imgdims).z || j>=(*local_copy_imgdims).w) {
    gl_image[j*get_global_size(0)+i]=rgbaFloatToInt((float4)(0.0,0.0,0.0,0.0));
    //in_image[j*get_global_size(0)+i]=(float)-1.0f;
    return;
  }
  float4 origin=(*scene_origin);
  float4 data_return=(float4)(0.0f,1.0f,0.0f,0.0f);
  float tnear = 0.0f, tfar =0.0f;
  float4 ray_d = backproject(i,j,local_copy_cam[0],local_copy_cam[1],local_copy_cam[2],ray_o);

  //// scene origin
  float4 blockdims=(*block_dims);
  int4 scenedims=(int4)(*scene_dims).xyzw;
  scenedims.w=1;blockdims.w=1; // for safety purposes.

  float4 cell_min, cell_max;
  float4 entry_pt, exit_pt;

  //// scene bounding box
  cell_min = origin;
  cell_max = blockdims*convert_float4(scenedims)+origin;
  int hit  = intersect_cell(ray_o, ray_d, cell_min, cell_max,&tnear, &tfar);
  if (!hit) {
    gl_image[j*get_global_size(0)+i]=rgbaFloatToInt((float4)(0.0,0.0,0.0,0.0));
    //in_image[j*get_global_size(0)+i]=(float)-1.0f;
    return;
  }

  tnear=tnear>0?tnear:0;

  float fardepth=tfar;

  entry_pt = ray_o + tnear*ray_d;

  int4 curr_block_index = convert_int4((entry_pt-origin)/blockdims);

  // handling the border case where a ray pierces the max side
  curr_block_index=curr_block_index+(curr_block_index==scenedims);
  int global_count=0;
  float global_depth=tnear;
  while (!(any(curr_block_index<(int4)0) || any(curr_block_index>=(scenedims))))
  {
     // Ray tracing with in each block

     // 3-d index to 1-d index
    int4 block = block_ptrs[curr_block_index.z
                           +curr_block_index.y*scenedims.z
                           +curr_block_index.x*scenedims.y*scenedims.z];

    // tree offset is the root_ptr
    int root_ptr = block.x*lenbuffer+block.y;

    float4 local_ray_o= (ray_o-origin)/blockdims-convert_float4(curr_block_index);
    
    // canonincal bounding box of the tree
    float4 block_entry_pt=(entry_pt-origin)/blockdims-convert_float4(curr_block_index);
    short4 entry_loc_code = loc_code(block_entry_pt, rootlevel);
    short4 curr_loc_code=(short4)-1;
    
    // traverse to leaf cell that contains the entry point
    //curr_cell_ptr = traverse_force_woffset(tree_array, root_ptr, root, entry_loc_code,&curr_loc_code,&global_count,root_ptr);
    curr_cell_ptr = traverse_force_woffset_mod_opt(tree_array, root_ptr, root, entry_loc_code,&curr_loc_code,&global_count,lenbuffer,block.x,block.y);

    // this cell is the first pierced by the ray
    // follow the ray through the cells until no neighbors are found
    while (true)
    {
      //// current cell bounding box
      cell_bounding_box(curr_loc_code, rootlevel+1, &cell_min, &cell_max);
     
      // check to see how close tnear and tfar are
      int hit = intersect_cell(local_ray_o, ray_d, cell_min, cell_max,&tnear, &tfar);
     
      // special case whenray grazes edge or corner of cube
      if (fabs(tfar-tnear)<cellsize/10)
      {
        block_entry_pt=block_entry_pt+ray_d*cellsize/2;
        block_entry_pt.w=0.5;
        if (any(block_entry_pt>=(float4)1.0f)|| any(block_entry_pt<=(float4)0.0f))
            break;

        entry_loc_code = loc_code(block_entry_pt, rootlevel);
        //// traverse to leaf cell that contains the entry point
        curr_cell_ptr = traverse_woffset_mod_opt(tree_array, root_ptr, root, entry_loc_code,&curr_loc_code,&global_count,lenbuffer,block.x,block.y);
        cell_bounding_box(curr_loc_code, rootlevel+1, &cell_min, &cell_max);
        hit = intersect_cell(local_ray_o, ray_d, cell_min, cell_max,&tnear, &tfar);
        if (hit)
          block_entry_pt=local_ray_o + tnear*ray_d;
      }
      if (!hit)
          break;

      
      //int data_ptr =  block.x*lenbuffer+tree_array[curr_cell_ptr].z;
      ushort2 child_data = as_ushort2(tree_array[curr_cell_ptr].y);
      int data_ptr = block.x*lenbuffer + (int) child_data.x;

      //// distance must be multiplied by the dimension of the bounding box
      float d = (tfar-tnear)*(blockdims.x);
      global_depth+=d;
      
      // X:-) DO NOT DELETE THE LINE BELOW THIS IS A STRING REPLACEMNT
      /*$$step_cell$$*/
      // X:-)

      // Added aliitle extra to the exit point
      exit_pt=local_ray_o + (tfar+cellsize/10)*ray_d;exit_pt.w=0.5;

      // if the ray pierces the volume surface then terminate the ray
      if (any(exit_pt>=(float4)1.0f)|| any(exit_pt<=(float4)0.0f))
        break;

      //// required exit location code
      short4 exit_loc_code = loc_code(exit_pt, rootlevel);
      curr_cell_ptr = traverse_force_woffset_mod_opt(tree_array, root_ptr, root,exit_loc_code, &curr_loc_code,&global_count,lenbuffer,block.x,block.y);

      block_entry_pt = local_ray_o + (tfar)*ray_d;
    }

    // finding the next block

    // block bounding box
    cell_min=blockdims*convert_float4(curr_block_index)+origin;
    cell_max=cell_min+blockdims;
    if (!intersect_cell(ray_o, ray_d, cell_min, cell_max,&tnear, &tfar))
    {
        // this means the ray has hit a special case
        // two special cases
        // (1) grazing the corner/edge and (2) grazing the side.

        // this is the first case
        if (tfar-tnear<blockdims.x/100)
        {
            entry_pt=entry_pt + blockdims.x/2 *ray_d;
            curr_block_index=convert_int4((entry_pt-origin)/blockdims);
            curr_block_index.w=0;
        }

    }
    else
    {
        entry_pt=ray_o + tfar *ray_d;
        ray_d.w=1;
        if (any(-1*(isless(fabs(entry_pt-cell_min),(float4)blockdims.x/100.0f)*isless(fabs(ray_d),(float4)1e-3))))
            break;
        if (any(-1*(isless(fabs(entry_pt-cell_max),(float4)blockdims.x/100.0f)*isless(fabs(ray_d),(float4)1e-3))))
            break;
        curr_block_index=convert_int4(floor((entry_pt+(blockdims.x/20.0f)*ray_d-origin)/blockdims));
        curr_block_index.w=0;
    }
  }
#ifdef DEPTH
  data_return.z+=(1-data_return.w)*fardepth;
#endif
#ifdef INTENSITY
  data_return.z+=(1-data_return.w)*.5f;
#endif
  data_return.z+=(1-data_return.w)*.5f;
  gl_image[j*get_global_size(0)+i]=rgbaFloatToInt((float4)data_return.z);
  //in_image[j*get_global_size(0)+i]=(float)data_return.z;
}

__kernel
void
single_ray_probe_opt(__global int4    * scene_dims,  // level of the root.
                     __global float4  * scene_origin,
                     __global float4  * block_dims,
                     __global int4    * block_ptrs,
                     __global int     * root_level,
                     __global int     * num_buffer,
                     __global int     * len_buffer,
                     __global int2    * tree_array,
                     __global float   * alpha_array,
                     __global uchar8  * mixture_array,
                     __global float16 * persp_cam, // camera orign and SVD of inverse of camera matrix
                     __local  float16 * local_copy_cam,
                     __private int     pixeli,
                     __private int     pixelj,
                     __private float  intensity,
                     __global float   * depthout,
                     __global float   * visout,
                     //__global float   * omega,
                     __global float   * mean0,
                     __global float   * sigma0,
                     __global float   * weight0,
                     __global float   * mean1,
                     __global float   * sigma1,
                     __global float   * weight1,
                     __global float   * mean2,
                     __global float   * sigma2,
                     __global float   * output
                     )
{
  uchar llid = (uchar)(get_local_id(0) + get_local_size(0)*get_local_id(1));

  if (llid == 0 )
  {
    local_copy_cam[0]=persp_cam[0];  // conjugate transpose of U
    local_copy_cam[1]=persp_cam[1];  // V
    local_copy_cam[2]=persp_cam[2];  // Winv(first4) and ray_origin(last four)
  }
  barrier(CLK_LOCAL_MEM_FENCE);

  for(int i=0;i<1000;i++)
  {

      depthout[i]=-1.0f;
      visout[i]=-1.0f;
      mean0[i]    =-1.0;
      sigma0[i]   =-1.0f;
      weight0[i]  =-1.0f;
      mean1[i]    =-1.0f;
      sigma1[i]   =-1.0f;
      weight1[i]  =-1.0f;
      mean2[i]    =-1.0f;
      sigma2[i]   =-1.0f;
  }
  // camera origin
  float4 ray_o=(float4)local_copy_cam[2].s4567;
  ray_o.w=1.0f;
  int rootlevel=(*root_level);

  //cell size of what?
  float cellsize=(float)(1<<rootlevel);
  cellsize=1/cellsize;
  short4 root = (short4)(0,0,0,rootlevel);
  short4 exit_face=(short4)-1;
  int curr_cell_ptr=-1;

  // get image coordinates
  int i=pixeli;
  int j=pixelj;
  int lenbuffer =(*len_buffer);
  // rootlevel of the trees.

  // check to see if the thread corresponds to an actual pixel as in some cases #of threads will be more than the pixels.
  float4 origin=(*scene_origin);
  float4 data_return=(float4)(0.0f,1.0f,0.0f,0.0f);
  float tnear = 0.0f, tfar =0.0f;
  float4 ray_d = backproject(i,j,local_copy_cam[0],local_copy_cam[1],local_copy_cam[2],ray_o);

  //// scene origin
  float4 blockdims=(*block_dims);
  int4 scenedims=(int4)(*scene_dims).xyzw;
  scenedims.w=1;blockdims.w=1; // for safety purposes.

  float4 cell_min, cell_max;
  float4 entry_pt, exit_pt;

  //// scene bounding box
  cell_min = origin;
  cell_max = blockdims*convert_float4(scenedims)+origin;
  int hit  = intersect_cell(ray_o, ray_d, cell_min, cell_max,&tnear, &tfar);
  if (!hit) {
    return;
  }

  tnear=tnear>0?tnear:0;

  entry_pt = ray_o + tnear*ray_d;

  int4 curr_block_index = convert_int4((entry_pt-origin)/blockdims);

  // handling the border case where a ray pierces the max side
  curr_block_index=curr_block_index+(curr_block_index==scenedims);
  int global_count=0;
  float global_depth=tnear;
  int cnt=0;

  while (!(any(curr_block_index<(int4)0) || any(curr_block_index>=(scenedims))))
  {
     // Ray tracing with in each block

     // 3-d index to 1-d index
    int4 block = block_ptrs[curr_block_index.z
                           +curr_block_index.y*scenedims.z
                           +curr_block_index.x*scenedims.y*scenedims.z];

    // tree offset is the root_ptr
    int root_ptr = block.x*lenbuffer+block.y;

    float4 local_ray_o= (ray_o-origin)/blockdims-convert_float4(curr_block_index);
    
    // canonincal bounding box of the tree
    float4 block_entry_pt=(entry_pt-origin)/blockdims-convert_float4(curr_block_index);
    short4 entry_loc_code = loc_code(block_entry_pt, rootlevel);
    short4 curr_loc_code=(short4)-1;
    
    // traverse to leaf cell that contains the entry point
    curr_cell_ptr = traverse_force_woffset_mod_opt(tree_array, root_ptr, root, entry_loc_code,&curr_loc_code,&global_count,lenbuffer,block.x,block.y);

    // this cell is the first pierced by the ray
    // follow the ray through the cells until no neighbors are found
    while (true)
    {
      //// current cell bounding box
      cell_bounding_box(curr_loc_code, rootlevel+1, &cell_min, &cell_max);
     
      // check to see how close tnear and tfar are
      int hit = intersect_cell(local_ray_o, ray_d, cell_min, cell_max,&tnear, &tfar);
     
      // special case whenray grazes edge or corner of cube
      if (fabs(tfar-tnear)<cellsize/10)
      {
        block_entry_pt=block_entry_pt+ray_d*cellsize/2;
        block_entry_pt.w=0.5;
        if (any(block_entry_pt>=(float4)1.0f)|| any(block_entry_pt<=(float4)0.0f))
            break;

        entry_loc_code = loc_code(block_entry_pt, rootlevel);
        //// traverse to leaf cell that contains the entry point
        curr_cell_ptr = traverse_woffset_mod_opt(tree_array, root_ptr, root, entry_loc_code,&curr_loc_code,&global_count,lenbuffer,block.x,block.y);
        cell_bounding_box(curr_loc_code, rootlevel+1, &cell_min, &cell_max);
        hit = intersect_cell(local_ray_o, ray_d, cell_min, cell_max,&tnear, &tfar);
        if (hit)
          block_entry_pt=local_ray_o + tnear*ray_d;
      }
      if (!hit)
          break;

      
      //int data_ptr =  block.x*lenbuffer+tree_array[curr_cell_ptr].z;
      ushort2 child_data = as_ushort2(tree_array[curr_cell_ptr].y);
      int data_ptr = block.x*lenbuffer + (int) child_data.x;

      //// distance must be multiplied by the dimension of the bounding box
      float d = (tfar-tnear)*(blockdims.x);
      if(d<0.0)break;
      global_depth+=d;
      depthout[cnt]=global_depth;
      visout[cnt]=data_return.y;
    
      //step_cell_render_opt(mixture_array,alpha_array,data_ptr,d,&data_return);
      //step_cell_change_detection(mixture_array,alpha_array,data_ptr,d,&data_return);
      step_cell_change_detection_uchar8(mixture_array,alpha_array,data_ptr,d,&data_return,intensity);
      //visout[cnt]-=data_return.y;
      //visout[cnt]=1-exp(-alpha_array[data_ptr]*d);
      // Added aliitle extra to the exit point
      mean0[cnt]   =mixture_array[data_ptr].s0/255.0f;
      sigma0[cnt]  =mixture_array[data_ptr].s1/255.0f;
      weight0[cnt] =mixture_array[data_ptr].s2/255.0f;
      mean1[cnt]   =mixture_array[data_ptr].s3/255.0f;
      sigma1[cnt]  =mixture_array[data_ptr].s4/255.0f;
      weight1[cnt] =mixture_array[data_ptr].s5/255.0f;
      mean2[cnt]   =mixture_array[data_ptr].s6/255.0f;
      sigma2[cnt]  =mixture_array[data_ptr].s7/255.0f;
      ++cnt;
    

      exit_pt=local_ray_o + (tfar+cellsize/10)*ray_d;exit_pt.w=0.5;

      // if the ray pierces the volume surface then terminate the ray
      if (any(exit_pt>=(float4)1.0f)|| any(exit_pt<=(float4)0.0f))
        break;

      //// required exit location code
      short4 exit_loc_code = loc_code(exit_pt, rootlevel);
      curr_cell_ptr = traverse_force_woffset_mod_opt(tree_array, root_ptr, root,exit_loc_code, &curr_loc_code,&global_count,lenbuffer,block.x,block.y);

      block_entry_pt = local_ray_o + (tfar)*ray_d;
    }

    // finding the next block

    // block bounding box
    cell_min=blockdims*convert_float4(curr_block_index)+origin;
    cell_max=cell_min+blockdims;
    if (!intersect_cell(ray_o, ray_d, cell_min, cell_max,&tnear, &tfar))
    {
        // this means the ray has hit a special case
        // two special cases
        // (1) grazing the corner/edge and (2) grazing the side.

        // this is the first case
        if (tfar-tnear<blockdims.x/100)
        {
            entry_pt=entry_pt + blockdims.x/2 *ray_d;
            curr_block_index=convert_int4((entry_pt-origin)/blockdims);
            curr_block_index.w=0;
        }

    }
    else
    {
        entry_pt=ray_o + tfar *ray_d;
        ray_d.w=1;
        if (any(-1*(isless(fabs(entry_pt-cell_min),(float4)blockdims.x/100.0f)*isless(fabs(ray_d),(float4)1e-3))))
            break;
        if (any(-1*(isless(fabs(entry_pt-cell_max),(float4)blockdims.x/100.0f)*isless(fabs(ray_d),(float4)1e-3))))
            break;
        curr_block_index=convert_int4(floor((entry_pt+(blockdims.x/20.0f)*ray_d-origin)/blockdims));
        curr_block_index.w=0;
    }
  }
  (*output)=data_return.z;
}