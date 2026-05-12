#include "Scan.h"

Scan::Scan() : frequecy(0), endAngle(0), timeStamp(0), CRC8(0) {}

Scan::Scan(uint8_t lidarData[]) {

    // Each LiDAR frame (47 bytes) is a scan. This constructor unpacks the data.
    this->frequecy = (lidarData[3] << 8) | lidarData[2];
    this->startAngle = (lidarData[5] << 8) | lidarData[4];
    this->endAngle = (lidarData[43] << 8) | lidarData[42];
    this->timeStamp = (lidarData[45] << 8) | lidarData[44];
    this->CRC8 = lidarData[46];

    // Bytes 6 to 41 are points data
    uint8_t nPoint = 0;                 // track of how many points in the scanPoints[]
    for (int i = 6; i < 41; i += 3) {
        // Separate each point data (3 bytes) and send into Point constructor.
        Point pnt(&lidarData[i], nPoint, this->startAngle, this->endAngle);
        this->scanPoints[nPoint] = pnt;
        nPoint++;
    }
}

uint16_t Scan::getStartAngle() const {return startAngle;}

uint16_t Scan::getEndAngle() const {return endAngle;}

Point Scan::getPoint(int index) const{
    if (index >= 0 && index < 12) return this->scanPoints[index];
    return Point();
}