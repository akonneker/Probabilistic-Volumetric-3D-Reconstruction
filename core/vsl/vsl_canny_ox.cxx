#include "vsl_canny_ox.h"
#include <vsl/vsl_canny_port.h>
#include <vsl/vsl_canny_ox_params.h>
#include <vsl/vsl_kernel.h>
#include <vsl/vsl_canny_smooth.h>
#include <vsl/vsl_canny_gradient.h>
#include <vsl/vsl_canny_nms.h>

#include <vcl/vcl_cmath.h>
#include <vcl/vcl_cstdlib.h>
#include <vcl/vcl_list.h>
#include <vcl/vcl_iostream.h>
#include <vcl/vcl_cassert.h>
#include <vnl/vnl_math.h>

//#define proddy(n) { delete [] new char [n]; }
//#define prod_heap { for (int n=1; n <= 0x100000; n *= 2) proddy(n); }

//-----------------------------------------------------------------------------

vsl_canny_ox::vsl_canny_ox(vsl_canny_ox_params const &params)
  : vsl_canny_base(params.sigma, params.low, params.high, params.verbose)
{
  // Set the maximum allowable convolution kernel
  _gauss_tail = params.gauss_tail;
  _max_width_OX = params.max_width;
  _kernel = new float[_max_width_OX];
  _sub_area_OX = new float[_max_width_OX];

  _edge_min_OX = params.edge_min;
  _min_length_OX = params.min_length;
  _join_flag_OX = params.join_flag;
  _border_size_OX = params.border_size;
  _border_value_OX = params.border_value;
  _scale_OX = params.scale;
  _follow_strategy_OX = params.follow_strategy;
  _junction_option_OX = params.junction_option;

  _xjunc = new vcl_list<int>;
  _yjunc = new vcl_list<int>;
  _vlist = new vcl_list<vsl_Vertex*>;

  _jval = 2000.0;
}

//-----------------------------------------------------------------------------

vsl_canny_ox::~vsl_canny_ox() { 
  vsl_canny_base_free_raw_image(_smooth);
  vsl_canny_base_free_raw_image(_dx);
  vsl_canny_base_free_raw_image(_dy);
  vsl_canny_base_free_raw_image(_grad);
  vsl_canny_base_free_raw_image(_thick);
  vsl_canny_base_free_raw_image(_thin);
  vsl_canny_base_free_raw_image(_theta);

  vsl_canny_base_free_raw_image(_junction);
  vsl_canny_base_free_raw_image(_jx);
  vsl_canny_base_free_raw_image(_jy);

  fsm_delete_array _kernel;
  fsm_delete_array _sub_area_OX;
  //no point _vlist->clear();
  fsm_delete _vlist;
  fsm_delete _xjunc;  
  fsm_delete _yjunc;
}

//-----------------------------------------------------------------------------

