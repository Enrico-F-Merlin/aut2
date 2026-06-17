#include "../include/ProcessorCAN.h"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <cstring>
#include <queue>



ProcessorCAN::ProcessorCAN() {
    // Start socket and define expected message size
    if (this->startSocket(CAN_INTERFACE)) {
        std::cout << "\nProcessor can initialized\n";
        messageSize = 2 * NUMBER_OF_SECTORS + 4;
    } else {
        std::cout << "\nProcessor failed to initialized\n";
    }
}



ProcessorCAN::~ProcessorCAN() {
    // shutdown socket
    if (socket_fd != -1) {
        close(socket_fd);
        socket_fd = -1;
    }
}


// configures and start can socket
bool ProcessorCAN::startSocket(const std::string &interface) {
    // This function configures and starts a socket as can
    socket_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);

    if (socket_fd < 0)
        return false;

    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ - 1);

    if (ioctl(socket_fd, SIOCGIFINDEX, &ifr) < 0)
        return false;

    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        return false;

    return true;
}


//reads incoming can frames until it finds a complete lidar read
bool ProcessorCAN::getMessage(uint8_t* message, uint8_t* ultrass_message) {

    // check if socket is ok
    if (socket_fd < 0) {
        std::cout << "Socket Problem\n";
        return false;
    }

    can_frame frame;
    bool headerFound = false;
    int messageIdx = 0;

    while(true) {
        int nbytes = read(socket_fd, &frame, sizeof(struct can_frame));
        // check if socket read was ok
        if (nbytes <= 0) {
            std::cout << "Could not read CAN frame\n";
            return false;
        }

        // mask out the flags to get the pure ID
        uint32_t actual_id = frame.can_id & CAN_EFF_MASK;
        
        
        if (actual_id == ULTRASS_ID) {
            memcpy(ultrass_message, frame.data, sizeof(float));
            continue;
        }

        // check if frame is from LiDAR
        if (actual_id != LIDAR_ID) {
            continue;
        }

        // check if current frame is a header frame (is only a momentary trigger)
        bool isHeader = (frame.len >= 2 && frame.data[0] == CAN_HEADER_1 && frame.data[1] == CAN_HEADER_2);

        if (isHeader) {
            headerFound = true; // tells if the headers is found and message is being built (is a state)
            messageIdx = 0; // Force reset to start fresh with the new cone
        }

        if (!headerFound) {
            continue; // Ignore garbage until a header is found
        }

        // Concatenate full message bytes piece by piece
        for (int i = 0; i < frame.len; i++) {
            // Safety check to prevent array memory overflow segmentation faults
            if (messageIdx + i < messageSize) {
                message[messageIdx + i] = frame.data[i];
            }
        }
        messageIdx += frame.len;

        // Check if we hit the target size
        if (messageIdx >= messageSize) {
            if (message[messageSize - 1] == CAN_FOOTER) {
                return true; // Perfect message
            } else {
                std::cout << "broken message (bad footer)\n";
                headerFound = false; // Reset state for the next read
                return false;
            }
        }
    }
}



//receives the complete LiDAR message and interprets into points (cartesian)
bool ProcessorCAN::messageToPoints(const uint8_t* canMessage, const uint8_t* ultrassMessage, std::vector<DBscanPoint>& points, float& ultrass_dist) {

    memcpy(&ultrass_dist, ultrassMessage, sizeof(float));

    // always clear previous points
    if (!points.empty()) {
        points.clear();
    }

    // another check for headers and footer
    if (canMessage[0] != CAN_HEADER_1 || canMessage[1] != CAN_HEADER_2) {
        std::cout << "Problem on message headers\n";
        return false;
    }
    if (canMessage[messageSize - 1] != CAN_FOOTER) {
        std::cout << "Problem with message footer\n";
        return false;
    }
    

    if (LiDAR_UPSIDE_DOWN) { // reverse points order if the lidar was installed upside down so left/right are coherent

        // get points from 2 bytes and associate to respective sector (reverse order)
        for (int i = messageSize - 3, sector = 0; i > 3; i-=2, sector++) {

            uint16_t distRaw = (canMessage[i + 1] << 8) | canMessage[i];
            // Remove invalid (as filler value) reads and reads way too close (ultrasound will be used for close range)
            if (distRaw == 0xFFFF || distRaw < 0x0050) {
                points.emplace_back();
                continue;
            }

            float dist = distRaw / 1000.0f;     //meters
            float angleRad = secAngRad * sector;  //degrees

            DBscanPoint p(dist, angleRad);

            PolarToCartesian(p);

            points.push_back(p);
            points.back().isValid = true;
        }

    }
    else {

        // get points from 2 bytes and associate to respective sector (order it came)
        for (int i = 3, sector = 0; i < messageSize - 1; i+=2, sector++) {

            uint16_t distRaw = (canMessage[i + 1] << 8) | canMessage[i];
            // Remove invalid (as filler value) reads and reads way too close (ultrasound will be used for close range)
            if (distRaw == 0xFFFF || distRaw < 0x0050) {
                points.emplace_back();
                continue;
            }

            float dist = distRaw / 1000.0f;     //meters
            float angleRad = secAngRad * sector;  //degrees

            DBscanPoint p(dist, angleRad);

            PolarToCartesian(p);

            points.push_back(p);
            points.back().isValid = true;
        }
        
    }

    return true;
}



bool ProcessorCAN::getRawFrame(can_frame& frame) {
    if (socket_fd < 0) return false;
    
    int nbytes = read(socket_fd, &frame, sizeof(struct can_frame));
    return (nbytes == sizeof(struct can_frame));
}