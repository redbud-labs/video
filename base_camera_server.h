#ifndef	BASE_CAMERA_SERVER_H
#define	BASE_CAMERA_SERVER_H

#include <stdio.h>   // For NULL
#include <math.h>    // For floor()
#include  <vrpn_Types.h>
#include  <vrpn_Connection.h>
#include  <vrpn_TempImager.h>

//----------------------------------------------------------------------------
// This class forms a basic wrapper for an image.  It treats an image as anything
// which can support requests on the number of pixels in an image and can
// perform queries by pixel coordinate in that image.  It is a set of functions
// that are needed by the spot_tracker applications.

class image_wrapper {
public:
  // Tell what the range is for the image.
  virtual void	read_range(int &minx, int &maxx, int &miny, int &maxy) const = 0;

  /// Return the number of colors that the image has
  virtual unsigned  get_num_colors() const = 0;

  /// Read a pixel from the image into a double; return true if the pixel
  // was in the image, false if it was not.
  virtual bool	read_pixel(int x, int y, double	&result, unsigned rgb = 0) const = 0;

  /// Read a pixel from the image into a double; Don't check boundaries.
  virtual double read_pixel_nocheck(int x, int y, unsigned rgb = 0) const = 0;

  // Do bilinear interpolation to read from the image, in order to
  // smoothly interpolate between pixel values.
  // All sorts of speed tweaks in here because it is in the inner loop for
  // the spot tracker and other codes.
  // Return a result of zero and false if the coordinate its outside the image.
  // Return the correct interpolated result and true if the coordinate is inside.
  inline bool	read_pixel_bilerp(double x, double y, double &result, unsigned rgb = 0) const
  {
    result = 0;	// In case of failure.
    // The order of the following statements is optimized for speed.
    // The double version is used below for xlowfrac comp, ixlow also used later.
    // Slightly faster to explicitly compute both here to keep the answer around.
    double xlow = floor(x); int ixlow = (int)xlow;
    // The double version is used below for ylowfrac comp, ixlow also used later
    // Slightly faster to explicitly compute both here to keep the answer around.
    double ylow = floor(y); int iylow = (int)ylow;
    int ixhigh = ixlow+1;
    int iyhigh = iylow+1;
    double xhighfrac = x - xlow;
    double yhighfrac = y - ylow;
    double xlowfrac = 1.0 - xhighfrac;
    double ylowfrac = 1.0 - yhighfrac;
    double ll, lh, hl, hh;

    // Combining the if statements into one using || makes it slightly slower.
    // Interleaving the result calculation with the returns makes it slower.
    if (!read_pixel(ixlow, iylow, ll, rgb)) { return false; }
    if (!read_pixel(ixlow, iyhigh, lh, rgb)) { return false; }
    if (!read_pixel(ixhigh, iylow, hl, rgb)) { return false; }
    if (!read_pixel(ixhigh, iyhigh, hh, rgb)) { return false; }
    result = ll * xlowfrac * ylowfrac + 
	     lh * xlowfrac * yhighfrac +
	     hl * xhighfrac * ylowfrac +
	     hh * xhighfrac * yhighfrac;
    return true;
  };