void vsl_canny_ox::detect_edges(vil_image const &image_in, vcl_list<vsl_Edge*> *edges) {
  assert(edges);

  // Get the image size
  _xsize = image_in.height();
  _ysize = image_in.width();
  _xstart = 0;
  _ystart = 0;

  if (verbose)
    cerr << "Doing Canny on image region " 
	 << _xsize << " by " << _ysize << endl
	 << "Sigma               = " << _sigma << endl
         << "Gaussian tail       = " << _gauss_tail << endl
         << "Max kernel size     = " << _max_width_OX << endl
	 << "Upper threshold     = " << _high << endl
	 << "Lower threshold     = " << _low << endl
         << "Min edgel intensity = " << _edge_min_OX << endl
         << "Min edge length     = " << _min_length_OX << endl
         << "Image border size   = " << _border_size_OX << endl << endl;

  // Allocate internal bitmaps ..
  _smooth = vsl_canny_base_make_raw_image(_xsize, _ysize, (float*)0);
  _dx     = vsl_canny_base_make_raw_image(_xsize, _ysize, (float*)0);
  _dy     = vsl_canny_base_make_raw_image(_xsize, _ysize, (float*)0);
  _grad   = vsl_canny_base_make_raw_image(_xsize, _ysize, (float*)0); 
  _thick  = vsl_canny_base_make_raw_image(_xsize, _ysize, (float*)0);
  _thin   = vsl_canny_base_make_raw_image(_xsize, _ysize, (float*)0);
  _theta  = vsl_canny_base_make_raw_image(_xsize, _ysize, (float*)0);
  _junction = vsl_canny_base_make_raw_image(_xsize, _ysize, (int*)0);
  _jx       = vsl_canny_base_make_raw_image(_xsize, _ysize, (int*)0);
  _jy       = vsl_canny_base_make_raw_image(_xsize, _ysize, (int*)0);
  //_image = 0;

  // .. and initialize them.
  vsl_canny_base_fill_raw_image(_grad, _xsize, _ysize, 0.0f);
  vsl_canny_base_fill_raw_image(_thick, _xsize, _ysize, 0.0f);
  vsl_canny_base_fill_raw_image(_thin, _xsize, _ysize, 0.0f);
  vsl_canny_base_fill_raw_image(_theta, _xsize, _ysize, 10000.0f);
  vsl_canny_base_fill_raw_image(_junction, _xsize, _ysize, 0);
  vsl_canny_base_fill_raw_image(_jx, _xsize, _ysize, 0);
  vsl_canny_base_fill_raw_image(_jy, _xsize, _ysize, 0);

  // Do the traditional Canny parts

  if (verbose) cerr << "setting convolution kernel and zeroing images\n";
  vsl_kernel_DOG(_kernel, _sub_area_OX, _k_size,
		 _sigma, _gauss_tail,
		 _max_width_OX, _width);
  if (verbose) cerr << "Kernel size     = " << _k_size << endl;

  
  if (verbose) cerr << "smoothing the image\n";
  vsl_canny_smooth(image_in,
		   _kernel, _width, _sub_area_OX,
		   _smooth);

  if (verbose) cerr << "computing x derivatives, y derivatives and norm of gradient\n";
  vsl_canny_gradient(_xsize, _ysize, _smooth, _dx, _dy, _grad);


  if (verbose) cerr << "doing non-maximal supression\n";
  int n_edgels_NMS = vsl_canny_nms(_xsize, _ysize, _dx, _dy, _grad, _thick, _theta);
  if (verbose) cerr << "Number of edgels after NMS = " << n_edgels_NMS << endl;



  // (_x,_y) holds the pixel location (and not the sub pixel accuracy)
  // of the edges.
  // Needed in Get_hysteresis_edgelsOX() to fill the _thin image 
  // at the edgels locations
  // (_x,_y)'s are initialised in Get_NMS_edgelsOX()
  int *_x = new int[n_edgels_NMS];
  int *_y = new int[n_edgels_NMS];


  // Copy edgels from _thick image into an vsl_EdgelChain class.
  // vsl_Edgels after NMS are not connected, but still are stored in vsl_EdgelChain.
  // edgels_NMS is the input to Hysteresis
  vsl_EdgelChain *edgels_NMS = Get_NMS_edgelsOX(n_edgels_NMS, _x, _y);


  if (verbose) cerr << "doing hysteresis\n";
  int *status = new int[n_edgels_NMS];
  int n_edgels_Hysteresis = HysteresisOX(edgels_NMS, status);
  if (verbose) cerr << "Number of edgels after Hysteresis = " << n_edgels_Hysteresis << endl;

  vsl_EdgelChain *edgels_Hysteresis = new vsl_EdgelChain(n_edgels_Hysteresis);
  Get_hysteresis_edgelsOX(edgels_NMS,status,edgels_Hysteresis, _x, _y);

  // delete the edgels that are output of Non_maximal_supression
  fsm_delete_array status;
  fsm_delete edgels_NMS; edgels_NMS = 0;

  // delete *_x and *_y; they are not needed anymore
  fsm_delete_array _x;
  fsm_delete_array _y;

  if (_follow_strategy_OX == 0)  {  //Don't do the follow stage of canny 
    
    edges->push_front( NO_FollowerOX(edgels_Hysteresis) );
    // delete the edgels that are output of Hysteresis
    fsm_delete edgels_Hysteresis;
    return;
  }
  
  // delete the edgels that are output of Hysteresis
  fsm_delete edgels_Hysteresis;
  
  // Multiply _thin image by _scaleOX. Values that are above 255 are set to 255
  Scale_imageOX(_thin, _scale_OX);
  // Set image border to _border_valueOX (default = 0) so follow can't overrun
  Set_image_borderOX(_thin, _border_size_OX, _border_value_OX);

  if (_junction_option_OX) { 

    // Locate junctions in the edge image
    if (verbose) cerr << "locating junctions in the edge image - ";
    Find_junctionsOX();
    if (verbose) cerr << _xjunc->size() << " junctions found\n";
    Find_junction_clustersOX();
    if (verbose)  cerr << _vlist->size() << " junction clusters found\n";
  }

  // Finally do edge following to extract the edge data from the _thin image
  if (verbose) cerr << "doing final edge following\n";
  FollowerOX(edges);
  if (verbose) cerr << "finished vsl_canny_ox\n";
}



//-----------------------------------------------------------------------------


// -- Copy edgels from _thick image. These edgels are the result of NMS.
// Although the edgels are stored in vsl_EdgelChain edgels_NMS, they are
// not actually connected. 
// Also initialises _x and _y: the pixel locations of the edgels. These
// are needed later in filling the image _thin with the edgels after 
// hysteresis.
vsl_EdgelChain *vsl_canny_ox::Get_NMS_edgelsOX(int n_edgels_NMS, int *_x, int *_y) {
  // the number of edges must be given in advance!
  vsl_EdgelChain *edgels_NMS = new vsl_EdgelChain(n_edgels_NMS);

  int i = 0;

  for (int y=1; y<=_ysize-2; ++y) 
    for (int x=1; x<=_xsize-2; ++x) 
      if ( _thick[x][y] != 0 /*&& i < n_edgels_NMS*/) {
	assert(i < n_edgels_NMS);
	// fill edgels_NMS
	edgels_NMS->SetX(_dx[x][y],i);
	edgels_NMS->SetY(_dy[x][y],i);
	edgels_NMS->SetGrad(_thick[x][y],i);
	edgels_NMS->SetTheta(_theta[x][y],i);
	// remember the pixel locations (x,y) of the edgels
	_x[i] = x; _y[i] = y;
	i++;
      }

  return edgels_NMS;
}



