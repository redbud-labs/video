//---------------------------------------------------------------------------
// This section contains configuration settings for the Stereo Spin appliaction.
// It is used to make it possible to compile and link the code when one or
// more of the camera- or file- driver libraries are unavailable. First comes
// a list of definitions.  These should all be defined when building the
// program for distribution.  Following that comes a list of paths and other
// things that depend on these definitions.  They may need to be changed
// as well, depending on where the libraries were installed.

// XXXX Add stereo support
// XXXX Add responses for + and - to change disparity
// XXXX Update instructions with new commands.

#ifdef _WIN32
#define	VST_USE_DIRECTX
#endif

// END configuration section.
//---------------------------------------------------------------------------

// This pragma tells the compiler not to tell us about truncated debugging info
// due to name expansion within the string, list, and vector classes.
#pragma warning( disable : 4786 4995 4996 )

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tcl.h>
#include <tk.h>
#ifdef _WIN32
#include "Tcl_Linkvar.h"
#else
#include "Tcl_Linkvar85.h"
#endif
#include "directx_camera_server.h"
#include "directx_videofile_server.h"
#include "file_stack_server.h"
#include "image_wrapper.h"
#ifdef	_WIN32
#include <windows.h>
#endif
#include <GL/gl.h>
#include <GL/glut.h>
#include "controllable_video.h"

#include <quat.h>

#include <list>
#include <vector>
#include <algorithm>
using namespace std;

//#define	DEBUG

static void cleanup();
static void dirtyexit();

#ifndef	M_PI
#ifndef M_PI_DEFINED
const double M_PI = 2*asin(1.0);
#define M_PI_DEFINED
#endif
#endif

//--------------------------------------------------------------------------
// Version string for this program
const char *Version_string = "01.00";

//--------------------------------------------------------------------------
// Glut wants to take over the world when it starts, so we need to make
// global access to the objects we will be using.

Tcl_Interp	    *g_tk_control_interp;

char		    *g_device_name = NULL;	  //< Name of the camera/video/file device to open
base_camera_server  *g_camera;			  //< Camera used to get an image
unsigned            g_camera_bit_depth = 8;       //< Bit depth of the particular camera

image_wrapper       *g_image;                     //< Image, possibly from camera and possibly computed
copy_of_image	    *g_last_image = NULL;	  //< Copy of the last image we had, if any

Controllable_Video  *g_video = NULL;		  //< Video controls, if we have them
#ifdef _WIN32
Tclvar_int_with_button	g_window_offset_x("window_offset_x",NULL,0);  //< Offset windows more in some arch
Tclvar_int_with_button	g_window_offset_y("window_offset_y",NULL,0);  //< Offset windows more in some arch
#else
Tclvar_int_with_button	g_window_offset_x("window_offset_x",NULL,25);  //< Offset windows more in some arch
Tclvar_int_with_button	g_window_offset_y("window_offset_y",NULL,-10);  //< Offset windows more in some arch
#endif

int		    g_tracking_window;		  //< Glut window displaying tracking
unsigned char	    *g_glut_image = NULL;	  //< Pointer to the storage for the image
vector<GLuint>      g_texture_ids;                //< Keeps track of the texture IDs
int                 g_which_image = 0;            //< Which image to show now?
int                 g_image_delta = 1;            //< How much to add to get to the next image to show?
bool                g_spin_left = true;           //< Does the movie spin towards the left?
float               g_exposure = 10;              //< Not used, but must pass to camera.

bool		    g_ready_to_display = false;	  //< Don't unless we get an image
bool		    g_already_posted = false;	  //< Posted redisplay since the last display?
int		    g_mousePressX, g_mousePressY; //< Where the mouse was when the button was pressed
int                 g_mousePressImage;            //< What image number was the button pressed on?
int		    g_whichDragAction;		  //< What action to take for mouse drag

bool                g_quit_at_end_of_video = false; //< When we reach the end of the video, should we quit?

bool                g_use_gui = true;             //< Use 3D and 2D GUI?

bool g_gotNewFrame = false;

//--------------------------------------------------------------------------
// Tcl controls and displays
#ifdef	_WIN32
void  device_filename_changed(char *newvalue, void *);
#else
void  device_filename_changed(const char *newvalue, void *);
#endif
Tclvar_int_with_button	g_quit("quit",NULL);
bool g_video_valid = false; // Do we have a valid video frame in memory?

