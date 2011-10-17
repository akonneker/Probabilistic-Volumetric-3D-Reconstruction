#include "icam_ocl_view_metadata.h"

#include <vgl/vgl_box_3d.h>
#include <vil/vil_load.h>


icam_ocl_view_metadata::icam_ocl_view_metadata(vcl_string const& exp_img,
                                               vcl_string const& dt)
                                              : icam_view_metadata(exp_img,dt)    
{ 
#if 0
  // will use the defaulst params
  icam_minimizer_params params;
  unsigned wgsize = 16;
  minimizer_=new icam_ocl_minimizer(exp_img, dt, params, true);
  static_cast<icam_ocl_minimizer*>(minimizer_)->set_workgroup_size(wgsize);
  static_cast<icam_ocl_minimizer*>(minimizer_)->set_rot_kernel_path("trans_parallel_transf_search.cl");
  final_level_ = minimizer_->n_levels() - 3;
#endif
}

void icam_ocl_view_metadata::create_minimizer(vil_image_view<float>*& exp_img, vil_image_view<double>*& depth_img,
                                          vpgl_camera_double_sptr camera, icam_minimizer_params const& params,
                                          icam_minimizer*& minimizer)
{
  vil_image_view_base_sptr exp=vil_load(exp_img_path_.c_str());
  vil_image_view_base_sptr depth=vil_load(depth_img_path_.c_str());
  if (load_image<float>(exp, exp_img) && load_image(depth, depth_img))  {
    vpgl_perspective_camera<double>* cam = dynamic_cast<vpgl_perspective_camera<double>*> (camera.as_pointer());
    if (cam) {
      vnl_matrix_fixed<double, 3, 3> K = cam->get_calibration().get_matrix();
      vgl_rotation_3d<double> rot=cam->get_rotation();
      vgl_vector_3d<double> t=cam->get_translation();
      icam_depth_transform dt(K, *depth_img, rot, t);
      unsigned wgsize = 16;
      minimizer=new icam_ocl_minimizer (*exp_img, dt, params, false); 
      static_cast<icam_ocl_minimizer*>(minimizer)->set_workgroup_size(wgsize);
      static_cast<icam_ocl_minimizer*>(minimizer)->set_rot_kernel_path("trans_parallel_transf_search.cl");
      final_level_ = minimizer->n_levels() - 3;
    }
  }
}

void icam_ocl_view_metadata::register_image(vil_image_view<float> const& source_img, 
                                            vpgl_camera_double_sptr camera, 
                                            icam_minimizer_params const& params)
{
  // create the images
  vil_image_view<float> *exp_img=0;
  vil_image_view<double> *depth_img=0;
  icam_minimizer* minimizer;
  create_minimizer(exp_img,depth_img,camera,params,minimizer);
  if (minimizer) {
    final_level_ = minimizer->n_levels() - 4;

    // set the source to the minimizer
    minimizer->set_source_img(source_img);

    // take the coarsest level
    unsigned level = minimizer->n_levels()-1;
        
    // return params
    vgl_box_3d<double> trans_box;
    trans_box.add(vgl_point_3d<double>(-.5, -.5, -.5));
    trans_box.add(vgl_point_3d<double>(.5, .5, .5));
    vgl_vector_3d<double> trans_steps(0.5, 0.5, 0.5);
    double min_allowed_overlap = 0.5, min_overlap;
    minimizer->exhaustive_camera_search(trans_box,trans_steps,level,
                                       min_allowed_overlap,min_trans_,
                                       min_rot_, cost_,min_overlap);

    vcl_cout << " min translation " << min_trans_ << '\n';
    vcl_cout << " min rotation " << min_rot_.as_rodrigues() << '\n';
    vcl_cout << " registration cost " << cost_ << '\n'<< '\n';
    delete minimizer;
  }
  if (exp_img) delete exp_img;
  if (depth_img) delete depth_img;
}

void icam_ocl_view_metadata::refine_camera(vil_image_view<float> const& source_img,
                                           vpgl_camera_double_sptr camera,
                                           icam_minimizer_params const& params)
{
vil_image_view<float> *exp_img;
  vil_image_view<double> *depth_img;
  icam_minimizer* minimizer;
  create_minimizer(exp_img,depth_img,camera,params,minimizer);
  if (minimizer) {
    final_level_ = minimizer->n_levels() - 4;
    unsigned start_level = minimizer->n_levels()-1;
    vgl_vector_3d<double> trans_steps(0.5, 0.5, 0.5);
    double min_overlap = 0.5, act_overlap;
   // set the source to the minimizer
    minimizer->set_source_img(source_img);
    minimizer->pyramid_camera_search(min_trans_, min_rot_,
                                    trans_steps,
                                    start_level,
                                    final_level_,
                                    min_overlap,
                                    false,
                                    min_trans_,
                                    min_rot_,
                                    cost_,
                                    act_overlap);

     vcl_cout << " Pyramid search result \n";
     vcl_cout << " min translation " << min_trans_ << '\n';
     vcl_cout << " min rotation " << min_rot_.as_rodrigues() << '\n';
     vcl_cout << " registration cost " << cost_ << '\n'<< '\n';
     delete minimizer;
  }
  if (exp_img) delete exp_img;
  if (depth_img) delete depth_img;
}


void icam_ocl_view_metadata::b_read(vsl_b_istream& is)
{
  icam_view_metadata::b_read(is);
}

void icam_ocl_view_metadata::b_write(vsl_b_ostream& os) const
{
  icam_view_metadata::b_write(os);
}

vcl_ostream& operator<<(vcl_ostream& os, icam_ocl_view_metadata const& p)
{
  p.print(os);
  return os;
}

void vsl_b_read(vsl_b_istream& is, icam_ocl_view_metadata& p)
{
  p.b_read(is);
}

void vsl_b_write(vsl_b_ostream& os, icam_ocl_view_metadata const& p)
{
  p.b_write(os);
}
