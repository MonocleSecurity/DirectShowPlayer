#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <Dshow.h>
#include <functional>
#include <GL/glew.h>
#include <iostream>
#include <GL/glu.h>
#include <GLFW/glfw3.h>
#include <mutex>
#include <optional>
#include <stdio.h>
#include <vector>
#include <boost/scope_exit.hpp>
#include <system_error>

static const CLSID CLSID_KsDataTypeHandlerVideo = { 0x05589f80, 0xc356, 0x11ce, { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };
static const CLSID CLSID_NullRenderer = { 0xC1F400A4, 0x3F08, 0x11d3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };
DEFINE_GUID(IID_ISampleGrabber, 0x6b652fff, 0x11fe, 0x4fce, 0x92, 0xad, 0x02, 0x66, 0xb5, 0xd7, 0xc7, 0x8f);
DEFINE_GUID(CLSID_SampleGrabber, 0xc1f400a0, 0x3f08, 0x11d3, 0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37);
DEFINE_GUID(CLSID_NullRenderer, 0xc1f400a4, 0x3f08, 0x11d3, 0x9f, 0x0b, 0x00, 0x60, 0x08, 0x03, 0x9e, 0x37);
DEFINE_GUID(IID_ISampleGrabberCB, 0x0579154a, 0x2b53, 0x4994, 0xb0, 0xd0, 0xe7, 0x73, 0x14, 0x8e, 0xff, 0x85);

MIDL_INTERFACE("0579154a-2b53-4994-b0d0-e773148eff85")
ISampleGrabberCB : public IUnknown
{
  virtual HRESULT STDMETHODCALLTYPE SampleCB(double SampleTime, IMediaSample * pSample) = 0;
  virtual HRESULT STDMETHODCALLTYPE BufferCB(double SampleTime, BYTE* pBuffer, LONG BufferLen) = 0;
};

MIDL_INTERFACE("6b652fff-11fe-4fce-92ad-0266b5d7c78f")
ISampleGrabber : public IUnknown
{
  virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL OneShot) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE* pType) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE* pType) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL BufferThem) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(LONG* pBufferSize, LONG* pBuffer) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample** ppSample) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB* pCallback, LONG WhichMethodToCallback) = 0;
};

class FrameGrabber : public ISampleGrabberCB
{
public:
  FrameGrabber(std::function<void(const char*, const size_t)> callback);
  ~FrameGrabber();
  STDMETHODIMP_(ULONG) AddRef();
  STDMETHODIMP_(ULONG) Release();
  STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject);
  STDMETHODIMP SampleCB(double time, IMediaSample* sample);
  STDMETHODIMP BufferCB(double time, BYTE* buffer, long bufferSize);

private:
  std::function<void(const char*, const size_t)> callback_;
};

class DirectShowDevice
{
public:

  DirectShowDevice();
  ~DirectShowDevice();

  int Init(std::function<void(const char*, const size_t)> callback);
  void Destroy();

  IMoniker* GetMoniker() const; // Caller owns the moniker on success
  IPin* GetPin(IBaseFilter* filter, const PIN_DIRECTION direction) const; // Caller owns the pin on success

  int width_;
  int height_;

  IGraphBuilder* graphbuilder_;
  ICaptureGraphBuilder2* capturegraphbuilder_;
  IMediaControl* mediacontrol_;
  IMoniker* moniker_;
  IAMStreamConfig* streamconfig_;
  IBaseFilter* grabberfilter_;
  ISampleGrabber* grabber_;
  FrameGrabber* framegrabber_;
  IBaseFilter* nullfilter_;

};

FrameGrabber::FrameGrabber(std::function<void(const char*, const size_t)> callback)
  : callback_(callback)
{
}

FrameGrabber::~FrameGrabber()
{
}

ULONG FrameGrabber::AddRef()
{
  return S_OK;
}

ULONG FrameGrabber::Release()
{
  return S_OK;
}

