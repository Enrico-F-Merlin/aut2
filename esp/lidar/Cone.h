#ifndef CONE_H
#define CONE_H

#include <Arduino.h>
#include "Scan.h"

class Cone{
    private:
        uint16_t limitStart;
        uint16_t limitEnd;
        uint8_t nScans;

        Scan scanCollection[SCANS_ON_CONE];

    public:
        Cone(uint16_t limS, uint16_t limE);

        Scan getScan(int index) const;

        uint8_t getNScans() const;

        bool growCone(Scan& scan);
        
        void sendConeFan();

        void sendConeFan_Serial();
};

#endif