#ifndef PROCESSOR_CAN_H
#define PROCESSOR_CAN_H

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <linux/can.h>
#include <unistd.h>
#include <opencv2/opencv.hpp>

#include "DBscan.h" 

class ProcessorCAN {
private:
    static constexpr bool LiDAR_UPSIDE_DOWN = false;

    static constexpr float VALID_LIDAR_CONE_ANGLE = 180.0f;     // from current lidar config
    static constexpr uint16_t NUMBER_OF_SECTORS = 360;          // from current lidar config
    static inline const std::string CAN_INTERFACE = "can0";     // raspi's can interface. Used on startSocket()
    static constexpr uint16_t LIDAR_ID = 16;                    // = 010 hex
    static constexpr uint32_t ULTRASS_ID = 32;                  // = 020 hex
    static constexpr uint8_t CAN_HEADER_1 = 170;                // = AA hex
    static constexpr uint8_t CAN_HEADER_2 = 85;                 // = 55 hex
    static constexpr uint8_t CAN_FOOTER = 254;                  // = FE hex

    static constexpr float secAngRad = M_PI / 180.0f * (VALID_LIDAR_CONE_ANGLE / NUMBER_OF_SECTORS); // used to get point angle from sector number

    int socket_fd = -1;                                         // socket status
    size_t messageSize = 0;                                     // size of the expected message. Defined at constructor

    bool startSocket(const std::string &interface);

public:
    ProcessorCAN();
    ~ProcessorCAN();

    // Standard Getters
    constexpr float getValidLidarConeAngle() const { return VALID_LIDAR_CONE_ANGLE; }
    constexpr uint16_t getNumberOfSectors() const { return NUMBER_OF_SECTORS; }
    const size_t getMessageSize() const { return messageSize; }
    int getSocket() const { return socket_fd; }
    
    // String getter
    const std::string& getCanInterface() const { return CAN_INTERFACE; }

    constexpr uint16_t getLidarID() const { return LIDAR_ID; }
    
    // Protocol Header/Footer Getters
    constexpr uint8_t getCanHeader1() const { return CAN_HEADER_1; }
    constexpr uint8_t getCanHeader2() const { return CAN_HEADER_2; }
    constexpr uint8_t getCanFooter() const { return CAN_FOOTER; }

    // Core functionality
    bool getMessage(uint8_t* message, uint8_t* ultrass_message);
    bool messageToPoints(const uint8_t* canMessage, const uint8_t* ultrassMessage, std::vector<DBscanPoint>& points, float& ultrass_dist);
    //void pointsToImage(std::vector<DBscanPoint>& allPoints, std::vector<DBscanCluster>& clusters, std::vector<cv::Scalar>& clusterColors);

    //Testing
    bool getRawFrame(can_frame& frame);


    /*
    bool evaluate(const std::vector<std::pair<float, float>>& points);
    */
};

#endif // PROCESSOR_CAN_H