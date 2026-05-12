#ifndef DBSCAN_H
#define DBSCAN_H

#include <vector>
#include <utility>
#include <cstddef>
#include <cmath>
#include <algorithm>
#include <opencv2/opencv.hpp>

// DBscan logic
constexpr int N_FOR_CORE = 6;                           // number of neighbours to be core point

// Attenuations
constexpr float DELTA_DISTANCE_ATTENUATION = 0.0225f;   // to increase neighbour search range as point is further
constexpr int MIN_POINTS_FOR_VALID_CLUST = 30;          // only clusters of certain size will be considered obstacle
constexpr float MIN_POINTS_ATTENUATION = 1.0f;          // to decrease number of points needed to be valid cluster as distance increases

// velocity tracking with centroid jittering filter (ALPHA-BETA filter)
constexpr float ALPHA = 0.4f;       // How much the prediction is trusted over than read
constexpr float BETA = 0.05f;       // Resistance to sudden changes

constexpr float rear_proximity = 0.5f;              // distance a centroid has to be from x = 0 (img coordinates) to be valid

constexpr float MAX_DISTANCE_THRESHOLD = 1.5f;      // distance a centroid can be from previous frame to be concidered a possible match
constexpr size_t MAX_TRACK_TIME = 15;               // number of frames a track will be remembered without being seen 



struct DBscanPoint {
    bool isCore = false;
    bool inCluster = false;
    bool isValid = false;

    std::pair<float, float> coordinates{-1.0, -1.0};
    std::vector<int> neighbours;

    DBscanPoint() = default; 

    DBscanPoint(float x, float y) : coordinates(x, y) {} 
};



struct DBscanCluster {
    int id = -1;

    std::pair<float, float> centroid{-1.0, -1.0};
    std::vector<int> points;

    bool valid = false;

    DBscanCluster() = default;

    DBscanCluster(int id_) : id(id_) {}
};




void printClosestPoint(const std::vector<DBscanPoint>& pointsPolar);
void PolarToCartesian(DBscanPoint &point);
void pointsToImage(std::vector<DBscanPoint>& pointsCart, cv::Size imageSize);
bool pointsToImage_saveImg(std::vector<DBscanPoint>& pointsCart, cv::Size imageSize);
void clustersToImage(std::vector<DBscanPoint>& pointsCart, std::vector<DBscanCluster>& clusters, cv::Size imageSize, std::vector<cv::Scalar> clusterColors);
//void findNeighbors(std::vector<DBscanPoint>& points, int idx, float radius, float attenuation);
void find_neighbors(std::vector<DBscanPoint>& points, int idx, float radius, float attenuation);
std::vector<DBscanCluster> ClusterPoints(std::vector<DBscanPoint>& DBSpoints, int min_pts, int max_pts, float pts_attenuation);
void calculate_centroid(DBscanCluster& cluster, const std::vector<DBscanPoint>& allPoints);



struct TrackedObject{
    // cluster stracking
    int id;
    DBscanCluster currentCluster;
    size_t untrackedSince = 0;          // number of frames since the cluster was tracked

    // cluster centroid speed tracking
    std::pair<float, float> estPos;
    std::pair<float, float> vel;

    TrackedObject(int id_, DBscanCluster c) : id(id_), currentCluster(c) {
        estPos = c.centroid;
        vel.first = 0.0f;
        vel.second = 0.0f;
    }

    //RFID track creation
    TrackedObject(uint rfid_id) : id(rfid_id){
        estPos = std::pair<float, float>(0.0f, 0.0f);
        vel.first = 0.0f;
        vel.second = 0.0f;
    }

    void updatePosition(float measuredX, float measuredY, float deltaTimeSec) {
        // prediction based on velocity and previous position
        float predX = estPos.first + (vel.first * deltaTimeSec);
        float predY = estPos.second + (vel.second * deltaTimeSec);

        // diference between read and predicted position
        float diffX = measuredX - predX;
        float diffY = measuredY - predY;

        // update estimated position with ALPHA weight
        estPos.first = predX + (ALPHA * diffX);
        estPos.second = predY + (ALPHA * diffY);

        // update velocity with BETA resistance to change
        vel.first = vel.first + ((BETA / deltaTimeSec) * diffX);
        vel.second = vel.second +((BETA / deltaTimeSec) * diffY);
    }

    float getAvgSpeedMod() const {
        return std::sqrt((vel.first * vel.first) + (vel.second * vel.second));
    }

    float getDirection() const {
        return static_cast<float>(std::atan2(vel.second, vel.first));
    }

};



struct Match {
    int trackIdx;
    int newClusterIdx;
    float distance;

    Match(int track, int cluster, float dist) : trackIdx(track), newClusterIdx(cluster), distance(dist) {}
};

void matchClusters(std::vector<TrackedObject>& activeTracks, std::vector<DBscanCluster>& newClusters, double deltaTimeSec);
void TrackedToImage(std::vector<DBscanPoint>& pointsCart, std::vector<TrackedObject> activeTracks, std::vector<DBscanCluster>& clusters, cv::Size imageSize, std::vector<cv::Scalar> Colours);
int DetectOvertake(const TrackedObject& track);


#endif // DBSCAN_H