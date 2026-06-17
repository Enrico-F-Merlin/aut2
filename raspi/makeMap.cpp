#include "include/ProcessorCAN.h"
#include "include/DBscan.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <iomanip>
#include <opencv2/opencv.hpp>

void appendPointsToImage(const std::vector<DBscanPoint>& pointsCart, cv::Mat& image);

int main() {
    ProcessorCAN lidarProc;
    
    size_t messageMemAllocation = lidarProc.getMessageSize();
    size_t pointsMemAllocation = lidarProc.getNumberOfSectors();

    std::vector<uint8_t> message(messageMemAllocation);
    std::vector<uint8_t> ultrassonic_message(sizeof(float));
    float ultrassonic_dist = 0;
    std::vector<DBscanPoint> points(pointsMemAllocation);
    std::vector<TrackedObject> activeTracks;

    cv::Size imageSize(800, 400);
    
    cv::Mat mapImage = cv::Mat::zeros(imageSize, CV_8UC3);

    auto start = std::chrono::steady_clock::now();
    double timeElapsed = 0; 

    int i = 0;

    std::cout << "Starting main map making\n";
    do {
        auto currTime = std::chrono::steady_clock::now();
        
        if (!lidarProc.getMessage(message.data(), ultrassonic_message.data())) continue;
        if (!lidarProc.messageToPoints(message.data(), ultrassonic_message.data(), points, ultrassonic_dist)) continue;

        appendPointsToImage(points, mapImage);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        
        timeElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currTime - start).count();
        
        if (timeElapsed/1000 > i) {
            i++;
            std::cout << timeElapsed << "\n";
        }
    } while(timeElapsed < 10000); 


    bool isSaved = cv::imwrite("lidar_map.png", mapImage);
    
    if (isSaved) {
        std::cout << "Successfully saved map to lidar_map.png\n";
    } else {
        std::cerr << "Failed to save the image!\n";
    }

    cv::imshow("Lidar", mapImage);
    cv::waitKey(0);

    return 0;
}



void appendPointsToImage(const std::vector<DBscanPoint>& pointsCart, cv::Mat& image) {
    float scaleFactor = image.rows / 5.0f;

    for (const auto& pCart : pointsCart) {

        if (!pCart.isValid) continue;

        cv::Point2f p(pCart.coordinates.first, pCart.coordinates.second);

        p.x *= scaleFactor;
        p.y *= scaleFactor;

        p.x += image.cols / 2;

        cv::circle(image, p, 2, cv::Scalar(0, 255, 0), -1);
    }
}