//--------------------------------------------------------------------------
// Return the length of the named file.  Used to help figure out what
// kind of raw file is being opened.  Returns -1 on failure.
// NOTE: Some of our files are longer than 2GB, which means that
// a 32-bit long will wrap and so be unable to determine the file
// length.
#ifndef	_WIN32
  #define __int64 long
  #define _fseeki64 fseek
  #define _ftelli64 ftell
#endif
static __int64 determine_file_length(const char *filename)
{
#ifdef	_WIN32
  FILE *f = fopen(filename, "rb");
#else
  FILE *f = fopen(filename, "r");
#endif
  if (f == NULL) {
    perror("determine_file_length(): Could not open file for reading");
    return -1;
  }
  __int64 val;
  if ( (val = _fseeki64(f, 0, SEEK_END)) != 0) {
    fprintf(stderr, "determine_file_length(): fseek() returned %ld", val);
    fclose(f);
    return -1;
  }
  __int64 length = _ftelli64(f);
  fclose(f);
  return length;
}

//--------------------------------------------------------------------------
// Helper routine to get the Y coordinate right when going between camera
// space and openGL space.
double	flip_y(double y)
{
  return g_image->get_num_rows() - 1 - y;
}

//--------------------------------------------------------------------------
// Glut callback routines.

void drawStringAtXY(double x, double y, char *string)
{
  void *font = GLUT_BITMAP_TIMES_ROMAN_24;
  int len, i;

  glRasterPos2f(x, y);
  len = (int) strlen(string);
  for (i = 0; i < len; i++) {
    glutBitmapCharacter(font, string[i]);
  }
}


// This is called when someone kills the task by closing Glut or some
// other means we don't have control over.  If we try to delete the VRPN
// objects here, we get a seg fault for some reason.  VRPN must have already
// cleaned up our stuff for us.
static void  dirtyexit(void)
{
  static bool did_already = false;
  g_quit = 1;  // Lets all the threads know that it is time to exit.

  if (did_already) {
    return;
  } else {
    did_already = true;
  }

  // Done with the camera and other objects.
  printf("Exiting\n");

  // Get rid of any trackers.
  if (g_camera) { delete g_camera; g_camera = NULL; }
  if (g_glut_image) { delete [] g_glut_image; g_glut_image = NULL; };
}

static void  cleanup(void)
{
  static bool cleaned_up_already = false;

  if (cleaned_up_already) {
    return;
  } else {
    cleaned_up_already = true;
  }

  // Done with the camera and other objects.
  printf("Cleanly ");

  // Do the dirty-exit stuff, then clean up VRPN stuff.
  dirtyexit();
}

static	double	timediff(struct timeval t1, struct timeval t2)
{
	return (t1.tv_usec - t2.tv_usec) / 1e6 +
	       (t1.tv_sec - t2.tv_sec);
}

GLenum check_for_opengl_errors(const char *msg)
{
  GLenum  errcode;
  if ( (errcode = glGetError()) != GL_NO_ERROR) {
    switch (errcode) {
        case GL_INVALID_ENUM:
            fprintf(stderr,"%s:Warning: GL error GL_INVALID_ENUM occurred\n", msg);
            break;
        case GL_INVALID_VALUE:
            fprintf(stderr,"%s:Warning: GL error GL_INVALID_VALUE occurred\n", msg);
            break;
        case GL_INVALID_OPERATION:
            fprintf(stderr,"%s:Warning: GL error GL_INVALID_OPERATION occurred\n", msg);
            break;
        case GL_STACK_OVERFLOW:
            fprintf(stderr,"%s:Warning: GL error GL_STACK_OVERFLOW occurred\n", msg);
            break;
        case GL_STACK_UNDERFLOW:
            fprintf(stderr,"%s:Warning: GL error GL_STACK_UNDERFLOW occurred\n", msg);
            break;
        case GL_OUT_OF_MEMORY:
            fprintf(stderr,"%s:Warning: GL error GL_OUT_OF_MEMORY occurred\n", msg);
            break;
        default:
            fprintf(stderr,"%s:Warning: GL error (code 0x%x) occurred\n", msg, errcode);
    }
  }
  // This will be GL_NO_ERROR (0) if no error was found.
  return errcode;
}

