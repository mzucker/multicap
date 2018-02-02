#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <string>
#include <string.h>
#include <assert.h>
#include <pylon/PylonIncludes.h>
#include <jpeglib.h>

void msleep(int msec) { usleep(msec * 1000); }

void write_jpg(FILE* fp,
               int quality,
               size_t width,
               size_t height,
               size_t stride,
               const uint8_t* data) {

  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;

  memset(&cinfo, 0, sizeof(cinfo));
    
  cinfo.err = jpeg_std_error(&jerr);

  jpeg_create_compress(&cinfo);
  jpeg_stdio_dest(&cinfo, fp);

  //////////////////////////////////////////////////////////////////////

  cinfo.image_width = width;
  cinfo.image_height = height;

  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality( &cinfo, quality, TRUE );

  jpeg_start_compress( &cinfo, TRUE );

  JSAMPLE* srcptr = (JSAMPLE*)data;
  std::vector<JSAMPLE> bgr_buf;
    
  JSAMPROW row_pointer[1];
    
    
  for (size_t row=0; row<height; ++row) {

    if (true) {
            
      row_pointer[0] = srcptr;
            
    } else {
            
      bgr_buf.resize(3*width);
            
      for (size_t i=0; i<bgr_buf.size(); i+=3) {
	bgr_buf[i+0] = srcptr[i+2];
	bgr_buf[i+1] = srcptr[i+1];
	bgr_buf[i+2] = srcptr[i+0];
      }

      row_pointer[0] = &(bgr_buf[0]);
                
    }

    jpeg_write_scanlines( &cinfo, row_pointer, 1 );
        
    srcptr += stride;
        
  }

  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  
}

//////////////////////////////////////////////////////////////////////

struct ImageData {
  std::string basename;
  Pylon::CPylonImage pylon_image;
};

typedef std::vector<ImageData> ImageDataArray;

class CameraWrapper {
public:


  CameraWrapper();
  ~CameraWrapper();

  bool OpenCameras(int num_cameras,
		   std::string& error_text);

  void CloseCameras();

  size_t GetConnectedCameras() const;

  const std::string& GetCameraID(size_t camera_index) const;

  bool ShootImages(size_t camera_index,
		   size_t images_per_camera,
		   const std::string& prefix, 
		   ImageDataArray& images,
		   std::string& error_text,
		   bool continuous,
		   size_t between_image_delay_ms);

  void SaveImages(const std::string& save_dir,
		  const ImageDataArray& images,
		  size_t start_index=0,
		  size_t end_index=-1);

  void SaveImage(const std::string& save_dir,
		 const ImageData& image_data);
  
  void ResetCounters();
    
private:

  void FillImageData(Pylon::CGrabResultPtr grab_result,
		     ImageData& image_data);

  static int _pylon_is_initted;

  Pylon::CImageFormatConverter* _fc;
    
  size_t _connected_cameras;
  Pylon::CInstantCamera* _cameras;
  std::string _camera_ids[2];
  int _photo_index[2];
    
};

int CameraWrapper::_pylon_is_initted = 0;

CameraWrapper::CameraWrapper():
  _connected_cameras(0)

{

  _fc = new Pylon::CImageFormatConverter;
  _fc->OutputPixelFormat = Pylon::PixelType_RGB8packed;
    
  if (!_pylon_is_initted) {
    Pylon::PylonInitialize();
    _pylon_is_initted = 1;
  }
    
  _cameras = new Pylon::CInstantCamera[2];
    
}

CameraWrapper::~CameraWrapper() {
    
  CloseCameras();
  delete[] _cameras;

  delete _fc;
    
}