HRESULT FrameGrabber::QueryInterface(const IID& riid, void** ppvObject)
{
  if (ppvObject == nullptr)
  {
    return E_POINTER;
  }
  if (riid == __uuidof(IUnknown))
  {
    *ppvObject = static_cast<IUnknown*>(this);
    return S_OK;
  }
  if (riid == __uuidof(ISampleGrabberCB))
  {
    *ppvObject = static_cast<ISampleGrabberCB*>(this);
    return S_OK;
  }
  return E_NOTIMPL;
}

HRESULT FrameGrabber::SampleCB(double time, IMediaSample* sample)
{
  if (!sample)
  {
    return S_FALSE;
  }
  BYTE* buffer = nullptr;
  if (FAILED(sample->GetPointer(&buffer)))
  {
    return S_FALSE;
  }
  callback_(reinterpret_cast<const char*>(buffer), sample->GetActualDataLength());
  return S_OK;
}

HRESULT FrameGrabber::BufferCB(double time, BYTE* buffer, long bufferSize)
{
  return S_OK;
}

///// Methods /////

DirectShowDevice::DirectShowDevice()
  : graphbuilder_(nullptr)
  , capturegraphbuilder_(nullptr)
  , mediacontrol_(nullptr)
  , moniker_(nullptr)
  , streamconfig_(nullptr)
  , grabberfilter_(nullptr)
  , grabber_(nullptr)
  , framegrabber_(nullptr)
  , nullfilter_(nullptr)
  , width_(0)
  , height_(0)
{
}

DirectShowDevice::~DirectShowDevice()
{
  Destroy();
}

