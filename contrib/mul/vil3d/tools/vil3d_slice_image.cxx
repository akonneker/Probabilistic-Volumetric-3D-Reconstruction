//:
// \file
// \brief Tool to load in a 3D image and produce 2D slices
// \author Tim Cootes
// Slices may be cropped, and range is stretched linearly

#include <vul/vul_arg.h>
#include <vcl_iostream.h>
#include <vil3d/vil3d_property.h>
#include <vil3d/vil3d_load.h>
#include <vil3d/vil3d_convert.h>
#include <vil3d/vil3d_slice.h>
#include <vil/vil_convert.h>
#include <vil/vil_save.h>
#include <vil/vil_crop.h>
#include <vil/vil_resample_bilin.h>
#include <vnl/vnl_math.h>
#include <vxl_config.h> // For vxl_byte

void print_usage()
{
  vcl_cout<<"Tool to load in a 3D image and produce 2D slices."<<vcl_endl;
  vul_arg_display_usage_and_exit();
}

void save_slice(const vil_image_view<float>& image,
                float wi, float wj, double border,
                vcl_string path)
{
  // Resize so that image has square pixels
  float w=vcl_min(wi,wj);
  unsigned ni=vnl_math::rnd(image.ni()*wi/w);
  unsigned nj=vnl_math::rnd(image.nj()*wj/w);

  vil_image_view<float> image2;
  if (ni==nj)
    image2=image;
  else
    vil_resample_bilin(image,image2,ni,nj);

  unsigned ilo=vnl_math::rnd(ni*border);
  unsigned ihi=ni-1-ilo;
  unsigned jlo=vnl_math::rnd(nj*border);
  unsigned jhi=nj-1-jlo;

  vil_image_view<float> cropped_image
     = vil_crop(image2,ilo,1+ihi-ilo, jlo,1+jhi-jlo);
  // Ignore image size in first test
  vil_image_view<vxl_byte> byte_im;
  vil_convert_stretch_range(cropped_image,byte_im);

  if (vil_save(byte_im,path.c_str()))
    vcl_cerr<<"Saved image to "<<path<<'\n';
  else
    vcl_cerr<<"Failed to save image to "<<path<<'\n';
}

int main(int argc, char** argv)
{
  vul_arg<vcl_string> image_path("-i","3D image filename");
  vul_arg<vcl_string> output_path("-o","Base path for output","slice");
  vul_arg<double> pi("-pi","Slice position as %age along i",50);
  vul_arg<double> pj("-pj","Slice position as %age along i",50);
  vul_arg<double> pk("-pk","Slice position as %age along i",50);
  vul_arg<double> b("-b","Border as %age",0);

  vul_arg_parse(argc,argv);

  if (image_path()=="")
  {
    print_usage();
    return 0;
  }

  // Attempt to load in the 3D image
  vil3d_image_resource_sptr im_res = vil3d_load_image_resource(image_path().c_str());
  if (im_res==VXL_NULLPTR)
  {
    vcl_cerr<<"Failed to load in image from "<<image_path()<<'\n';
    return 1;
  }

  // Read in voxel size if available
  float width[3] = { 1.0f, 1.0f, 1.0f };
  im_res->get_property(vil3d_property_voxel_size, width);
  vcl_cout<<"Voxel sizes: "
          <<width[0]<<" x "<<width[1]<<" x "<<width[2]<<vcl_endl;

//  vil3d_image_view_base_sptr im_ptr = im_res->get_view();

  // Load the image data and cast to float
  vil3d_image_view<float> image3d
    = vil3d_convert_cast(float(),im_res->get_view());

  if (image3d.size()==0)

  vcl_cout<<"Image3D: "<<image3d<<vcl_endl;
  {
    vcl_cerr<<"Failed to load in image from "<<image_path()<<'\n';
    return 1;
  }

  // Select i,j,k values
  unsigned i=vnl_math::rnd(0.01*pi()*(image3d.ni()-1));
  i=vcl_min(i,image3d.ni()-1);
  unsigned j=vnl_math::rnd(0.01*pj()*(image3d.nj()-1));
  j=vcl_min(j,image3d.nj()-1);
  unsigned k=vnl_math::rnd(0.01*pk()*(image3d.nk()-1));
  k=vcl_min(k,image3d.nk()-1);

  vil_image_view<float> slice_jk = vil3d_slice_jk(image3d,i);
  vil_image_view<float> slice_ik = vil3d_slice_ik(image3d,j);
  vil_image_view<float> slice_ij = vil3d_slice_ij(image3d,k);

  save_slice(slice_jk,width[1],width[2],0.01*b(),output_path()+"_jk.jpg");
  save_slice(slice_ik,width[0],width[2],0.01*b(),output_path()+"_ik.jpg");
  save_slice(slice_ij,width[0],width[1],0.01*b(),output_path()+"_ij.jpg");

  return 0;
}