//-----------------------------------------------------------------------------
//
// -- Hysteresis follows edgels that lie above the low threshold and have at 
// least one edgel above the high threshold.
//
int vsl_canny_ox::HysteresisOX(vsl_EdgelChain *&edgels_NMS,
			       int *&status)
{
  int n_edgels_NMS = edgels_NMS->size();

  // Allocate arrays ..
  vcl_vector<unsigned> rows(_ysize+1); // rows[i] will contain the index in 'edgels_NMS' of the first
  //                                      edgel after start of row i. Thus, the edgels in row i are exactly
  //                                      those with indices j in the range rows[i] <= j < rows[i+1].
  vcl_vector<unsigned> row(n_edgels_NMS);  // (row[i], col[i]) will be the position of the ith
  vcl_vector<unsigned> col(n_edgels_NMS);  // edgel in 'edgels_NMS'.
  vcl_vector<vsl_LINK *> links(n_edgels_NMS);  // 
  // .. initialize arrays.
  for (int i=0; i<n_edgels_NMS; ++i) {
    links[i]  = 0; // null pointer
    row[i]    = (int) edgels_NMS->GetY(i);
    col[i]    = (int) edgels_NMS->GetX(i);
    status[i] = 0;
  }
  for (int i=0,j=0; i<=_ysize; ++i) { // Note: rows[_ysize] is one more than last edgel index
    while (j<n_edgels_NMS && row[j]<i)
      ++j;
    rows[i]=j;   // index of first edgel after start of row i
  }
  


  // Create a list of links for each edgel.
  Link_edgelsOX(col, rows, &links[0]); //vector<>::iterator


  // -- Perform Hysteresis part of canny.
  float low  = (32.0/log(2.0)) * log(_low/100+1.0);     // compute lower threshold
  float high = (32.0/log(2.0)) * log(_high/100+1.0);    // compute upper threshold
  //formerly "Do_hysteresisOX(edgels_NMS,links,status,low,high);"
  for (int i=0; i<n_edgels_NMS; ++i) 
    if (!status[i] && edgels_NMS->GetGrad(i)>high) {
      status[i]=1;
      for (vsl_LINK *lptr=links[i]; lptr; lptr=lptr->nextl) 
	Initial_followOX(lptr->to, i, edgels_NMS, &links[0], status, low); //vector<>::iterator
    }
  

  // and this?
  int n_edgels_Hysteresis = Get_n_edgels_hysteresisOX(edgels_NMS,status);

  // delete the vsl_LINK * in the array 'links' :
  for (int i=0; i<n_edgels_NMS; ++i) {
    for (vsl_LINK *link1 = links[i]; link1; ) {
      vsl_LINK *link2 = link1->nextl;
      fsm_delete link1;
      link1 = link2;
    }
  }

  return n_edgels_Hysteresis;
}


//-------------------------------------------------------------------------

// -- Initial follow. Used for the hysteresis part of canny.
void vsl_canny_ox::Initial_followOX(int to,
				    int from,
				    vsl_EdgelChain *&edgels_NMS,
				    vsl_LINK *links[],
				    int *&status,
				    float low)
{
  if (!status[to] && edgels_NMS->GetGrad(to) > low)
    status[to]=1;
  else
    return;

  for (vsl_LINK *lptr=links[to]; lptr; lptr=lptr->nextl) 
    if (lptr->to!=from)
      Initial_followOX(lptr->to,to,edgels_NMS,links,status,low);
}



//-------------------------------------------------------------------------

// -- Add link. 'edgel' and 'to' are the indices of the two edges to be linked.
// A link with value 'to'    becomes the new head of the link-list for 'edgel'.
// A link with value 'edgel' becomes the new head of the link-list for 'to'.
void vsl_canny_ox::Add_linkOX(int edgel,
			      int to,
			      vsl_LINK *links[])
{
  vsl_LINK *lptr1=new vsl_LINK;
  lptr1->to=to;
  lptr1->nextl=links[edgel];
  links[edgel]=lptr1;

  vsl_LINK *lptr2=new vsl_LINK;
  lptr2->to=edgel;
  lptr2->nextl=links[to];
  links[to]=lptr2;
}