  // Do bilinear interpolation to read from the image, in order to
  // smoothly interpolate between pixel values.
  // All sorts of speed tweaks in here because it is in the inner loop for
  // the spot tracker and other codes.
  // Does not check boundaries to make sure they are inside the image.
  inline double read_pixel_bilerp_nocheck(double x, double y, unsigned rgb = 0) const
  {
    // The order of the following statements is optimized for speed.
    // The double version is used below for xlowfrac comp, ixlow also used later.
    // Slightly faster to explicitly compute both here to keep the answer around.
    double xlow = floor(x); int ixlow = (int)xlow;
    // The double version is used below for ylowfrac comp, ixlow also used later
    // Slightly faster to explicitly compute both here to keep the answer around.
    double ylow = floor(y); int iylow = (int)ylow;
    int ixhigh = ixlow+1;
    int iyhigh = iylow+1;
    double xhighfrac = x - xlow;
    double yhighfrac = y - ylow;
    double xlowfrac = 1.0 - xhighfrac;
    double ylowfrac = 1.0 - yhighfrac;

    // Combining the if statements into one using || makes it slightly slower.
    // Interleaving the result calculation with the returns makes it slower.
    return read_pixel_nocheck(ixlow, iylow, rgb) * xlowfrac * ylowfrac +
	   read_pixel_nocheck(ixlow, iyhigh, rgb) * xlowfrac * yhighfrac +
	   read_pixel_nocheck(ixhigh, iylow, rgb) * xhighfrac * ylowfrac +
	   read_pixel_nocheck(ixhigh, iyhigh, rgb) * xhighfrac * yhighfrac;
  };

  /// Store the memory image to a PPM file.
  virtual bool  write_to_ppm_file(const char *filename, int gain = 1, bool sixteen_bits = false) const {return false;};

  /// Store the portion of image in memory to a TIFF file (subclasses may override this to make it more efficient,
  // but we provide an instance in the C file).
  virtual bool  write_to_tiff_file(const char *filename, double gain = 1.0, bool sixteen_bits = false,
    const char *magick_files_dir = "C:/nsrg/external/pc_win32/bin/ImageMagick-5.5.7-Q16/MAGIC_DIR_PATH") const;

  /// Send whole image over a vrpn connection
  virtual bool  send_vrpn_image(vrpn_TempImager_Server* svr,vrpn_Synchronized_Connection* svrcon,double g_exposure,int svrchan) {return false;};
};

//----------------------------------------------------------------------------
// Concrete version of above virtual base class that creates itself by copying
// from an existing image.

class copy_of_image: public image_wrapper {
public:
  copy_of_image(const image_wrapper &copyfrom);
  ~copy_of_image();

  // Tell what the range is for the image.
  virtual void	read_range(int &minx, int &maxx, int &miny, int &maxy) const;

  /// Return the number of colors that the image has
  virtual unsigned  get_num_colors() const { return _numcolors; };

  /// Read a pixel from the image into a double; return true if the pixel
  // was in the image, false if it was not.
  virtual bool	read_pixel(int x, int y, double	&result, unsigned rgb = 0) const;

  /// Read a pixel from the image into a double; Don't check boundaries.
  virtual double read_pixel_nocheck(int x, int y, unsigned rgb = 0) const;

  /// Copy new values from the image that is passed in, reallocating if needed
  void	operator= (const image_wrapper &copyfrom);

protected:
  int _minx, _maxx, _miny, _maxy;   //< Coordinates for the pixels (copied from other image)
  int _numx, _numy;		    //< Calculated based on the above min/max values
  int _numcolors;		    //< How many colors do we have
  double  *_image;		    //< Holds the values copied from the other image

  inline int index(int x, int y, unsigned rgb) const {
    int xindex = x - _minx;
    int yindex = y - _miny;
    return rgb + get_num_colors() * (xindex + _numx * yindex);
  };
};

//----------------------------------------------------------------------------
// Concrete version of above virtual base class that applies a translation
// to the pixel coordinates for another image and lets you resample it.  It does a
// bilinear interpolation on the pixel coordinates to get the values from the
// other image.  It is a faster implementation than the more general affine_transformed
// image defined below.

class translated_image: public image_wrapper {
public:
  translated_image(const image_wrapper &reference_image, double dx, double dy) :
    _ref(reference_image), _dx(dx), _dy(dy) { };
  virtual ~translated_image() {};

  // Tell what the range is for the image.
  virtual void	read_range(int &minx, int &maxx, int &miny, int &maxy) const
  {
    _ref.read_range(minx, maxx, miny, maxy);
  }

  /// Return the number of colors that the image has
  virtual unsigned  get_num_colors() const { return _ref.get_num_colors(); }