int DirectShowDevice::Init(std::function<void(const char*, const size_t)> callback)
{
  Destroy();

  if (FAILED(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, reinterpret_cast<void**>(&graphbuilder_))))
  {
    return 1;
  }
  if (FAILED(CoCreateInstance(CLSID_CaptureGraphBuilder2, nullptr, CLSCTX_INPROC_SERVER, IID_ICaptureGraphBuilder2, reinterpret_cast<void**>(&capturegraphbuilder_))))
  {
    Destroy();
    return 2;
  }
  if (FAILED(capturegraphbuilder_->SetFiltergraph(graphbuilder_)))
  {
    Destroy();
    return 3;
  }
  if (FAILED(graphbuilder_->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&mediacontrol_))))
  {
    Destroy();
    return 4;
  }
  moniker_ = GetMoniker();
  if (moniker_ == nullptr)
  {
    Destroy();
    return 4;
  }
  IBaseFilter* filter = nullptr;
  if (FAILED(moniker_->BindToObject(0, 0, IID_IBaseFilter, reinterpret_cast<void**>(&filter))))
  {
    Destroy();
    return 5;
  }
  if (FAILED(graphbuilder_->AddFilter(filter, L"Source")))
  {
    Destroy();
    return 6;
  }
  if (FAILED(capturegraphbuilder_->FindInterface(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, filter, IID_IAMStreamConfig, reinterpret_cast<void**>(&streamconfig_))))
  {
    Destroy();
    return 7;
  }
  if (FAILED(CoCreateInstance(CLSID_SampleGrabber, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter, reinterpret_cast<void**>(&grabberfilter_))))
  {
    Destroy();
    return 8;
  }
  if (FAILED(grabberfilter_->QueryInterface(IID_ISampleGrabber, reinterpret_cast<void**>(&grabber_))))
  {
    Destroy();
    return 9;
  }
  if (FAILED(graphbuilder_->AddFilter(grabberfilter_, L"Grabber")))
  {
    Destroy();
    return 10;
  }
  IEnumPins* enumpins = nullptr;
  if (FAILED(filter->EnumPins(&enumpins)))
  {
    Destroy();
    return 11;
  }
  BOOST_SCOPE_EXIT(enumpins)
  {
    enumpins->Release();
  }
  BOOST_SCOPE_EXIT_END
  bool foundpin = false;
  IPin* pin = nullptr;
  while ((enumpins->Next(1, &pin, nullptr) == S_OK) && (foundpin == false))
  {
    if (pin == nullptr)
    {
      break;
    }
    BOOST_SCOPE_EXIT(pin)
    {
      pin->Release();
    }
    BOOST_SCOPE_EXIT_END
    PIN_INFO pininfo;
    if (FAILED(pin->QueryPinInfo(&pininfo)))
    {
      continue;
    }
    if (pininfo.dir != PINDIR_OUTPUT) // We are only interested in outputs
    {
      continue;
    }
    IEnumMediaTypes* enummediatypes = nullptr;
    if (FAILED(pin->EnumMediaTypes(&enummediatypes)))
    {
      continue;
    }
    AM_MEDIA_TYPE* ammediatype = nullptr;
    while ((enummediatypes->Next(1, &ammediatype, nullptr) == S_OK) && (foundpin == false))
    {
      VIDEOINFOHEADER* videoinfoheader = reinterpret_cast<VIDEOINFOHEADER*>(ammediatype->pbFormat);
      if ((ammediatype->majortype == MEDIATYPE_Video) && (ammediatype->subtype == MEDIASUBTYPE_YUY2) && (videoinfoheader->bmiHeader.biWidth > 0) && (videoinfoheader->bmiHeader.biHeight > 0))
      {
        if (FAILED(streamconfig_->SetFormat(ammediatype)))
        {
          // Try another one
          continue;
        }
        else
        {
          width_ = videoinfoheader->bmiHeader.biWidth;
          height_ = videoinfoheader->bmiHeader.biHeight;
          foundpin = true;
        }
      }
      if (ammediatype->pbFormat)
      {
        CoTaskMemFree(ammediatype->pbFormat);
      }
      CoTaskMemFree(ammediatype);
    }
  }
  if (foundpin == false)
  {
    Destroy();
    return 12;
  }
  if (FAILED(grabber_->SetOneShot(false)))
  {
    Destroy();
    return 13;
  }
  if (FAILED(grabber_->SetBufferSamples(false)))
  {
    Destroy();
    return 14;
  }
  framegrabber_ = new FrameGrabber(callback);
  if (FAILED(grabber_->SetCallback(framegrabber_, 0)))
  {
    Destroy();
    return 15;
  }
  if (FAILED(CoCreateInstance(CLSID_NullRenderer, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&nullfilter_))))
  {
    Destroy();
    return 16;
  }
  if (FAILED(graphbuilder_->AddFilter(nullfilter_, L"NullFilter")))
  {
    Destroy();
    return 17;
  }
  IPin* sourcepin = GetPin(filter, PINDIR_OUTPUT);
  if (sourcepin == nullptr)
  {
    Destroy();
    return 18;
  }
  IPin* grabberinputpin = GetPin(grabberfilter_, PINDIR_INPUT);
  if (grabberinputpin == nullptr)
  {
    Destroy();
    return 19;
  }
  if (FAILED(graphbuilder_->Connect(sourcepin, grabberinputpin)))
  {
    Destroy();
    return 20;
  }
  IPin* grabberoutputpin = GetPin(grabberfilter_, PINDIR_OUTPUT);
  if (grabberoutputpin == nullptr)
  {
    Destroy();
    return 21;
  }
  if (FAILED(graphbuilder_->Render(grabberoutputpin)))
  {
    Destroy();
    return 22;
  }
  if (FAILED(mediacontrol_->Run()))
  {
    Destroy();
    return 23;
  }
  return 0;
}