//-------------------------------------------------------------------------
//
// -- Link edgels.  First try pixels at distance 1 (direct neighbours),
// then at sqrt(2) (diagonal), then at 2 (horizontal or vertical), then
// at sqrt(5) (chess horse).
void vsl_canny_ox::Link_edgelsOX(vcl_vector<unsigned> const &col,
				 vcl_vector<unsigned> const &rows,
				 vsl_LINK *links[])
  // Rewritten and inline-documented by Peter Vanroose, 30 Dec. 1999.
{
  for (int i=0; i<_ysize; ++i) {// for each image row
    for (unsigned j=rows[i]; j<rows[i+1]; ++j) {// for each edgel in this row
      // what are these for?
      bool e=false;
      bool w=false;
      bool s=false;
      
      // First link horizontal, direct neighbours:

      if (j+1<rows[i+1] && col[j]+1==col[j+1])  {  // next edgel is (horiz) neighbour 
	Add_linkOX(j, j+1, links); e=true; }
      
      w=(j>rows[i] && col[j]==col[j-1]+1);// previous edgel was (horiz) neighbour
      
      // Don't go on (except for distance 2 neighbour) if there is certainly
      // no vertical neighbour edgel:
      if (rows[i+1] == rows[_ysize]) {
	if (e) continue;
	// Verify that there was no diagonal north-east link:
	if (i > 0) {
	  unsigned int k=rows[i-1]; // candidate diagonal neighbour edgel
	  while (k < rows[i] && col[k] < col[j]+1) ++k;
	  if (k < rows[i] && col[k] == col[j]+1) continue; // found n-e link
	}
	// Now verify the east-2 link:
	if (j+1<rows[i+1] && col[j]+2==col[j+1])
	  Add_linkOX(j, j+1, links);
	continue; // go on with the next value of j
      }
      
      // Now try to link vertically:
      
      unsigned int k=rows[i+1]; // candidate vertical (/ | or \) neighbour edgel

      while (k < rows[i+2] && col[k]+1 < col[j]) ++k; // skip the early ones

      // Note that rows[i+2] makes sense when rows[i+1]<rows[_ysize]
      if (k == rows[i+2]) continue; // no vertical or diagonal neighbour

      if (k+1 < rows[i+2] && col[k+1] == col[j]) ++k;
      if (col[j]==col[k])  {  // j and k are (vertical) neighbours 
	Add_linkOX(j, k, links); s = true; }

      // Diagonal neighbours (distance sqrt(2)):

      bool se = false;
      if (!e && !s && col[j]+1==col[k])  {  // j and k are diagonal \ neigbours 
	Add_linkOX(j,k,links); se = true; };
      if (!w && !s && col[j]==col[k]+1)  {  // j and k are diagonal / neigbours 
	Add_linkOX(j,k,links); s = w = true; };
      if (se) { s = e = true; };

      // Verify if there was a diagonal north-east or north-west link:
      if (i > 0) {
	k=rows[i-1];
	while (k < rows[i] && col[k]+1 < col[j]) ++k;
	if (k < rows[i] && col[k]+1 == col[j])  w = true;
	while (k < rows[i] && col[k] < col[j]+1) ++k;
	if (k < rows[i] && col[k] == col[j]+1)  e = true;
      }

      if (e && w && s) continue;

      // Horizontal neighbours at distance 2:

      if (!e && j+1<rows[i+1] && col[j]+2==col[j+1]) { 
	Add_linkOX(j, j+1, links); e = true; }

      if (j>rows[i] && col[j]==col[j-1]+2)
	w = true;

      // Vertical neighbour at distance 2:

      k=rows[i+2];
      if (!s && k < rows[_ysize]) {
	while (k < rows[i+3] && col[k] < col[j]) ++k;
	// Note that rows[i+3] makes sense if rows[i+2]<rows[_ysize]
	if (k < rows[i+3] && col[j] == col[k]) { 
	  Add_linkOX(j, k, links); s = true; }
      }

      if (e && w && s) continue;

      // Neighbours at distance sqrt(5):

      // First find the e-s-e and w-s-w neighbours:
      k=rows[i+1];
      while (k < rows[i+2] && col[k]+2 < col[j]) ++k;
      if (!w && k < rows[i+2] && col[j] == col[k]+2) { 
	Add_linkOX(j, k, links); s = w = true; }
      while (k < rows[i+2] && col[k] < col[j]+2) ++k;
      if (!e && k < rows[i+2] && col[j]+2 == col[k]) { 
	Add_linkOX(j, k, links); s = e = true; }

      if (s) continue;

      // And finally the s-s-e and s-s-w neighbours:
      k=rows[i+2]; if (k == rows[_ysize]) continue;
      while (k < rows[i+3] && col[k]+1 < col[j]) ++k;
      if (k < rows[i+3] && (col[j] == col[k]+1 || col[j]+1 == col[k]))
	Add_linkOX(j, k, links);

    } // end for j
  } // end for i
}


//-------------------------------------------------------------------------
//
// -- Returns the number of edgels after hysteresis.
//
int vsl_canny_ox::Get_n_edgels_hysteresisOX(vsl_EdgelChain *&edgels_NMS,
					    int *&status)
{
  int n_edgels_Hysteresis = 0;
  for (int i=edgels_NMS->size()-1; i>=0; --i)
    if (status[i])
      ++n_edgels_Hysteresis;
  return n_edgels_Hysteresis;
}



//-------------------------------------------------------------------------
//
// -- Returns the edgels after hysteresis 
// Also fill the image _thin with the edgels after hysteresis for
// further processing by the FollowerOX() part.
//
void vsl_canny_ox::Get_hysteresis_edgelsOX(vsl_EdgelChain *& edgels_NMS,
					   int *&status,
					   vsl_EdgelChain *& edgels_Hysteresis,
					   int *_x, int *_y)
{
  // Initialise _thin to zero's
  vsl_canny_base_fill_raw_image(_thin, _xsize, _ysize, 0.0f);

  int n_edgels_NMS = edgels_NMS->size();

  for (int i=0,j=0; i<n_edgels_NMS; ++i) {
    if (status[i]) {
      // Fill edgels_Hysteresis
      edgels_Hysteresis->SetX(edgels_NMS->GetX(i),j);
      edgels_Hysteresis->SetY(edgels_NMS->GetY(i),j);
      edgels_Hysteresis->SetGrad(edgels_NMS->GetGrad(i),j);
      edgels_Hysteresis->SetTheta(edgels_NMS->GetTheta(i),j);
      // Re-fill image _thin for further processing
      _thin[_x[i]][_y[i]] = edgels_NMS->GetGrad(i);
      j++;
    } // end if
  } // end for i
}

//----------------------------------------------------------------------------

