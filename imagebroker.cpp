// std
#include <cassert>
#include <cstdlib>
#include <exception>
#include <forward_list>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

// json
#include <nlohmann/json.hpp>

// OpenCV
#include <opencv2/core/opengl.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/cvconfig.h>
#ifdef HAVE_CUDA
#include <opencv2/cudaimgproc.hpp>
#else
#pragma message("Missing CUDA support in OpenCV, make sure to adjust configuration file accordingly")
#endif

// IFF SDK
#include <iff.h>


//#define IMAGE_MONO
constexpr int  MAX_WINDOW_WIDTH  = 1280;
constexpr int  MAX_WINDOW_HEIGHT = 1024;
constexpr char CONFIG_FILENAME[] = "imagebroker.json";

int main()
{
    nlohmann::json config;
    try
    {
        config = nlohmann::json::parse(std::ifstream(CONFIG_FILENAME), nullptr, true, true);
    }
    catch(const std::exception& e)
    {
        std::cerr << "Invalid configuration provided: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    const auto it_chains = config.find("chains");
    if(it_chains == config.end())
    {
        std::cerr << "Invalid configuration provided: missing `chains` section\n";
        return EXIT_FAILURE;
    }
    if(!it_chains->is_array())
    {
        std::cerr << "Invalid configuration provided: section `chains` must be an array\n";
        return EXIT_FAILURE;
    }
    if(it_chains->empty())
    {
        std::cerr << "Invalid configuration provided: section `chains` must not be empty\n";
        return EXIT_FAILURE;
    }
    const auto it_iff = config.find("IFF");
    if(it_iff == config.end())
    {
        std::cerr << "Invalid configuration provided: missing `IFF` section\n";
        return EXIT_FAILURE;
    }

    iff_initialize(it_iff->dump().c_str());

    std::vector<iff_chain_handle_t> chain_handles;
    for(const auto& chain_config : *it_chains)
    {
        const auto chain_handle = iff_create_chain(chain_config.dump().c_str(),
                [](const char* const element_name, const int error_code)
                {
                    std::ostringstream message;
                    message << "Chain element `" << element_name << "` reported an error: " << error_code;
                    iff_log(IFF_LOG_LEVEL_ERROR, message.str().c_str());
                });
        chain_handles.push_back(chain_handle);
    }
    const auto total_chains = chain_handles.size();
    size_t grid_x = 1;
    size_t grid_y = 1;
    while(grid_x * grid_y < total_chains)
    {
        ++grid_x;
        if(grid_x * grid_y >= total_chains)
        {
            break;
        }
        ++grid_y;
    }

#ifdef HAVE_CUDA
    using Mat = cv::cuda::GpuMat;
    namespace imgproc = cv::cuda;
#else
    using Mat = cv::Mat;
    namespace imgproc = cv;
#endif
    using exporter_t = std::function<void(const void*, size_t, const iff_image_metadata*)>;
    auto export_callbacks = std::vector<exporter_t>(total_chains);
    auto buffer_mutexes = std::vector<std::mutex>(total_chains);
    auto pending_buffers = std::vector<std::unique_ptr<Mat>>(total_chains);
    auto unused_buffer_lists = std::vector<std::forward_list<std::unique_ptr<Mat>>>(total_chains);
    for(size_t i = 0; i < total_chains; ++i)
    {
        for(int j = 0; j < 3; ++j) //one buffer currently rendering, one buffer already filled, one buffer currently filling
        {
            unused_buffer_lists[i].emplace_front(new Mat());
        }
        export_callbacks[i] = [&, i](const void* const data, const size_t size, const iff_image_metadata* const metadata)
        {
            #ifdef IMAGE_MONO
                const auto pitch = metadata->width * size_t{1} + metadata->padding;
            #else
                const auto pitch = metadata->width * size_t{4} + metadata->padding;
            #endif
            if(size < pitch * metadata->height)
            {
                std::ostringstream message;
                message << "Ignoring invalid buffer: " << metadata->width << "x" << metadata->height << "+" << metadata->padding << " " << size << " bytes";
                iff_log(IFF_LOG_LEVEL_WARNING, message.str().c_str());
                return;
            }
            #ifdef IMAGE_MONO
                const Mat src_image(cv::Size(metadata->width, metadata->height), CV_8UC1, const_cast<void*>(data), pitch);
            #else
                const Mat src_image(cv::Size(metadata->width, metadata->height), CV_8UC4, const_cast<void*>(data), pitch);
            #endif
            auto& unused_buffer_list = unused_buffer_lists[i];
            std::unique_ptr<Mat> pending_buffer;
            {
                std::lock_guard<std::mutex> buffer_lock(buffer_mutexes[i]);
                assert(!unused_buffer_list.empty());
                pending_buffer = std::move(unused_buffer_list.front());
                unused_buffer_list.pop_front();
            }
            #ifdef IMAGE_MONO
                imgproc::cvtColor(src_image, *pending_buffer, cv::COLOR_GRAY2BGRA);
            #else
                imgproc::cvtColor(src_image, *pending_buffer, cv::COLOR_RGBA2BGRA); //OpenCV requires BGR channel order
            #endif
            {
                std::lock_guard<std::mutex> buffer_lock(buffer_mutexes[i]);
                pending_buffers[i].swap(pending_buffer);
                if(pending_buffer)
                {
                    unused_buffer_list.push_front(std::move(pending_buffer));
                }
            }
        };
        const auto& chain_handle = chain_handles[i];
        iff_set_export_callback(chain_handle, "exporter",
                [](const void* const data, const size_t size, iff_image_metadata* const metadata, void* const private_data)
                {
                    const auto export_function = reinterpret_cast<const exporter_t*>(private_data);
                    (*export_function)(data, size, metadata);
                },
                &export_callbacks[i]);
        iff_execute(chain_handle, nlohmann::json{{"exporter", {{"command", "on"}}}}.dump().c_str());
    }

    const std::string window_name = "IFF SDK Image Broker Sample";
    cv::namedWindow(window_name, cv::WINDOW_NORMAL | cv::WINDOW_OPENGL);
    cv::setWindowProperty(window_name, cv::WND_PROP_VSYNC, 1);
    using renderer_t = std::function<void()>;
    renderer_t render_callback = [&]()
            {
                static auto textures = std::vector<cv::ogl::Texture2D>(total_chains);
#ifdef HAVE_CUDA
                static auto buffers  = std::vector<cv::ogl::Buffer>   (total_chains);
                static auto gpumats  = std::vector<cv::cuda::GpuMat>  (total_chains);
#endif
                for(size_t i = 0; i < total_chains; ++i)
                {
                    std::unique_ptr<Mat> pending_buffer;
                    {
                        std::lock_guard<std::mutex> buffer_lock(buffer_mutexes[i]);
                        pending_buffer = std::move(pending_buffers[i]);
                    }
                    if(pending_buffer)
                    {
#ifdef HAVE_CUDA
                        // When copying from cuda::GpuMat to ogl::Texture2D OpenCV creates temporary ogl::Buffer
                        // and registers it with CUDA (cudaGraphicsGLRegisterBuffer function) each time.
                        // This seems to be unnecessary and problematic (fails after some time) on Windows
                        // (at least when NVDEC function cuvidMapVideoFrame is also used in another thread).
                        // Instead create ogl::Buffer and map it as cuda::GpuMat once - this is faster and more stable.
                        // Source for the fact that mapping can be done just once:
                        // https://web.archive.org/web/20180611003604/https://github.com/nvpro-samples/gl_cuda_interop_pingpong_st#but-how-do-i-read-and-write-to-gl_texture_3d-in-cuda-and-what-will-it-cost-me
                        // Error message without this optimization:
                        // OpenCV(4.6.0) Error: Gpu API call (unknown error) in `anonymous-namespace'::CudaResource::registerBuffer, file ...\opencv-4.6.0\modules\core\src\opengl.cpp, line 176
                        if(buffers[i].size() != pending_buffer->size())
                        {
                            buffers[i].create(pending_buffer->size(), pending_buffer->type(), cv::ogl::Buffer::Target::PIXEL_UNPACK_BUFFER);
                            gpumats[i] = buffers[i].mapDevice();
                            buffers[i].unmapDevice();
                        }
                        pending_buffer->copyTo(gpumats[i]);
                        textures[i].copyFrom(buffers[i]);
#else
                        textures[i].copyFrom(*pending_buffer);
#endif
                        std::lock_guard<std::mutex> buffer_lock(buffer_mutexes[i]);
                        unused_buffer_lists[i].push_front(std::move(pending_buffer));
                    }
                }
                for(size_t y = 0; y < grid_y; ++y)
                {
                    for(size_t x = 0; x < grid_x; ++x)
                    {
                        const auto i = grid_x * y + x;
                        if(i >= total_chains)
                        {
                            break;
                        }
                        // image aspect ratio might not be preserved
                        cv::ogl::render(textures[i], { x * 1. / grid_x, y * 1. / grid_y, 1. / grid_x, 1. / grid_y });
                    }
                }
            };
    cv::setOpenGlDrawCallback(window_name,
            [](void* const private_data)
            {
                const auto render_function = reinterpret_cast<const renderer_t*>(private_data);
                (*render_function)();
            },
            &render_callback);

    iff_log(IFF_LOG_LEVEL_INFO, "Press Esc to terminate the program");
    bool size_set = false;
    bool rendering = true;
    while(true)
    {
        const auto keycode = cv::pollKey();
        if(keycode != -1)
        {
            if((keycode & 0xff) == 27)
            {
                iff_log(IFF_LOG_LEVEL_INFO, "Esc key was pressed, stopping the program");
                break;
            }
            else if((keycode & 0xff) == 8)
            {
                iff_log(IFF_LOG_LEVEL_INFO, "Backspace key was pressed, disabling acquisition");
                for(const auto chain_handle : chain_handles)
                {
                    iff_execute(chain_handle, nlohmann::json{{"exporter", {{"command", "off"}}}}.dump().c_str());
                }
            }
            else if((keycode & 0xff) == 13)
            {
                iff_log(IFF_LOG_LEVEL_INFO, "Enter key was pressed, enabling acquisition");
                for(const auto chain_handle : chain_handles)
                {
                    iff_execute(chain_handle, nlohmann::json{{"exporter", {{"command", "on"}}}}.dump().c_str());
                }
            }
            else if((keycode & 0xff) == 32)
            {
                if(rendering)
                {
                    iff_log(IFF_LOG_LEVEL_INFO, "Space key was pressed, pausing rendering");
                    rendering = false;
                }
                else
                {
                    iff_log(IFF_LOG_LEVEL_INFO, "Space key was pressed, resuming rendering");
                    rendering = true;
                }
            }
            else
            {
                std::ostringstream message;
                message << "Key press ignored, code: " << keycode;
                iff_log(IFF_LOG_LEVEL_DEBUG, message.str().c_str());
            }
        }
        if(!size_set)
        {
            // try to preserve image aspect ratio by sizing the window accordingly
            // (assuming all chains produce images with the same aspect ratio)
            cv::Size size;
            {
                std::lock_guard<std::mutex> buffer_lock(buffer_mutexes[0]);
                if(pending_buffers[0])
                {
                    size = pending_buffers[0]->size();
                }
            }
            if(!size.empty())
            {
                size.width *= static_cast<int>(grid_x);
                size.height *= static_cast<int>(grid_y);
                if(size.width > MAX_WINDOW_WIDTH)
                {
                    size.height = static_cast<cv::Size::value_type>(MAX_WINDOW_WIDTH / size.aspectRatio());
                    size.width = MAX_WINDOW_WIDTH;
                }
                if(size.height > MAX_WINDOW_HEIGHT)
                {
                    size.width = static_cast<cv::Size::value_type>(MAX_WINDOW_HEIGHT * size.aspectRatio());
                    size.height = MAX_WINDOW_HEIGHT;
                }
                cv::resizeWindow(window_name, size);
                size_set = true;
            }
        }
        if(rendering)
        {
            cv::updateWindow(window_name);
        }
    }

    for(const auto chain_handle : chain_handles)
    {
        iff_release_chain(chain_handle);
    }

    iff_finalize();

    return EXIT_SUCCESS;
}