void myDisplayFunc(void)
{
  if (!g_ready_to_display) { return; }

  // Clear the window and prepare to draw in the back buffer
  glDrawBuffer(GL_BACK);
  glClearColor(0.0, 0.0, 0.0, 0.0);
  glClear(GL_COLOR_BUFFER_BIT);

  //--------------------------------------------------------------------
  // Display whichever image we're supposed to, based on the settings
  // for current image, then advance the image however we're supposed
  // to, based on the delta-image value.  These are controlled by the
  // mouse and keyboard events.
  // Cycle through the available images, displaying them one at at time.

  if (g_which_image < static_cast<int>(g_texture_ids.size())) {

    // Store away our state so that we can return it to normal when
    // we're done and not mess up other rendering.
    glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT | GL_PIXEL_MODE_BIT);

    // Figure out if the movie spins to the right or to the left.  If to the left,
    // which is the default, then play the movie normally.  If to the right, then
    // we leave all of the controls the same but at the last minute switch the
    // direction we move through the images, for both the left and right eyes.
    int left_eye = g_which_image;
    if (!g_spin_left) {
      left_eye = (g_texture_ids.size()-1) - left_eye;
    }

    // Enable 2D texture-mapping so that we can do our thing.
    // Bind the appropriate texture
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_texture_ids[left_eye]);

    // Set the clamping behavior and such, which should not have any
    // effect because we should always be at the right scale
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );

    // Write the texture to the quad in the appropriate orientation for
    // this camera.
    g_camera->write_opengl_texture_to_quad();

    // Put the state back the way it was
    glPopAttrib();
  }

  // Go to the next image we're supposed to show, keeping us in
  // bounds.
  g_which_image += g_image_delta;
  while (g_which_image >= static_cast<int>(g_texture_ids.size())) {
    g_which_image -= g_texture_ids.size();
  }
  while (g_which_image < 0) {
    g_which_image += g_texture_ids.size();
  }

  // Check for OpenGL errors during our action here.
  check_for_opengl_errors("myDisplayFunc after drawing");

  // Swap buffers so we can see it.
  glutSwapBuffers();
  
  // Have no longer posted redisplay since the last display.
  g_already_posted = false;
}