// -- NO_FollowerOX : In the case of _follow_strategy_OX = 0, this function
// returns an vsl_Edge * filled from vsl_EdgelChain *edgels_Hysteresis.
// i.e., the result of the Hysteresis part of Canny is returned in the vsl_Edge *.
// The Follow part (FollowerOX) is not executed.
vsl_Edge *vsl_canny_ox::NO_FollowerOX(vsl_EdgelChain *edgels_Hysteresis) 
{
  int n_edgels_Hysteresis = edgels_Hysteresis->size();
  
  // Define a digital curve to store the edgels data
  //  dc only stores the list of edgels after the Hysteresis stage of canny. 
  //  dc is used later in this function to define an vsl_Edge *edge
  vsl_Edge *dc = new vsl_Edge(n_edgels_Hysteresis, 
			      new vsl_Vertex(edgels_Hysteresis->GetX(0)+_xstart, 
					     edgels_Hysteresis->GetY(0)+_ystart), 
			      new vsl_Vertex(edgels_Hysteresis->GetX(n_edgels_Hysteresis-1)+_xstart,
					     edgels_Hysteresis->GetY(n_edgels_Hysteresis-1)+_ystart));

  float* px = dc->GetX();    float* py = dc->GetY();
  float* pg = dc->GetGrad(); float* pt = dc->GetTheta();
  
  for (int i=0; i<n_edgels_Hysteresis; ++i) {
    *(px++) = edgels_Hysteresis->GetX(i) + _xstart;
    *(py++) = edgels_Hysteresis->GetY(i) + _ystart;
    *(pg++) = edgels_Hysteresis->GetGrad(i);
    *(pt++) = edgels_Hysteresis->GetTheta(i);
  }
  
  return dc; //new vsl_Edge(dc);
}



//----------------------------------------------------------------------------

// -- Returns the first vsl_Vertex* in l which matches (i.e. compares equal to) v.
// returns 0 if none found.
vsl_Vertex *vsl_find(vcl_list<vsl_Vertex*> const *l, vsl_Vertex const &v) {
  for (vcl_list<vsl_Vertex*>::const_iterator i=l->begin(); i!=l->end(); ++i) 
    if (v == *(*i))
      return *i;
  return 0;
}

vsl_Vertex *vsl_find(vcl_list<vsl_Vertex*> const *l, float x, float y) {
  for (vcl_list<vsl_Vertex*>::const_iterator i=l->begin(); i!=l->end(); ++i) 
    if ((*i)->x == x && (*i)->y == y)
      return *i;
  return 0;
}


//
// -- Go through every point in the image and for every one which is above a
// threshold:   follow from the point, in one direction and then the other.
//
void vsl_canny_ox::FollowerOX(vcl_list<vsl_Edge*> *edges) {
  if (_junction_option_OX)
    _chain_no = 10;    // Must be set to a number >= 1

  // temporaries used in loop
  vcl_list<int> xcoords, ycoords;
  vcl_list<float> grad; 
      
  edges->clear();
  for (int x=_border_size_OX; x<_xsize-_border_size_OX; ++x) {
    for (int y=_border_size_OX; y<_ysize-_border_size_OX; ++y) {
      // Due to Initial_hysteresis we can follow everything > _edge_min_OX
      if ( (_thin[x][y] < _edge_min_OX) || _junction[x][y] ) 
	continue;
      
      if (_junction_option_OX)
	_chain_no++;
      
      // clear lists before accumulating edgels.
      xcoords.clear();  
      ycoords.clear();  
      grad.clear();

      // forward follow [? fsm]
      Final_followOX(x,y, &xcoords, &ycoords, &grad,0);
      
      // We may have picked up the edgel chain somewhere
      // away from its ends. Therefore, reverse the list
      // and try to follow again. 
      xcoords.reverse();  
      ycoords.reverse();  
      grad.reverse();
      Final_followOX(x,y,&xcoords,&ycoords,&grad,1);
      
      int count=xcoords.size();
      if (count < _min_length_OX || count < 1)
	// cerr << "short list found in Final_followOX\n";
	continue;
      
      // Create an vsl_EdgeChain and add to the list
      vsl_EdgelChain * dc = new vsl_EdgelChain(count);
      float *px = dc->GetX();
      float *py = dc->GetY();
      float *pg = dc->GetGrad();
      float *pt = dc->GetTheta();
      
      // Write the points to the vsl_EdgelChain and the end points to the Curve
      //dc->SetStart(xcoords.front()+_xstart, ycoords.front()+_ystart);
      int tmpx=0,tmpy=0;// dummy initialization, as count is always > 0.
      while (count--) {
	tmpx = xcoords.front(); xcoords.pop_front(); 
	tmpy = ycoords.front(); ycoords.pop_front(); 
	float val = grad.front(); grad.pop_front();
	// If we are not at a junction use sub-pixel value
	if ( val != _jval ) {
	  *(px++) = _dx[tmpx][tmpy] + _xstart;
	  *(py++) = _dy[tmpx][tmpy] + _ystart;
	  *(pg++) = val;
	}
	else {
	  *(px++) = tmpx + _xstart;
	  *(py++) = tmpy + _ystart;
	  *(pg++) = 0.0; // Mark the gradient as zero at a junction
	}
	*(pt++) = _theta[tmpx][tmpy];
      }
      //dc->SetEndX(tmpx+_xstart, tmpy+_ystart);
      
      // Just check whether we have created a trivial edgechain
      // can happen if _min_length_OX = 2
      if ( (dc->size()==2) && 
	   (dc->GetX(0)==dc->GetX(1)) &&
	   (dc->GetY(0)==dc->GetY(1)) ) {
	//cerr << "trivial edgechain" << endl;
	delete dc;
	dc = 0;
	continue;
      }
      else if ( dc->size() > 1 ) {
	// Create an edge for the image topology
	vsl_Vertex *v1 = new vsl_Vertex(dc->GetX(0),dc->GetY(0));
	vsl_Vertex *v2 = new vsl_Vertex(dc->GetX(dc->size()-1),dc->GetY(dc->size()-1));
	
	if (_junction_option_OX) {
	  // Check whether each vertex is a junction
	  vsl_Vertex *V1 = vsl_find(_vlist, *v1);
	  vsl_Vertex *V2 = vsl_find(_vlist, *v2);
	  
	  // If neither are junctions we may have formed a single 
	  // isolated chain that should have common vertex endpoints.
	  bool single_chain = false;
	  if ( !V1 && !V2 ) {
	    float dx = dc->GetX(0) - dc->GetX(dc->size()-1);
	    float dy = dc->GetY(0) - dc->GetY(dc->size()-1);
	    if (dc->size() < 1 || dx*dx+dy*dy < 4) {// ie. dist < 2 pixels it is closed
	      //edge = new vsl_Edge(v1,v1);
	      //IUDelete(v2);
	      delete v2;
	      v2 = v1;
	      single_chain = true;
	    }
	  }
	  
	  if (!single_chain) {
	    if (V1) { delete v1; v1 = V1; }//IUDelete (v1); v1 = V1; }
	    if (V2) { delete v2; v2 = V2; }//IUDelete (v2); v2 = V2; }
	    //edge = new vsl_Edge(v1,v2);
	  }
	}
	else {
	  // We may have formed a single isolated
	  // chain that should have common vertex endpoints.
	  float dx = dc->GetX(0) - dc->GetX(dc->size()-1);
	  float dy = dc->GetY(0) - dc->GetY(dc->size()-1);
	  if ((dx*dx+dy*dy) < 4.0) {// ie. dist < 2 pixels it is closed
	    //edge = new vsl_Edge(v1,v1);
	    //IUDelete(v2);
	    delete v2;
	    v2 = v1;
	  }
	  //else
	  //  edge = new vsl_Edge(v1,v2);
	}
	
	// Note that the edge can start and end in the same vsl_Vertex.
	// However, if this is so the DigitalCurve has positive length
	//dc->SetStart(dc->GetX(0), dc->GetY(0));
	//dc->SetEnd(dc->GetX(dc->size()-1),dc->GetY(dc->size()-1));
	
	//cerr << __FILE__ ": push" << endl;
	edges->push_front(new vsl_Edge(*dc, v1, v2));
	delete dc;
      }
    }
  }
  //cerr << "edges->size() : " << edges->size() << endl;
}



