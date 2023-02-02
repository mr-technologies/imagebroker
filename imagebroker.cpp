// std
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <vector>

// json
#include <nlohmann/json.hpp>

// OpenCV
#include <opencv2/opencv.hpp>
#include <opencv2/cvconfig.h>
#if defined(HAVE_CUDA) && defined(HAVE_OPENGL)
#include <opencv2/core/opengl.hpp>
#define OPENCV_HAS_CUDA_AND_OPENGL 1
#else
#warning Missing CUDA or OpenGL support in OpenCV, make sure to adjust configuration file accordingly
#endif

// IFF SDK
#include <iff.h>


constexpr int MAX_WINDOW_WIDTH  = 1280;
constexpr int MAX_WINDOW_HEIGHT = 1024;

struct exporter_t
{
    std::function<void(const void*, size_t, iff_image_metadata*)> invoke;
};

int main()
{
    std::ifstream cfg_file("imagebroker.json");
    const std::string config_str = { std::istreambuf_iterator<char>(cfg_file), std::istreambuf_iterator<char>() };

    const auto config = nlohmann::json::parse(config_str, nullptr, true, true);
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
    const auto it_iff = config.find("IFF");
    if(it_iff == config.end())
    {
        std::cerr << "Invalid configuration provided: missing `IFF` section\n";
        return EXIT_FAILURE;
    }

    iff_initialize(it_iff.value().dump().c_str());

    std::vector<iff_chain_handle_t> chain_handles;
    for(const auto& chain_config : it_chains.value())
    {
        const auto chain_handle = iff_create_chain(chain_config.dump().c_str(), [](const char* element_name, int error_code)
                {
                    std::ostringstream message;
                    message << "Chain element `" << element_name << "` reported an error: " << error_code;
                    iff_log(IFF_LOG_LEVEL_ERROR, message.str().c_str());
                });
        chain_handles.push_back(chain_handle);
    }

    const auto& current_chain = chain_handles.front();

#if OPENCV_HAS_CUDA_AND_OPENGL
    using Mat = cv::cuda::GpuMat;
#else
    using Mat = cv::Mat;
#endif
    std::mutex render_mutex;
    Mat render_image;

    exporter_t export_func;
    export_func.invoke = [&](const void* data, size_t size, iff_image_metadata* metadata)
    {
        void* const img_data = const_cast<void*>(data);
        Mat src_image(cv::Size(metadata->width, metadata->height), CV_8UC4, img_data, metadata->width * 4 + metadata->padding);
        std::lock_guard<std::mutex> render_lock(render_mutex);
        src_image.copyTo(render_image);
    };
    iff_set_export_callback(current_chain, "exporter",
                            [](const void* data, size_t size, iff_image_metadata* metadata, void* private_data)
                            {
                                auto export_function = (exporter_t*)private_data;
                                export_function->invoke(data, size, metadata);
                            },
                            &export_func);

    iff_execute(current_chain, nlohmann::json{ { "exporter", { { "command", "on" } } } }.dump().c_str());

    const std::string window_name = "IFF SDK Image Broker Sample";
    bool size_set = false;
#if OPENCV_HAS_CUDA_AND_OPENGL
    cv::namedWindow(window_name, cv::WINDOW_NORMAL | cv::WINDOW_OPENGL);
    cv::setWindowProperty(window_name, cv::WND_PROP_VSYNC, 1);
    cv::ogl::Texture2D tex;
#else
    cv::namedWindow(window_name, cv::WINDOW_NORMAL);
#endif

    iff_log(IFF_LOG_LEVEL_INFO, "Press Esc to terminate the program");
    while(true)
    {
        if((cv::pollKey() & 0xffff) == 27)
        {
            iff_log(IFF_LOG_LEVEL_INFO, "Esc key was pressed, stopping the program");
            break;
        }
        std::lock_guard<std::mutex> render_lock(render_mutex);
        if(!render_image.empty())
        {
            if(!size_set)
            {
                auto size = render_image.size();
                if(size.width > MAX_WINDOW_WIDTH)
                {
                    size.height = MAX_WINDOW_WIDTH / size.aspectRatio();
                    size.width = MAX_WINDOW_WIDTH;
                }
                if(size.height > MAX_WINDOW_HEIGHT)
                {
                    size.width = MAX_WINDOW_HEIGHT * size.aspectRatio();
                    size.height = MAX_WINDOW_HEIGHT;
                }
                cv::resizeWindow(window_name, size);
                size_set = true;
            }
#if OPENCV_HAS_CUDA_AND_OPENGL
            tex.copyFrom(render_image);
            cv::imshow(window_name, tex);
#else
            cv::imshow(window_name, render_image);
#endif
        }
    }

    for(const auto chain_handle : chain_handles)
    {
        iff_release_chain(chain_handle);
    }

    iff_finalize();

    return EXIT_SUCCESS;
}
