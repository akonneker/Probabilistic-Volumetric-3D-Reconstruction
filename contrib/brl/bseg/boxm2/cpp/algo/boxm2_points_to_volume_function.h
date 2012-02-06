#ifndef boxm2_points_to_volume_h
#define boxm2_points_to_volume_h
//:
// \file

#include <boxm2/boxm2_data_traits.h>
#include <boxm2/cpp/algo/boxm2_cast_ray_function.h>
#include <boxm2/cpp/algo/boxm2_mog3_grey_processor.h>
#include <boct/boct_bit_tree.h>
#include <vnl/vnl_vector_fixed.h>
#include <vcl_iostream.h>
#include <boxm2/io/boxm2_cache.h>
#include <vgl/vgl_point_3d.h>
#include <bbas/imesh/imesh_mesh.h>
#include <imesh/algo/imesh_kd_tree.h>
#include <bvgl/bvgl_triangle_3d.h>

class boxm2_points_to_volume
{
 public:
  typedef unsigned char uchar;
  typedef unsigned short ushort;
  typedef vnl_vector_fixed<uchar, 16> uchar16;
  typedef vnl_vector_fixed<uchar, 8> uchar8;
  typedef vnl_vector_fixed<ushort, 4> ushort4;

  //: "default" constructor
  boxm2_points_to_volume(boxm2_scene_sptr scene, 
                         boxm2_cache_sptr cache, 
                         imesh_mesh& points); 

  //: fillVolume function
  void fillVolume(); 

  //populates data_base with appropriate alphas
  void fillBlockByTri(boxm2_block_metadata& data, boxm2_block* blk, vcl_vector<vcl_vector<float> >& alphas);

  //refines tree given a triangle that intersects with it
  void refine_tree(boct_bit_tree& tree, 
                   vgl_box_3d<double>& treeBox, 
                   bvgl_triangle_3d<double>& tri,
                   vcl_vector<float>& alpha); 

  //grab triangles that intersect a bounding box
  vcl_vector<bvgl_triangle_3d<double> > 
  tris_in_box(const imesh_mesh& mesh, vgl_box_3d<double>& box);
  vcl_vector<bvgl_triangle_3d<double> >
  tris_in_box(vcl_vector<bvgl_triangle_3d<double> >& tris, vgl_box_3d<double>& box);

  //uses axis aligned bounding boxes to speed up collision detection
  void tris_in_box(const vgl_box_3d<double>& bbox,
                   const vcl_vector<bvgl_triangle_3d<double> >& tris,
                   const vcl_vector<vgl_box_3d<double> >& bboxes,
                   vcl_vector<bvgl_triangle_3d<double> >& int_tris,
                   vcl_vector<vgl_box_3d<double> >& int_boxes);


 private:
  //: scene reference
  boxm2_scene_sptr scene_;

  //: cache reference
  boxm2_cache_sptr cache_;

  //: tris and their boxes
  vcl_vector<vgl_box_3d<double> > triBoxes_;
  vcl_vector<bvgl_triangle_3d<double> > tris_;

  //: points reference
  imesh_mesh& points_;


  //---- Private Helpers ---------//
  inline float alphaProb(float prob, float len) { return -log(1.0f-prob) / len; }

  //quick boolean intersection test
  inline bool bbox_intersect(vgl_box_3d<double> const& b1,
                             vgl_box_3d<double> const& b2) {
    return !( b1.min_x() > b2.max_x() ||
              b2.min_x() > b1.max_x() ||
              b1.min_y() > b2.max_y() ||
              b2.min_y() > b1.max_y() ||
              b1.min_z() > b2.max_z() || 
              b2.min_z() > b1.max_z() ) ; 
  }

  //inclusive clamp
  inline int clamp(int val, int min, int max) { 
    if(val < min)
      return min;
    if(val > max)
      return max; 
    return val;
  }

};


#endif