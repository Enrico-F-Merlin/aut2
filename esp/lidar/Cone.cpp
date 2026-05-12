#include <ESP32-TWAI-CAN.hpp>
#include "Cone.h"

/*
* limitStart and limitEnd are in the constructor, but main.cpp always considers
* limStart as zero and limitEnd as LIDAR_VALID_CONE_ANGLE at config.h.
*/
Cone::Cone(uint16_t limS, uint16_t limE) : limitStart(limS), limitEnd(limE), nScans(0) {}

Scan Cone::getScan(int index) const {
    if (index >= 0 && index < this->nScans) return scanCollection[index];
    return Scan();
}

uint8_t Cone::getNScans() const { return this->nScans; }

bool Cone::growCone (Scan& scan) {
    /*
    * The inRange considers True the cases where the scan is passing through the start
    * or the end limits defined in the constructor.
    */
    bool inRange = scan.getEndAngle() > this->limitStart && scan.getStartAngle() < this->limitEnd;

    if (inRange) {
        scanCollection[nScans] = scan;
        nScans++;
    }

    return inRange;
}

void Cone::sendConeFan() {
    /*
    * This method does further selection of the cone's values, more specifcaly, the points.
    * Even though the constructor accepts scans that cross limits, this function will only
    * accept points inside the defined limits as valid. Invalid points 
    */
    
    /*
    * frameSize for 2 bytes for each sector (distance) plus 4 for headers/footer and
    * count of valid points considered in sector closest distance calculation.
    */
    uint16_t frameSize = NUMBER_OF_SECTORS*2 + 4;
    uint8_t frame[frameSize];   // frame to be send in CAN

    frame[0] = CAN_HEADER_1;
    frame[1] = CAN_HEADER_2;
    frame[frameSize - 1] = CAN_FOOTER;

    frame[2] = 0; // valid points considered

    /*
    * Create sectorDists to hold the closest distance recorded in each.
    * memset() to fill all distances as 65535 (0xFFFF) as a "null" value.
    */
    uint16_t sectorDists[NUMBER_OF_SECTORS];
    memset(sectorDists, 0xFF, sizeof(sectorDists));

    // Validating all points of al scans.
    for (uint8_t scanIndex = 0; scanIndex < nScans; scanIndex++) {
        Scan currScan = scanCollection[scanIndex];

        for (uint8_t pointIndex = 0; pointIndex < 12; pointIndex++) {
            Point currPoint = currScan.getPoint(pointIndex);

            if (currPoint.getAngle() > LIDAR_VALID_CONE_ANGLE) continue; // skip invalid point (out of angle)
            if (currPoint.getDistance() > MAX_LIDAR_DIST) continue;      // ski invalid point (out of range)

            frame[2]++; // valid point, increment

            // sectorIndex indicates to which sector the current point belongs
            uint16_t sectorIndex = currPoint.getAngle()/(LIDAR_VALID_CONE_ANGLE/NUMBER_OF_SECTORS);


            // check if current valid point is closer to LiDAR in the sector in question.
            if (currPoint.getDistance() < sectorDists[sectorIndex] && currPoint.getGrade() > 128) {
                sectorDists[sectorIndex] = currPoint.getDistance();
            }
        }
    }

    /*
    * Copy the values of sectorDists (uint16) to the frame to be sent in CAN.
    * contrary to LiDAR's frame, this one will also be send in little-endian 
    because it is how micro-controller memory archtecture saves the data.
    (memcpy() will copy bytes as is in memory, little-endian)
    */
    for (uint16_t i = 0, j = 3; i < NUMBER_OF_SECTORS; i++, j+=2) {
        memcpy(&frame[j], &sectorDists[i], sizeof(sectorDists[0]));
    }

    uint16_t canIndex = 0;
    // lidarID is the composit ID of the message on the bus.
    uint32_t lidarID = (uint32_t)(CAN_LIDAR_PRIORITY << 26 | CAN_LIDAR_ID << 8 | backZ);

    // Configure CAN message (data lenght inside loop)
    CanFrame canFrame = { 0 };              // Reset message to avoid overlap
    canFrame.identifier = lidarID;          // Set ID on CAN bus
    canFrame.extd = 1;                      // Set extended ID format

    // Send frame[] an a CAN message.
    while (canIndex < frameSize) {
        // frame[] must be sent in chunks of 8 bytes AT MOST
        uint8_t chunkSize = min((int)8, (int)(frameSize - canIndex));

        canFrame.data_length_code = chunkSize;  // Set data lenght

        // Set each chunkSized part of frame[] as individual message
        for (uint8_t i = 0; i < chunkSize; i++) {
            canFrame.data[i] = frame[canIndex + i];
        }

        ESP32Can.writeFrame(canFrame);  // Send the chunk

        canIndex += chunkSize;  // Go foward in frame[] to select the next chunk

    }
}

void Cone::sendConeFan_Serial() {
    /*
    * Same as other one, but send through serial
    */
    
    uint16_t frameSize = NUMBER_OF_SECTORS*2 + 4;
    uint8_t frame[frameSize];

    frame[0] = CAN_HEADER_1;
    frame[1] = CAN_HEADER_2;
    frame[frameSize - 1] = CAN_FOOTER;

    frame[2] = 0;

    uint16_t sectorDists[NUMBER_OF_SECTORS];
    memset(sectorDists, 0xFF, sizeof(sectorDists));

    for (uint8_t scanIndex = 0; scanIndex < nScans; scanIndex++) {
        Scan currScan = scanCollection[scanIndex];

        for (uint8_t pointIndex = 0; pointIndex < 12; pointIndex++) {
            Point currPoint = currScan.getPoint(pointIndex);

            if (currPoint.getAngle() > LIDAR_VALID_CONE_ANGLE) continue;
            if (currPoint.getDistance() > MAX_LIDAR_DIST) continue;

            frame[2]++;

            uint16_t sectorIndex = currPoint.getAngle()/(LIDAR_VALID_CONE_ANGLE/NUMBER_OF_SECTORS);

            if (currPoint.getDistance() < sectorDists[sectorIndex]) {
                sectorDists[sectorIndex] = currPoint.getDistance();
            }
        }
    }

    for (uint16_t i = 0, j = 3; i < NUMBER_OF_SECTORS; i++, j+=2) {
        memcpy(&frame[j], &sectorDists[i], sizeof(sectorDists[0]));
    }

    Serial.write(frame, sizeof(frame));
}