void DirectShowDevice::Destroy()
{
  if (mediacontrol_)
  {
    mediacontrol_->Stop(); // Ignore error because we don't keep track of whether we started or not
  }
  if (nullfilter_)
  {
    nullfilter_->Release();
    nullfilter_ = nullptr;
  }
  if (framegrabber_)
  {
    delete framegrabber_;
    framegrabber_ = nullptr;
  }
  if (grabber_)
  {
    grabber_->Release();
    grabber_ = nullptr;
  }
  if (streamconfig_)
  {
    streamconfig_->Release();
    streamconfig_ = nullptr;
  }
  if (grabberfilter_)
  {
    grabberfilter_->Release();
    grabberfilter_ = nullptr;
  }
  if (mediacontrol_)
  {
    mediacontrol_->Release();
    mediacontrol_ = nullptr;
  }
  if (moniker_)
  {
    moniker_->Release();
    moniker_ = nullptr;
  }
  if (capturegraphbuilder_)
  {
    capturegraphbuilder_->Release();
    capturegraphbuilder_ = nullptr;
  }
  if (graphbuilder_)
  {
    graphbuilder_->Release();
    graphbuilder_ = nullptr;
  }
}

IMoniker* DirectShowDevice::GetMoniker() const
{
  ICreateDevEnum* createdeviceenum = nullptr;
  if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, reinterpret_cast<void**>(&createdeviceenum))))
  {
    return nullptr;
  }
  BOOST_SCOPE_EXIT(createdeviceenum)
  {
    createdeviceenum->Release();
  }
  BOOST_SCOPE_EXIT_END
  IEnumMoniker* enummoniker = nullptr;
  if (FAILED(createdeviceenum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enummoniker, 0)))
  {
    return nullptr;
  }
  if (enummoniker == nullptr)
  {
    return nullptr;
  }
  BOOST_SCOPE_EXIT(enummoniker)
  {
    enummoniker->Release();
  }
  BOOST_SCOPE_EXIT_END
  IMoniker* moniker = nullptr;
  while (enummoniker->Next(1, &moniker, nullptr) == S_OK)
  {
    return moniker;
  }
  return nullptr;
}

IPin* DirectShowDevice::GetPin(IBaseFilter* filter, const PIN_DIRECTION direction) const
{
  IEnumPins* enumpins = nullptr;
  if (FAILED(filter->EnumPins(&enumpins)))
  {
    return nullptr;
  }
  BOOST_SCOPE_EXIT(enumpins)
  {
    enumpins->Release();
  }
  BOOST_SCOPE_EXIT_END

    IPin* pin = nullptr;
  while (enumpins->Next(1, &pin, nullptr) == S_OK)
  {
    PIN_DIRECTION pindir = PINDIR_INPUT;
    if (FAILED(pin->QueryDirection(&pindir)))
    {
      pin->Release();
      continue;
    }
    if (pindir == direction)
    {
      return pin;
    }
    pin->Release();
  }
  return nullptr;
}