void myIdleFunc(void)
{
  //------------------------------------------------------------
  // This must be done in any Tcl app, to allow Tcl/Tk to handle
  // events.  This must happen at the beginning of the idle function
  // so that the camera-capture and video-display routines are
  // using the same values for the global parameters.

  while (Tk_DoOneEvent(TK_DONT_WAIT)) {};

  //------------------------------------------------------------
  // This is called once every time through the main loop.  It
  // pushes changes in the C variables over to Tcl.

  if (Tclvar_mainloop()) {
    fprintf(stderr,"Tclvar Mainloop failed\n");
  }

  //------------------------------------------------------------------
  // This is called after all of the setup has been done and we've got
  // a frame to render in.  The first time we get here, go ahead and
  // pull in all of the images of the video.  Put them into an array of
  // texture IDs on the graphics card so that they are ready to be rendered
  // onto Quads.  Keep track of how many frames there are so that the slider
  // setting the frame number can be used to pick from among them.  If we
  // run out of texture memory, then say so and quit.
  // XXX Why is this using a ton of system memory for an AVI file (and raw file)?
  //     It must be allocated system memory to match the GPU memory down inside
  //     OpenGL in case it needs to swap...
  static bool loaded_images = false;
  if (!loaded_images) {

    //------------------------------------------------------------------
    // Present a dialog box that lets the user quit if they want to, rather
    // than waiting forever for the video to load.
    char  command[256];
    if (Tcl_Eval(g_tk_control_interp, "show_quit_loading_dialog") != TCL_OK) {
      fprintf(stderr, "Tcl_Eval(%s) failed: %s\n", command, g_tk_control_interp->result);
      cleanup();
      exit(-1);
    } 

    while (g_camera->read_image_to_memory(0,g_camera->get_num_columns()-1,
                                          0,g_camera->get_num_rows()-1, g_exposure)) {

      // Get a new texture ID for this frame.
      GLuint  new_id;
      glGenTextures(1, &new_id);
      g_texture_ids.push_back(new_id);
      glBindTexture(GL_TEXTURE_2D, new_id);

      // Store the video from this frame into a the OpengL texture whose ID has
      // just been created.
      if (!g_camera->write_to_opengl_texture(new_id)) {
        fprintf(stderr,"Could not write frame %d to texture ID %d\n",
          g_texture_ids.size()-1, new_id);
        dirtyexit();
      } else {
        printf("Wrote image to OpenGL texture %d\n", new_id);
      }
      if (check_for_opengl_errors("main() after writing texture") != GL_NO_ERROR) {
        fprintf(stderr, "Failed to read the video into OpenGL for rendering\n");
        dirtyexit();
      }

      // Display the loaded images in a movie loop while they are coming in,
      // to give the user something to watch while it is loading.
      g_ready_to_display = true;
      myDisplayFunc();

      // Handle Tcl events, so the user can quit if they want to.
      // See if they asked us to quit.
      while (Tk_DoOneEvent(TK_DONT_WAIT)) {};
      if (g_quit) {
        cleanup();
        exit(0);
      }
    }

    // Don't need the dialog box anymore -- the images are all loaded.
    if (Tcl_Eval(g_tk_control_interp, "remove_quit_loading_dialog") != TCL_OK) {
      fprintf(stderr, "Tcl_Eval(%s) failed: %s\n", command, g_tk_control_interp->result);
      cleanup();
      exit(-1);
    } 

    // We're ready to display an image.
    g_ready_to_display = true;
    myDisplayFunc();

    loaded_images = true;
  }

  // Point the image to use at the camera's image.
  g_image = g_camera;

  //------------------------------------------------------------
  // Post a redisplay so that Glut will draw the new image
  if (g_use_gui && !g_already_posted) {
    glutSetWindow(g_tracking_window);
    glutPostRedisplay();
    g_already_posted = true;
  }

  //------------------------------------------------------------
  // Time to quit?
  if (g_quit) {
    cleanup();
    exit(0);
  }
  
  // Sleep a little while so that we don't eat the whole CPU.
#ifdef	_WIN32
  vrpn_SleepMsecs(0);
#else
  vrpn_SleepMsecs(1);
#endif
}

void keyboardCallbackForGLUT(unsigned char key, int x, int y)
{
  switch (key) {
  case 'q':
  case 'Q':
    g_quit = 1;
    break;

  case '>': // Spin faster in the negative direction
  case '.':
    g_image_delta -= 1;
    break;

  case '<': // Spin faster in the positive direction
  case ',':
    g_image_delta += 1;
    break;

  case ' ': // Stop spinning
    g_image_delta = 0;
    break;

  case 'r': // The movie spins towards the right
  case 'R':
    g_spin_left = false;
    break;

  case 'l': // The movie spins towards the left
  case 'L':
    g_spin_left = true;
    break;

  case 8:   // Backspace
  case 127: // Delete on Windows
     // Nothing to do for now.
    break;

  default:
    printf("Unrecognized key: %c (%d)\n", key, key);
  }
}

void mouseCallbackForGLUT(int button, int state, int raw_x, int raw_y)
{
  // Record where the button was pressed for use in the motion
  // callback, flipping the Y axis to make the coordinates match
  // image coordinates.
  g_mousePressX = raw_x;
  g_mousePressY = raw_y;

  // Convert from raw device coordinates to image coordinates.  This
  // means scaling by the ratio of window size to image size.  If the
  // user has not resized the window, and if the image was not too small
  // then this will be 1; otherwise it will be different in general.
  double xScale = (g_camera->get_num_columns() - 1.0) / (glutGet(GLUT_WINDOW_WIDTH) - 1.0);
  double yScale = (g_camera->get_num_rows() - 1.0) / (glutGet(GLUT_WINDOW_HEIGHT) - 1.0);
  int x = xScale * raw_x;
  int y = flip_y( yScale * raw_y);

  switch(button) {
    // The left button will create a new tracker and let the
    // user specify its radius if they move far enough away
    // from the pick point (it starts with a default of the same
    // as the current active tracker).
    // The new tracker becomes the active tracker.
    case GLUT_LEFT_BUTTON:
      if (state == GLUT_DOWN) {
        // Left button pans window.  Stop any automatic spinning that
        // is going on.
        g_whichDragAction = 1;
        g_image_delta = 0;
        g_mousePressImage = g_which_image;
      } else {
        // We're done dragging.
        g_whichDragAction = 0;
      }
      break;

    case GLUT_MIDDLE_BUTTON:
      if (state == GLUT_DOWN) {
	g_whichDragAction = 0;
      }
      break;

    // The right button will pull the closest existing tracker
    // to the location where the mouse button was pressed, and
    // then let the user pull it around the screen
    case GLUT_RIGHT_BUTTON:
      if (state == GLUT_DOWN) {
	g_whichDragAction = 2;
      }
      break;
  }
}