bool CameraWrapper::OpenCameras(int num_cameras,
				std::string& error_text) {

  assert( num_cameras == 1 || num_cameras == 2 );
    
  try {
        
    // Get the transport layer factory.
    Pylon::CTlFactory& tlFactory = Pylon::CTlFactory::GetInstance();

    Pylon::DeviceInfoList_t devices;
        
    int actual_count = tlFactory.EnumerateDevices(devices);
    std::cout << "Found " << actual_count << " cameras." << std::endl;

    if (actual_count == 0) {
      error_text = "No cameras detected";
      return false;
    }

    int indices[2] = { -1, -1 };

    if (num_cameras == 2) {
            
      if (actual_count != 2) {

	char buf[1024];
	snprintf(buf, 1024, "Expected %d cameras but got %d.",
		 num_cameras, actual_count);
	
	error_text = buf;
	return false;
	
      }

      if (devices[0].GetUserDefinedName() != "lower") {
	indices[0] = 0;
	indices[1] = 1;
      } else {
	indices[0] = 1;
	indices[1] = 0;
      }
            
    } else {
      if (actual_count == 0) {
	std::cerr << "no cameras found, bailing!\n";
	return false;
      } else if (actual_count > 1) {
	std::cerr << "More than one device, will just use first found\n";
      }
      indices[0] = 0;
    }

    for (int i=0; i<num_cameras; ++i) {
            
      Pylon::CInstantCamera& camera = _cameras[i];
      const Pylon::CDeviceInfo& info = devices[indices[i]];
            
      camera.Attach(tlFactory.CreateDevice(info));
      camera.Open();

      std::string user_name(info.GetUserDefinedName().c_str(),
			    info.GetUserDefinedName().size());

      if (user_name.empty()) {
	char buf[1024];
	snprintf(buf, 1024, "camera%d", i);
	user_name = buf;
      }

      std::cerr << "camera " << i << " has name " << user_name << "\n";

      _camera_ids[i] = user_name;
            
    }
        
  } catch (const GenericException &e) {

    error_text = e.GetDescription();
    return false;
        
  }

  printf("Cameras all set up.\n");

  _connected_cameras = num_cameras;

  ResetCounters();

  return true;

}

void CameraWrapper::ResetCounters() {

  memset(_photo_index, 0, sizeof(_photo_index));

}

void CameraWrapper::CloseCameras() {
    
  for (int i=0; i<2; ++i) {
    if (_cameras[i].IsPylonDeviceAttached()) {
      std::cout << "Destroying camera device " << i << "\n";
      _cameras[i].DestroyDevice();
    }
  }

  _connected_cameras = 0;

}

size_t CameraWrapper::GetConnectedCameras() const {
  return _connected_cameras;
}

bool CameraWrapper::ShootImages(size_t camera_index,
				size_t images_per_camera,
				const std::string& prefix, 
				ImageDataArray& images,
				std::string& error_text,
				bool continuous,
				size_t between_image_delay_ms) {

  assert( camera_index < _connected_cameras );

  Pylon::CInstantCamera& camera = _cameras[camera_index];

  try {

    std::cout << "will grab " << images_per_camera << " from " << _camera_ids[camera_index] << " " << (continuous ? "continuously" : "one-at-a-time") << "\n";
        
    if (continuous) {
      camera.StartGrabbing(images_per_camera);
    }

    for (size_t i=0; i<images_per_camera; ++i) {

      if (continuous && !camera.IsGrabbing()) {
	break;
      }

      if (i && between_image_delay_ms) {
	std::cerr << "delaying " << between_image_delay_ms << " ms between images\n";
	msleep(between_image_delay_ms);
      }
        
      ImageData image_data;

      Pylon::CGrabResultPtr grab_result;

      bool ok;

      if (continuous) {
                
	ok = camera.RetrieveResult(5000, grab_result,
				   Pylon::TimeoutHandling_Return);

      } else {

	ok = camera.GrabOne(1000, grab_result,
			    Pylon::TimeoutHandling_Return);

      }
                
      if (!ok) {

	error_text = _camera_ids[camera_index] + " camera timed out";
	return false;

      }

      char buf[1024];
      snprintf(buf, 1024, "%s_%s_%02d",
	       prefix.c_str(),
	       _camera_ids[camera_index].c_str(),
	       _photo_index[camera_index]++);
      
      image_data.basename = buf;

      _fc->Convert(image_data.pylon_image, grab_result);

      images.push_back(image_data);
            
    }

    std::cout << "...successfully grabbed " << images_per_camera << " images\n\n";

  } catch (Pylon::GenericException& e) {

    error_text = (std::string("Error capturing photos: ") +
		  e.GetDescription());

    return false;

  }

  return true;

}


const std::string& CameraWrapper::GetCameraID(size_t camera_index) const {

  assert( camera_index < _connected_cameras );

  return _camera_ids[camera_index];
    
}

void CameraWrapper::SaveImages(const std::string& save_dir,
			       const ImageDataArray& images,
			       size_t start_index,
			       size_t end_index) {


  end_index = std::min(end_index, images.size());

  for (size_t i=start_index; i<end_index; ++i) {
    SaveImage(save_dir, images[i]);        
  }

  std::cout << "\n";
  
    
}