  /// Read a pixel from the image into a double; return true if the pixel
  // was in the image, false if it was not.
  virtual bool	read_pixel(int x, int y, double	&result, unsigned rgb = 0) const
  {
    return _ref.read_pixel_bilerp(newx(x,y), newy(x,y), result, rgb);
  }

  /// Read a pixel from the image into a double; Don't check boundaries.
  virtual double read_pixel_nocheck(int x, int y, unsigned rgb = 0) const
  {
    return _ref.read_pixel_bilerp_nocheck(newx(x,y), newy(x,y), rgb);
  }

protected:
  const	image_wrapper &_ref;	    //< Image to get pixels from
  double  _dx, _dy;		    //< Transform to apply to coordinates

  // These compute the new coordinates from the old coordinates.
  inline  double  newx(double x, double y) const { return x + _dx; }
  inline  double  newy(double x, double y) const { return y + _dy; }
};

//----------------------------------------------------------------------------
// Concrete version of above virtual base class that applies a translation,
// rotation, and uniform scale to the pixel coordinates for another image
// and lets you resample it.  The rotation and scale are performed around
// a specified location in the coordinates of the affine_transform image,
// then the translation is applied in this rotated and scaled space.
// It does a bilinear interpolation on the pixel
// coordinates to get the values from the other image.  Rotation is in
// radians, and a positive rotation rotates the +X axis into the +Y axis.

class affine_transformed_image: public image_wrapper {
public:
  affine_transformed_image(const image_wrapper &reference_image,
			   double dx, double dy,
			   double rotradians, double scale,
			   double centerx = 0, double centery = 0) :
    _ref(reference_image), _dx(dx), _dy(dy), _rot(rotradians), _scale(scale),
    _centerx(centerx), _centery(centery)
  {
      _sinrot = sin(_rot);
      _cosrot = cos(_rot);
  };
  virtual ~affine_transformed_image() {};

  // Tell what the range is for the image.
  virtual void	read_range(int &minx, int &maxx, int &miny, int &maxy) const
  {
    _ref.read_range(minx, maxx, miny, maxy);
  }

  /// Return the number of colors that the image has
  virtual unsigned  get_num_colors() const { return _ref.get_num_colors(); }

  /// Read a pixel from the image into a double; return true if the pixel
  // was in the image, false if it was not.
  virtual bool	read_pixel(int x, int y, double	&result, unsigned rgb = 0) const
  {
    return _ref.read_pixel_bilerp(newx(x,y), newy(x,y), result, rgb);
  }

  /// Read a pixel from the image into a double; Don't check boundaries.
  virtual double read_pixel_nocheck(int x, int y, unsigned rgb = 0) const
  {
    return _ref.read_pixel_bilerp_nocheck(newx(x,y), newy(x,y), rgb);
  }

protected:
  const	image_wrapper &_ref;	    //< Image to get pixels from
  double  _dx, _dy;		    //< Transform to apply to coordinates
  double  _rot;			    //< Rotation in radians to apply to around translated center
  double  _scale;		    //< Scale factor to apply at translated center.
  double  _centerx;		    //< Center of scaling and rotation
  double  _centery;		    //< Center of scaling and rotation
  double  _sinrot;		    //< Sine of the rotation angle
  double  _cosrot;		    //< Cosine of the rotation angle

  // Calculate the direction and length of a step in (x,y) along the
  // rotated and scaled X axis.
  inline double _scale_and_rotate_x_step(double x, double y) const {
    return _scale * (_cosrot*x - _sinrot*y);
  }

  // Calculate the direction and length of a step in (x,y) along the
  // rotated and scaled Y axis.
  inline double _scale_and_rotate_y_step(double x, double y) const {
    return _scale * (_sinrot*x + _cosrot*y);
  }