void motionCallbackForGLUT(int raw_x, int raw_y)
{
  // Convert from raw device coordinates to image coordinates.  This
  // means scaling by the ratio of window size to image size.  If the
  // user has not resized the window, and if the image was not too small
  // then this will be 1; otherwise it will be different in general.
  double xScale = (g_camera->get_num_columns() - 1.0) / (glutGet(GLUT_WINDOW_WIDTH) - 1.0);
  double yScale = (g_camera->get_num_rows() - 1.0) / (glutGet(GLUT_WINDOW_HEIGHT) - 1.0);
  int x = xScale * raw_x;
  int y = flip_y( yScale * raw_y);
  int pressX = xScale * g_mousePressX;
  int pressY = flip_y( yScale * g_mousePressY );

  switch (g_whichDragAction) {

  case 0: //< Do nothing on drag.
    break;

  case 1: {
      // Change the viewed image by an amount such that panning over the
      // whole window in X will spin all the way around (show all of the frames).
      int x_motion_in_pixels = g_mousePressX - raw_x;
      xScale = (g_texture_ids.size()*1.0)/glutGet(GLUT_WINDOW_WIDTH);
      int delta_frames = x_motion_in_pixels * xScale;
      g_which_image = g_mousePressImage + delta_frames;
      while (g_which_image >= static_cast<int>(g_texture_ids.size())) {
        g_which_image -= g_texture_ids.size();
      }
      while (g_which_image < 0) {
        g_which_image += g_texture_ids.size();
      }
    }
    break;

  default:
    fprintf(stderr,"Internal Error: Unknown drag action (%d)\n", g_whichDragAction);
  }
  return;
}


//--------------------------------------------------------------------------
// Tcl callback routines.

// If the device filename becomes non-empty, then set the global
// device name to match what it is set to.

#ifdef _WIN32
void  device_filename_changed(char *newvalue, void *)
#else
void  device_filename_changed(const char *newvalue, void *)
#endif
{
  // Set the global name, if we have a non-empty name.
  if (strlen(newvalue) > 0) {
    g_device_name = new char[strlen(newvalue)+1];
    if (g_device_name == NULL) {
	fprintf(stderr,"device_filename_changed(): Out of memory\n");
	dirtyexit();
	exit(-1);
    }
    strcpy(g_device_name, newvalue);
  }
}

//--------------------------------------------------------------------------

void Usage(const char *progname)
{
    fprintf(stderr, "Usage: %s [-nogui] [filename]\n", progname);
    fprintf(stderr, "       filename: The source file for tracking can be specified here (default is\n");
    fprintf(stderr, "                 a dialog box)\n");
    exit(-1);
}