void CameraWrapper::SaveImage(const std::string& save_dir,
			      const ImageData& image_data) {

  const std::string& basename = image_data.basename;

  std::string outfile = basename + ".jpg";

  FILE* fp = fopen(outfile.c_str(), "wb");
  if (!fp) {
    std::cerr << "error opening " << outfile << "!\n";
    exit(1);
  }

  const Pylon::CPylonImage& pimage = image_data.pylon_image;

  size_t stride;

  if (!pimage.GetStride(stride)) {
    std::cerr << "can't get stride for image!\n";
    exit(1);
  }

  write_jpg(fp, 85,
	    pimage.GetWidth(), pimage.GetHeight(),
	    stride,
	    (const uint8_t*)pimage.GetBuffer());

  std::cout << "  saved " << outfile << "\n";

    
}




//////////////////////////////////////////////////////////////////////

int wrap_atoi(const char* name, const char* str, int min, int max) {

  char* tmp;
  int result = strtol(str, &tmp, 10);

  if (!tmp || *tmp || result < min || result > max) {
    std::cerr << "invalid value for " << name << "\n";
  }

  return result;
    
}

struct Option {
  const char* name;
  char flag;
  int min;
  int max;
  int* value;
};

void dieusage(std::ostream& ostr, const Option* options, int eval) {

  ostr << "usage: multi_capture_standalone [OPTIONS]\n\n"
       << "OPTIONS:\n";

  for (int j=0; options[j].name; ++j) {
    ostr << "  -" << options[j].flag << " " << options[j].name
	 << " (default " << *(options[j].value) << ")\n";
  }

  exit(eval);
    
}

void parse_options(int argc, char** argv, Option* options) {

  for (int i=1; i<argc; i += 2) {

    const char* cur_arg = argv[i];
        
    if (strlen(cur_arg) != 2 || cur_arg[0] != '-') {
      std::cerr << "bad argument: " << cur_arg << "\n";
      dieusage(std::cerr, options, 1);
    }

    char c = cur_arg[1];
    bool hit = false;

    for (int j=0; options[j].name; ++j) {
      if (c == options[j].flag) {
	*(options[j].value) = wrap_atoi(options[j].name,
					argv[i+1],
					options[j].min,
					options[j].max);
	hit = true;
	break;
      }
    }

    if (!hit) {
      std::cerr << "bad argument: " << cur_arg << "\n";
      dieusage(std::cerr, options, 1);
    }
                    
  }

  for (int j=0; options[j].name; ++j) {
    std::cout << options[j].name << " = " << *(options[j].value) << "\n";
  }
            
}


int main(int argc, char** argv) {

  int num_cameras = 2;
  int shots_per_camera = 3;
  int num_iter = 10;
  int continuous_mode = 1;
  int between_image_delay_ms = 0;
  int between_camera_delay_ms = 0;

  Option options[] = {
    { "NCAM", 'n', 1, 2, &num_cameras },
    { "NPIC", 'p', 1, 10, &shots_per_camera },
    { "NITER", 'i', 1, 1000, &num_iter },
    { "CMODE", 'c', 0, 1, &continuous_mode },
    { "IDELAY", 'I', 0, 1000, &between_image_delay_ms },
    { "CDELAY", 'C', 0, 10000, &between_camera_delay_ms },
    { NULL, 0, 0, 0, NULL }
  };

  parse_options(argc, argv, options);
    
  CameraWrapper cwrapper;

  std::string error_text;

  if (!cwrapper.OpenCameras(num_cameras, error_text)) {
    std::cout << "error: " << error_text << "\n";
    exit(1);
  }


  for (int i=0; i<num_iter; ++i) {

    ImageDataArray images;

    char prefix[1024];
    snprintf(prefix, 1024, "/tmp/test%04d", i);
	
    for (int c=0; c<num_cameras; ++c) {

      if (between_camera_delay_ms) {
	msleep(between_camera_delay_ms);
      }
            
      if (!cwrapper.ShootImages(c, shots_per_camera, prefix,
				images, error_text,
				continuous_mode,
				between_image_delay_ms)) {

	std::cerr << "error shooting for camera " << cwrapper.GetCameraID(c) << ": " << error_text << "\n";
	exit(1);
                
      }

    }

    cwrapper.SaveImages("/tmp", images);
        
  }

    
}