//-----------------------------------------------------------------------------
// -- Adds point (x, y) to the current curve, sets its value in the image to
// zero (any point may be included at most once in at most one curve)
// then searches for adjacent pixels (in an order intended to make closed
// curves ordered in a clockwise direction).
// If none are found, and we are not at a junction Join_dotsOX() is called to
// search for non-adjacent pixels.
// If a new point is found, a recursive call is made to Final_followOX() with
// that point.
//
// Two strategies can be followed according to _follow_strategy_OX
// if _follow_strategy_OX = 1 then neighbours are checked in the following
//    order (Charlie Rothwell's way)
// @{
// \begin{verbatim}
//         8  7  6
//         1  x  5
//         2  3  4
// \end{verbatim}
// @}
// but if _follow_strategy_OX = 2 then neighbours are checked in the following
//    order (Nic Pillow's way)
// @{
// \begin{verbatim}
//         8  6  7
//         1  x  4
//         3  2  5
// \end{verbatim}
// @}
//
void vsl_canny_ox::Final_followOX(int x,
				  int y,
				  vcl_list<int> *xc,
				  vcl_list<int> *yc,
				  vcl_list<float> *grad,
				  int reverse)
{
  // Make sure that we do not overun the border of the image
  if ( (x<=0) || (x>=_xsize-1) || (y<=0) || (y>=_ysize-1) )
    return;

  // Add the current point to the coordinate lists, and delete from
  // the edge image
  if (!reverse) {
    xc->push_front(x);
    yc->push_front(y);
    grad->push_front(_thin[x][y]);
  }
  _thin[x][y] = 0.0;

  bool junction_or_jump = false;

  switch (_follow_strategy_OX) {
  case 1: // charlie rothwell way
    // Find one adjacent pixel;  for a closed curve, the method `guarantees' 
    // a clockwise ordering of the points in the image sense
    if (false) { }
#define smoo(a, b) \
    else if ( (_thin[a][b] >= _edge_min_OX) && (!_junction[a][b]) ) \
      Final_followOX(a,b,xc,yc,grad,0);
    smoo(x-1, y  )
    smoo(x-1, y+1)
    smoo(x  , y+1)
    smoo(x+1, y+1)
    smoo(x+1, y  )
    smoo(x+1, y-1)
    smoo(x  , y-1)
    smoo(x-1, y-1)
#undef smoo
    else
      junction_or_jump = true;
    break;

  case 2: default: // nic pillow way
    if (false) { }
#define smoo(a, b) \
    else if ( (_thin[a][b] >= _edge_min_OX) && (!_junction[a][b]) ) Final_followOX(a,b ,xc,yc,grad,0);
    smoo(x-1, y  )
    smoo(x  , y+1)
    smoo(x-1, y+1)
    smoo(x+1, y  )
    smoo(x+1, y+1)
    smoo(x  , y-1)
    smoo(x+1, y-1)
    smoo(x-1, y-1)
#undef smoo
    else
      junction_or_jump = true;
    break;
  } // end switch

  if (!junction_or_jump)
    return;

  // Else see if there is a junction nearby, and record it. The _chain_no
  // variable is used to prevent the same junction being inserted at both
  // ends of the vsl_EdgelChains when reversel occurs next to the junction
  // (in that case there will only be two stored points: the edge and the junction)
  if (false) { }
#define smoo(a, b) \
  else if ( _junction[a][b] && ((xc->size()>2) || (_junction[a][b]!=_chain_no)) ) { \
    xc->push_front(_jx[a][b]); \
    yc->push_front(_jy[a][b]); \
    grad->push_front(_jval); \
    _junction[a][b] = _chain_no; \
  }
  smoo(x  , y-1)
  smoo(x-1, y  )
  smoo(x  , y+1)
  smoo(x+1, y  )
  smoo(x+1, y-1)
  smoo(x-1, y-1)
  smoo(x-1, y+1)
  smoo(x+1, y+1)
#undef smoo
  else if ( _join_flag_OX && (xc->size() > 1) )  {
    // Try to find a pixel to jump to, 
    //  and if successful, follow from it 
    int x_c = xc->front(); xc->pop_front();
    int x_p = xc->front(); xc->pop_front();
    int y_c = yc->front(); yc->pop_front(); 
    int y_p = yc->front(); yc->pop_front();
    int dx = x_c - x_p;
    int dy = y_c - y_p;
    xc->push_front(x_p);  xc->push_front(x_c);
    yc->push_front(y_p);  yc->push_front(y_c);
    int xNew, yNew;
    if (Join_dotsOX(x, y, dx, dy, xNew, yNew))
      Final_followOX(xNew,yNew,xc,yc,grad,0);
  }
}



