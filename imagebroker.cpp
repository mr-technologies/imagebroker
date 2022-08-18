#define OPENCV_HAS_CUDA_AND_OPENGL 1

// std
#include <map>
#include <stack>
#include <chrono>
#include <thread>
#include <vector>
#include <cassert>
#include <fstream>
#include <condition_variable>

// json
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// OpenCV
#include <opencv2/opencv.hpp>
#if OPENCV_HAS_CUDA_AND_OPENGL
#include <opencv2/core/opengl.hpp>
#endif
#include <opencv2/highgui.hpp>

// IFF SDK
#include "iff.h"


const int MAX_WINDOW_WIDTH  = 1280;
const int MAX_WINDOW_HEIGHT = 1024;

struct exporter_t
{
    std::function<void(const void*, size_t, iff_image_metadata*)> invoke;
};

int main()
{
    std::ifstream cfg_file("imagebroker.json");
    std::string config_str = { std::istreambuf_iterator<char>(cfg_file), std::istreambuf_iterator<char>() };

    auto config = json::parse(config_str, nullptr, true, true);
    auto it_chains = config.find("chains");
    if(it_chains == config.end())
    {
        printf("Invalid configuration provided: chains not found\n");
        return 1;
    }
    if(!it_chains->is_array())
    {
        printf("Invalid configuration provided: section 'chains' must be an array\n");
        return 1;
    }

    auto it_iff = config.find("IFF");
    if(it_iff == config.end())
    {
        printf("Unable to find IFF configuration section in config file\n");
        return 1;
    }
    iff_initialize(it_iff.value().dump().c_str());

    std::vector<iff_chain_handle_t> chains;
    for(json& chain_config : it_chains.value())
    {
        auto chain_handle = iff_create_chain(chain_config.dump().c_str(), [](const char* element_name, int error_code)
        {
            printf("Chain element %s reported an error: %d\n", element_name, error_code);
        });
        chains.push_back(chain_handle);
    }

    auto current_chain = chains[0];

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
        void* img_data = const_cast<void*>(data);
        Mat src_image(cv::Size(metadata->width, metadata->height), CV_8UC4, img_data, metadata->width * 4 + metadata->padding);
        std::scoped_lock<std::mutex> render_lock(render_mutex);
        src_image.copyTo(render_image);
    };
    iff_set_export_callback(current_chain, "exporter",
                            [](const void* data, size_t size, iff_image_metadata* metadata, void* private_data)
                            {
                                auto export_function = (exporter_t*)private_data;
                                export_function->invoke(data, size, metadata);
                            },
                            &export_func);

    iff_execute(current_chain, json{ {"exporter", {{"command", "on"}}} }.dump().c_str());

    std::string window_name = "IFF SDK Image Broker Sample";
    bool size_set = false;
#if OPENCV_HAS_CUDA_AND_OPENGL
    cv::namedWindow(window_name, cv::WINDOW_NORMAL | cv::WINDOW_OPENGL);
    cv::setWindowProperty(window_name, cv::WND_PROP_VSYNC, 1);
    cv::ogl::Texture2D tex;
#else
    cv::namedWindow(window_name, cv::WINDOW_NORMAL);
#endif

    printf("To terminate program press `Esc` key\n");
    while(true)
    {
        if((cv::pollKey() & 0xffff) == 27)
        {
            printf("Esc key is pressed by user. Stopping application.\n");
            break;
        }
        std::scoped_lock<std::mutex> render_lock(render_mutex);
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

    iff_release_chain(current_chain);
    iff_finalize();

    return 0;
}