  // These compute the new coordinates from the old coordinates.
  inline  double  newx(double x, double y) const {
    double x_centered = x - _centerx;
    double y_centered = y - _centery;
    double x_scaled_rot = _scale_and_rotate_x_step(x_centered, y_centered);
    double y_scaled_rot = _scale_and_rotate_y_step(x_centered, y_centered);
    double x_putback = x_scaled_rot + _centerx;
    double y_putback = y_scaled_rot + _centery;
    return x_putback + _dx;
  }
  inline  double  newy(double x, double y) const {
    double x_centered = x - _centerx;
    double y_centered = y - _centery;
    double x_scaled_rot = _scale_and_rotate_x_step(x_centered, y_centered);
    double y_scaled_rot = _scale_and_rotate_y_step(x_centered, y_centered);
    double x_putback = x_scaled_rot + _centerx;
    double y_putback = y_scaled_rot + _centery;
    return y_putback + _dy;
  }
};


//----------------------------------------------------------------------------
// This class forms a basic wrapper for a camera.  It treats an camera as anything
// which can do what is needed for an imager and also includes functions for
// reading images into memory from the camera.

class base_camera_server : public image_wrapper {
public:
  virtual ~base_camera_server(void) {};

  /// Is the camera working properly?
  bool working(void) const { return _status; };

  // These functions should be used to determine the stride in the
  // image when skipping lines.  They are in terms of the full-screen
  // number of pixels with the current binning level.
  unsigned  get_num_rows(void) const { return _num_rows / _binning; };
  unsigned  get_num_columns(void) const { return _num_columns / _binning; };

  /// Read an image to a memory buffer.  Max < min means "whole range".
  /// Setting binning > 1 packs more camera pixels into each image pixel, so
  /// the maximum number of pixels has to be divided by the binning constant
  /// (the effective number of pixels is lower and pixel coordinates are set
  /// in this reduced-count pixel space).  This routine returns false if a
  /// new image could not be read (for example, because of a timeout on
  /// the reading because we're at the end of a video stream).
  virtual bool	read_image_to_memory(unsigned minX = 255, unsigned maxX = 0,
			     unsigned minY = 255, unsigned maxY = 0,
			     double exposure_time = 250.0) = 0;

  /// Get pixels out of the memory buffer, RGB indexes the colors
  virtual bool	get_pixel_from_memory(unsigned X, unsigned Y, vrpn_uint8 &val, int RGB = 0) const = 0;
  virtual bool	get_pixel_from_memory(unsigned X, unsigned Y, vrpn_uint16 &val, int RGB = 0) const = 0;

  /// Read a pixel from the image into a double; return true if the pixel
  // was in the image, false if it was not.
  virtual bool	read_pixel(int x, int y, double	&result, unsigned rgb = 0) const
  { vrpn_uint16 val = 0;
    result = 0.0; // Until we get a better one
    if (get_pixel_from_memory(x, y, val, rgb)) {
      result = val; return true;
    } else { return false; }
  };

  /// Read a pixel from the image into a double; Don't check boundaries.
  virtual double read_pixel_nocheck(int x, int y, unsigned rgb = 0) const
  {
    vrpn_uint16  val = 0;
    get_pixel_from_memory(x, y, val, rgb);
    return val;
  };

  /// Instantiation needed for image_wrapper
  virtual void read_range(int &minx, int &maxx, int &miny, int &maxy) const {
    minx = _minX; miny = _minY; maxx = _maxX; maxy = _maxY;
  }

protected:
  bool	    _status;			//< True is working, false is not
  unsigned  _num_rows, _num_columns;    //< Size of the memory buffer
  unsigned  _minX, _minY, _maxX, _maxY; //< Region of the image in memory
  unsigned  _binning;			//< How many camera pixels compressed into image pixel

  virtual bool	open_and_find_parameters(void) {return false;};
  base_camera_server(unsigned binning = 1) {
    _binning = binning;
    if (_binning < 1) { _binning = 1; }
  };
};

#endif