int main(int argc, char** argv)
{
  // Initialise webcam
  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
  {
    std::cerr << "CoInitializeEx failed" << std::endl;
    return -1;
  }
  // Initialise webcam
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<uint8_t> frame;
  DirectShowDevice device;
  if (device.Init([&mutex, &cv, &frame](const char* data, const size_t size)
                  {
                    std::unique_lock<std::mutex> lock(mutex);
                    frame.resize(size);
                    std::memcpy(frame.data(), data, size);
                    lock.unlock();
                    cv.notify_one();
                  }))
  {
    std::cerr << "Failed to initialise webcam" << std::endl;
    return -1;
  }
  // Init window
  if (!glfwInit())
  {
    std::cerr << "Failed to initialize GLFW" << std::endl;
    return -1;
  }
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  GLFWwindow* window = glfwCreateWindow(device.width_, device.height_, "DirectShow Player", NULL, NULL);
  if (!window)
  {
    std::cerr << "Failed to create GLFW window" << std::endl;
    return -1;
  }
  glfwMakeContextCurrent(window);
  if (glewInit() != GLEW_OK)
  {
    std::cerr << "Failed to initialize GLEW" << std::endl;
    return -1;
  }
  glfwSwapInterval(1);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  int display_w, display_h;
  glfwGetFramebufferSize(window, &display_w, &display_h);
  glViewport(0, 0, display_w, display_h);
  // YUYV Shader
  const char* yuyv_vertex_shader_source = R"(#version 330 core
                                            layout(location = 0) in vec2 in_pos;
                                            layout(location = 1) in vec2 in_tex_coord;
                                            out vec2 tex_coord;
                                            void main()
                                            {
                                                gl_Position = vec4(in_pos, 0.0, 1.0);
                                                tex_coord = in_tex_coord;
                                            })";
  const char* yuyv_fragment_shader_source = R"(#version 330 core
                                               out vec4 FragColor;
                                               in vec2 tex_coord;
                                               uniform sampler2D yuyv_texture;
                                               void main()
                                               {
                                                   ivec2 texture_size = textureSize(yuyv_texture, 0);
                                                   float x = tex_coord.x * texture_size.x;
                                                   vec2 yuyv = texture(yuyv_texture, tex_coord).rg;
                                                   float isOdd = mod(floor(x), 2.0);
                                                   float y;
                                                   float u;
                                                   float v;
                                                   if (isOdd < 0.5)
                                                   {
                                                       y = yuyv.r;
                                                       u = yuyv.g;
                                                       v = textureOffset(yuyv_texture, tex_coord, ivec2(1,0)).g;
                                                   }
                                                   else
                                                   {
                                                       y = yuyv.r;
                                                       u = textureOffset(yuyv_texture, tex_coord, ivec2(1,0)).g;
                                                       v = yuyv.g;
                                                   }
                                                   // Convert YUV to RGB
                                                   u -= 0.5;
                                                   v -= 0.5;
                                                   vec3 rgb = mat3(1.0, 1.0, 1.0,
                                                                   0.0, -0.39465, 2.03211,
                                                                   1.13983, -0.58060, 0.0) * vec3(y, u, v);
                                               
                                                   FragColor = vec4(rgb, 1.0);
                                               })";
  const GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertex_shader, 1, &yuyv_vertex_shader_source, nullptr);
  glCompileShader(vertex_shader);
  const GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragment_shader, 1, &yuyv_fragment_shader_source, nullptr);
  glCompileShader(fragment_shader);
  GLuint yuyv_shader_program = glCreateProgram();
  glAttachShader(yuyv_shader_program, vertex_shader);
  glAttachShader(yuyv_shader_program, fragment_shader);
  glLinkProgram(yuyv_shader_program);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);
  // YUYV texture
  GLuint yuyv_texture;
  glGenTextures(1, &yuyv_texture);
  glBindTexture(GL_TEXTURE_2D, yuyv_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, device.width_, device.height_, 0, GL_RG, GL_UNSIGNED_BYTE, nullptr);
  // Geometory
  const float vertices[] =
  {
    // positions  texture coords
    -1.0f, -1.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 1.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 0.0f
  };
  const unsigned int indices[] =
  {
    0, 1, 2,
    2, 3, 0
  };
  GLuint vao;
  GLuint vbo;
  GLuint ebo;
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glGenBuffers(1, &ebo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);
  // Main loop
  const std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
  while (!glfwWindowShouldClose(window))
  {
    // Pick up any frames
    std::vector<uint8_t> tmp_frame;
    {
      std::unique_lock<std::mutex> lock(mutex);
      cv.wait_for(lock, std::chrono::milliseconds(10));
      if (frame.size())
      {
        std::swap(frame, tmp_frame);
      }
    }
    // Draw texture
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(yuyv_shader_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, yuyv_texture);
    if (tmp_frame.size())
    {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, device.width_, device.height_, GL_RG, GL_UNSIGNED_BYTE, tmp_frame.data());
      tmp_frame.clear();
    }
    glUniform1i(glGetUniformLocation(yuyv_shader_program, "yuyv_texture"), 0);
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, yuyv_texture);
    glUseProgram(0);
    // Draw the texture onto the window
    glfwPollEvents();
    glfwSwapBuffers(window);
  }
  // Cleanup
  glDeleteShader(yuyv_shader_program);
  glDeleteTextures(1, &yuyv_texture);
  glDeleteVertexArrays(1, &vao);
  glDeleteBuffers(1, &vbo);
  glDeleteBuffers(1, &ebo);
  // Codec
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