//-----------------------------------------------------------------------------

//void Set_intsOX(int& int1, int& int2, int val1, int val2) { 
//  int1 = val1;  
//  int2 = val2; 
//}
#define Set_intsOX(d1, d2, v1, v2) ((d1)=(v1), (d2)=(v2))


// -- The point (x, y) is the current curve point; 
// (dx, dy) is the increment in position to (x, y) from the preceding point.
// Based on dx and dy, it looks ahead in the same direction (or failing 
// that,  similar directions) for another point.
// On success, xNew and yNew are set to the coordinates of the point found 
// and true is returned;  otherwise false is returned.
//
int vsl_canny_ox::Join_dotsOX(int x, int y, int dx, int dy, int& xNew, int& yNew)
{
  if ((abs(dx) > 1) || (abs(dy) > 1)) {
    //  If dx or dy is too large (> 1), meaning the last 
    //   point was found by jumping, then jumping again 
    //   will be too unreliable
    return false;
  }

  // Make sure that we do not overun the border of the image
  if ( (x<=1) || (x>=_xsize-2) || (y<=1) || (y>=_ysize-2) )
    return false;

  if (!dx && (abs(dy) == 1)) {
    if      (_thin[x  ][y+2*dy] >= _edge_min_OX) Set_intsOX(xNew, yNew, x  , y+2*dy);
    else if (_thin[x+1][y+2*dy] >= _edge_min_OX) Set_intsOX(xNew, yNew, x+1, y+2*dy);
    else if (_thin[x-1][y+2*dy] >= _edge_min_OX) Set_intsOX(xNew, yNew, x-1, y+2*dy);
    else if (_thin[x+2][y+2*dy] >= _edge_min_OX) Set_intsOX(xNew, yNew, x+2, y+2*dy);
    else if (_thin[x-2][y+2*dy] >= _edge_min_OX) Set_intsOX(xNew, yNew, x-2, y+2*dy);
    else if (_thin[x+2][y+  dy] >= _edge_min_OX) Set_intsOX(xNew, yNew, x+2, y+dy  );
    else if (_thin[x-2][y+  dy] >= _edge_min_OX) Set_intsOX(xNew, yNew, x-2, y+dy  );
    else if (_thin[x+2][y     ] >= _edge_min_OX) Set_intsOX(xNew, yNew, x+2, y     );
    else if (_thin[x-2][y     ] >= _edge_min_OX) Set_intsOX(xNew, yNew, x-2, y     );
    else return false;
  }
  else if ((abs(dx) == 1) && !dy) {
    if      (_thin[x+2*dx][y  ] >= _edge_min_OX) Set_intsOX(xNew, yNew, x+2*dx, y  );
    else if (_thin[x+2*dx][y+1] >= _edge_min_OX) Set_intsOX(xNew, yNew, x+2*dx, y+1);
    else if (_thin[x+2*dx][y-1] >= _edge_min_OX) Set_intsOX(xNew, yNew, x+2*dx, y-1);
    else if (_thin[x+2*dx][y+2] >= _edge_min_OX) Set_intsOX(xNew, yNew, x+2*dx, y+2);
    else if (_thin[x+2*dx][y-2] >= _edge_min_OX) Set_intsOX(xNew, yNew, x+2*dx, y-2);
    else if (_thin[x+  dx][y+2] >= _edge_min_OX) Set_intsOX(xNew, yNew, x+  dx, y+2);
    else if (_thin[x+  dx][y-2] >= _edge_min_OX) Set_intsOX(xNew, yNew, x+  dx, y-2);
    else if (_thin[x     ][y+2] >= _edge_min_OX) Set_intsOX(xNew, yNew, x     , y+2);
    else if (_thin[x     ][y-2] >= _edge_min_OX) Set_intsOX(xNew, yNew, x     , y-2);
    else return false;
  }
  else if (abs(dx*dy) == 1) {
    if      (_thin[x+2*dx][y+2*dy] >= _edge_min_OX) Set_intsOX(xNew, yNew, x+2*dx, y+2*dy);
    else if (_thin[x+2*dx][y+  dy] >= _edge_min_OX) Set_intsOX(xNew, yNew, x+2*dx, y+  dy);
    else if (_thin[x+  dx][y+2*dy] >= _edge_min_OX) Set_intsOX(xNew, yNew, x+  dx, y+2*dy);
    else if (_thin[x+2*dx][y     ] >= _edge_min_OX) Set_intsOX(xNew, yNew, x+2*dx, y     );
    else if (_thin[x     ][y+2*dy] >= _edge_min_OX) Set_intsOX(xNew, yNew, x     , y+2*dy);
    else if (_thin[x+2*dx][y-  dy] >= _edge_min_OX) Set_intsOX(xNew, yNew, x+2*dx, y-  dy);
    else if (_thin[x-  dx][y+2*dy] >= _edge_min_OX) Set_intsOX(xNew, yNew, x-  dx, y+2*dy);
    else return false;
  }
  else
    return false;             // Should never be reached, but just in case...

  return true;
}



