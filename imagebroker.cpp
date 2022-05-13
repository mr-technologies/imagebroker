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
#include <opencv2/highgui.hpp>

// IFF SDK
#include "iff.h"

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

    std::mutex render_mutex;
    cv::Mat render_image;

    auto export_func = new exporter_t();
    auto cb = [&](const void* data, size_t size, iff_image_metadata* metadata)
    {
        cv::Mat dst_image;
        dst_image.create(cv::Size(metadata->width, metadata->height), CV_8UC4);
        void* img_data = const_cast<void*>(data);
        cv::Mat src_image(cv::Size(metadata->width, metadata->height), CV_8UC4, img_data, metadata->width * 4 + metadata->padding);
        src_image.copyTo(dst_image);

        std::scoped_lock<std::mutex> render_lock(render_mutex);
        render_image = dst_image;
    };
    export_func->invoke = std::move(cb);

    iff_set_export_callback(current_chain, "exporter",
                            [](const void* data, size_t size, iff_image_metadata* metadata, void* private_data)
                            {
                                auto export_function = (exporter_t*)private_data;
                                export_function->invoke(data, size, metadata);
                            },
                            export_func);


    iff_execute(current_chain, json{ {"exporter", {{"command", "on"}}} }.dump().c_str());

    std::string window_name = "IFF SDK Image Broker Sample";
    cv::namedWindow(window_name, cv::WINDOW_NORMAL);
    cv::resizeWindow(window_name, 1024, 1024);


    printf("To terminate program press `Esc` key\n");
    while (true)
    {
        std::unique_lock<std::mutex> render_lock(render_mutex);
        auto image_to_show = render_image;
        render_lock.unlock();

        if(image_to_show.empty())
        {
            image_to_show = cv::Mat(2048, 2048, CV_8UC4, cv::Scalar(0, 0, 0, 255));
        }

        cv::imshow(window_name, image_to_show);
        if(cv::waitKey(5) == 27)
        {
            printf("Esc key is pressed by user. Stopping application.\n");
            break;
        }
    }

    iff_release_chain(current_chain);
    iff_finalize();

    delete export_func;

    return 0;
}