int main(int argc, char *argv[])
{
  // Set up exit handler to make sure we clean things up no matter
  // how we are quit.  We hope that we exit in a good way and so
  // cleanup() gets called, but if not then we do a dirty exit.
  atexit(dirtyexit);

  //------------------------------------------------------------------
  // Generic Tcl startup.  Getting and interpreter and mainwindow.

  char		command[256];
  Tk_Window       tk_control_window;
  g_tk_control_interp = Tcl_CreateInterp();

  /* Start a Tcl interpreter */
  if (Tcl_Init(g_tk_control_interp) == TCL_ERROR) {
          fprintf(stderr,
                  "Tcl_Init failed: %s\n",g_tk_control_interp->result);
          return(-1);
  }

  /* Start a Tk mainwindow to hold the widgets */
  if (Tk_Init(g_tk_control_interp) == TCL_ERROR) {
	  fprintf(stderr,
	  "Tk_Init failed: %s\n",g_tk_control_interp->result);
	  return(-1);
  }
  tk_control_window = Tk_MainWindow(g_tk_control_interp);
  if (tk_control_window == NULL) {
          fprintf(stderr,"%s\n", g_tk_control_interp->result);
          return(-1);
  }

  //------------------------------------------------------------------
  // Loading the particular definition files we need.  russ_widgets is
  // required by the Tclvar_float_with_scale class.  simple_magnet_drive
  // is application-specific and sets up the controls for the integer
  // and float variables.

  /* Load the Tcl scripts that handle widget definition and
   * variable controls */
  sprintf(command, "source russ_widgets.tcl");
  if (Tcl_Eval(g_tk_control_interp, command) != TCL_OK) {
          fprintf(stderr, "Tcl_Eval(%s) failed: %s\n", command,
                  g_tk_control_interp->result);
          return(-1);
  }

  //------------------------------------------------------------------
  // Put the version number into the main window.
  sprintf(command, "label .versionlabel -text Stereo_Spin_v:%s", Version_string);
  if (Tcl_Eval(g_tk_control_interp, command) != TCL_OK) {
          fprintf(stderr, "Tcl_Eval(%s) failed: %s\n", command,
                  g_tk_control_interp->result);
          return(-1);
  }
  sprintf(command, "pack .versionlabel");
  if (Tcl_Eval(g_tk_control_interp, command) != TCL_OK) {
          fprintf(stderr, "Tcl_Eval(%s) failed: %s\n", command,
                  g_tk_control_interp->result);
          return(-1);
  }

  //------------------------------------------------------------------
  // Load the specialized Tcl code needed by this program.  This must
  // be loaded before the Tclvar_init() routine is called because it
  // puts together some of the windows needed by the variables.
  sprintf(command, "source stereo_spin.tcl");
  if (Tcl_Eval(g_tk_control_interp, command) != TCL_OK) {
          fprintf(stderr, "Tcl_Eval(%s) failed: %s\n", command,
                  g_tk_control_interp->result);
          return(-1);
  }

  //------------------------------------------------------------------
  // This routine must be called in order to initialize all of the
  // variables that came into scope before the interpreter was set
  // up, and to tell the variables which interpreter to use.  It is
  // called once, after the interpreter exists.

  // Initialize the variables using the interpreter
  if (Tclvar_init(g_tk_control_interp)) {
	  fprintf(stderr,"Can't do init!\n");
	  return -1;
  }
  sprintf(command, "wm geometry . +10+10");
  if (Tcl_Eval(g_tk_control_interp, command) != TCL_OK) {
          fprintf(stderr, "Tcl_Eval(%s) failed: %s\n", command,
                  g_tk_control_interp->result);
          return(-1);
  }

  //------------------------------------------------------------------
  // Parse the command line.  This has to be done after a bunch of the
  // set-up, so that we can create trackers and output files and such.
  // It has to be before the imager set-up because we don't know what
  // source to use until after we parse this.
  int	i, realparams;		  // How many non-flag command-line arguments
  realparams = 0;
  // These defaults are set for a Pulnix camera

  for (i = 1; i < argc; i++) {
    if (!strncmp(argv[i], "-nogui", strlen("-nogui"))) {
      g_use_gui = false;
    } else if (argv[i][0] == '-') {
      Usage(argv[0]);
    } else {
      switch (++realparams) {
      case 1:
        // Filename argument: open the file specified.
        g_device_name = argv[i];
        break;

      default:
        Usage(argv[0]);
      }
    }
  }

  //------------------------------------------------------------
  // This pushes changes in the C variables over to Tcl and then
  // calls any resulting callbacks (handles the commands set during
  // the command-line parsing).

  while (Tk_DoOneEvent(TK_DONT_WAIT)) {};
  if (Tclvar_mainloop()) {
    fprintf(stderr,"Tclvar Mainloop failed\n");
    return -1;
  }
  while (Tk_DoOneEvent(TK_DONT_WAIT)) {};

  //------------------------------------------------------------------
  // If we don't have a device name, then throw a Tcl dialog asking
  // the user for the name of a file to use and wait until they respond.
  if (g_device_name == NULL) {

    //------------------------------------------------------------
    // Create a callback for a variable that will hold the device
    // name and then create a dialog box that will ask the user
    // to either fill it in or quit.
    Tclvar_selector filename("device_filename", NULL, NULL, "", device_filename_changed, NULL);
    if (Tcl_Eval(g_tk_control_interp, "ask_user_for_filename") != TCL_OK) {
      fprintf(stderr, "Tcl_Eval(%s) failed: %s\n", command, g_tk_control_interp->result);
      cleanup();
      exit(-1);
    }

    do {
      //------------------------------------------------------------
      // This pushes changes in the C variables over to Tcl.

      while (Tk_DoOneEvent(TK_DONT_WAIT)) {};
      if (Tclvar_mainloop()) {
	fprintf(stderr,"Tclvar Mainloop failed\n");
      }
    } while ( (g_device_name == NULL) && !g_quit);
    if (g_quit) {
      cleanup();
      exit(0);
    }
  }

  //------------------------------------------------------------------
  // Open the camera.  If we have a video file, then
  // set up the Tcl controls to run it.  Also, report the frame number.
  float exposure = g_exposure;
  if (!get_camera(g_device_name, &g_camera_bit_depth, &exposure,
                  &g_camera, &g_video, 648,484,1,0,0)) {
    fprintf(stderr,"Cannot open camera\n");
    if (g_camera) { delete g_camera; g_camera = NULL; }
    cleanup();
    exit(-1);
  }
  g_exposure = exposure;

  // Verify that the camera is working.
  if (!g_camera->working()) {
    fprintf(stderr,"Could not establish connection to camera\n");
    if (g_camera) { delete g_camera; g_camera = NULL; }
    cleanup();
    exit(-1);
  }
  g_image = g_camera;

  //------------------------------------------------------------------
  // Initialize GLUT and create the window that will display the
  // video -- name the window after the device that has been
  // opened in VRPN.  Also set mouse callbacks.
  if (g_use_gui) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
    glutInitWindowPosition(10 + g_window_offset_x, 180 + g_window_offset_y);
    glutInitWindowSize(g_camera->get_num_columns(), g_camera->get_num_rows());
  #ifdef DEBUG
    printf("initializing window to %dx%d\n", g_camera->get_num_columns(), g_camera->get_num_rows());
  #endif
    g_tracking_window = glutCreateWindow(g_device_name);
    glutMotionFunc(motionCallbackForGLUT);
    glutMouseFunc(mouseCallbackForGLUT);
    glutKeyboardFunc(keyboardCallbackForGLUT);
  }

  //------------------------------------------------------------------
  // Set the video to play, so that it will try and read in all of the
  // frames from the video.
  if (g_video) {
    g_video->play();
  } else {
    fprintf(stderr,"Could not play video\n");
    if (g_camera) { delete g_camera; g_camera = NULL; }
    cleanup();
    exit(-1);
  }

  //------------------------------------------------------------------
  // Create the buffer that Glut will use to send to the tracking window.  This is allocating an
  // RGBA buffer.  It needs to be 4-byte aligned, so we allocated it as a group of
  // words and then cast it to the right type.  We're using RGBA rather than just RGB
  // because it also solves the 4-byte alignment problem caused by funky sizes of image
  // that are RGB images.
  if ( (g_glut_image = (unsigned char *)(void*)new vrpn_uint32
      [g_camera->get_num_columns() * g_camera->get_num_rows()]) == NULL) {
    fprintf(stderr,"Out of memory when allocating image!\n");
    fprintf(stderr,"  (Image is %u by %u)\n", g_camera->get_num_columns(), g_camera->get_num_rows());
    if (g_camera) { delete g_camera; g_camera = NULL; }
    cleanup();
    exit(-1);
  }

  //------------------------------------------------------------------
  // Set the display functions for each window and idle function for GLUT (they
  // will do all the work) and then give control over to GLUT.
  if (g_use_gui) {
    glutSetWindow(g_tracking_window);
    glutDisplayFunc(myDisplayFunc);
    glutShowWindow();
    glutIdleFunc(myIdleFunc);
  }

  if (g_use_gui) {
    glutMainLoop();
  } else {
    while (true) { myIdleFunc(); }
  }
  // glutMainLoop() NEVER returns.  Wouldn't it be nice if it did when Glut was done?
  // Nope: Glut calls exit(0);

  return 0;
}