//-----------------------------------------------------------------------------
//
// -- Multiply image by scale, and clip at 255.
// 
void vsl_canny_ox::Scale_imageOX(float **image, float scale)
{
  for (int x=0; x<_xsize; ++x) 
    for (int y=0; y<_ysize; ++y) 
      image[x][y] = vnl_math_min( (int) (image[x][y]*scale), 255 );
}



//-----------------------------------------------------------------------------
//
// -- Set size of pixels around image (border) to value, so follow can't 
// overrun.
// 
void vsl_canny_ox::Set_image_borderOX(float **image, int border_size, float value) {
  for (int i=0; i<border_size; ++i) {
    for (int x=0; x<_xsize; ++x) 
      image[x][i] = image[x][_ysize-1-i] = value;
    for (int y=0; y<_ysize; ++y) 
      image[i][y] = image[_xsize-1-i][y] = value;
  }
}



//-----------------------------------------------------------------------------
//
//  -- Searches for the junctions in the image.
//
void vsl_canny_ox::Find_junctionsOX() { 

  // Reset the junction variables
  _xjunc->clear();     _yjunc->clear();
  vsl_canny_base_fill_raw_image(_junction, _xsize, _ysize, 0);
  
  for (int x=_border_size_OX; x<_xsize -_border_size_OX; ++x) 
    for (int y=_border_size_OX; y<_ysize-_border_size_OX; ++y) {
      if ( _thin[x][y] < _edge_min_OX )
	continue;

      int a = ( _thin[x-1][y-1] >= _edge_min_OX ) ? 1 : 0;
      a +=    ( _thin[x  ][y-1] >= _edge_min_OX ) ? 1 : 0;
      a +=    ( _thin[x+1][y-1] >= _edge_min_OX ) ? 1 : 0;
      a +=    ( _thin[x+1][y  ] >= _edge_min_OX ) ? 1 : 0;
      a +=    ( _thin[x+1][y+1] >= _edge_min_OX ) ? 1 : 0;
      a +=    ( _thin[x  ][y+1] >= _edge_min_OX ) ? 1 : 0;
      a +=    ( _thin[x-1][y+1] >= _edge_min_OX ) ? 1 : 0;
      a +=    ( _thin[x-1][y  ] >= _edge_min_OX ) ? 1 : 0;

      if ( a > 2 )	  {
	_xjunc->push_front(x);  _yjunc->push_front(y);
	_junction[x][y] = 1;
      }
    }
}



//-----------------------------------------------------------------------------
//
// -- Locate junction clusters using the following method of hysteresis.
//
void vsl_canny_ox::Find_junction_clustersOX() { 

  vcl_list<int> xvertices,yvertices,xjunc,yjunc;

  // Find a junction and follow
  xvertices.clear();  yvertices.clear();
  xjunc.clear();      yjunc.clear();
  for (int x=_border_size_OX; x<_xsize-_border_size_OX; ++x) 
    for (int y=_border_size_OX; y<_ysize-_border_size_OX; ++y) 
      if ( _junction[x][y] )	{
	// Each cluster is written to (xcoords,ycooords)
	vcl_list<int> xcoords,ycoords;
	Follow_junctions(_junction, x,y,&xcoords,&ycoords);

	// Find the `centre' of the cluster. This is defined as the
	// junction closest to the centre of gravity of the cluster
	int x0,y0;
	Cluster_centre_of_gravity(_jx, _jy, xcoords,ycoords,x0,y0);

	// Add both the junctions and the new cluster centre to
	// the main lists
	xvertices.push_front(x0);       
	yvertices.push_front(y0);
	xjunc.insert(xjunc.begin(), xcoords.begin(), xcoords.end()); //xjunc.prepend(xcoords);   
	yjunc.insert(yjunc.begin(), ycoords.begin(), ycoords.end()); //yjunc.prepend(ycoords);
      }

  // Reset the junction image - this is order dependent because
  // the cluster centres appear in both lists
  // xjunc.reset();  yjunc.reset();
  while ( xjunc.size() ) {
    _junction[xjunc.front()][yjunc.front()] = 1;
    xjunc.pop_front();
    yjunc.pop_front();
  }

  // Construct the list of junction cluster centres
  typedef vcl_list<int>::iterator it;
  for (it i=xvertices.begin(), j=yvertices.begin(); i!=xvertices.end() && j!=yvertices.end(); ++i, ++j) {
    vsl_Vertex *v = new vsl_Vertex( (*i) + _xstart, (*j) + _ystart);
    _vlist->push_front(v);
    _junction[*i][*j] = 2;
  }
}
