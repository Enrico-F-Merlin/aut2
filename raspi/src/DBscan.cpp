#include "../include/DBscan.h"
#include <iostream>
#include <numeric>
#include <cmath>
#include <set>




// Find neighbors within a given radius and attenuation
void find_neighbors(std::vector<DBscanPoint>& points, int idx, float alpha, float alpha_attenuation) {
    DBscanPoint& current_point = points[idx];

    for (int i = 0; i < points.size(); i++) {
        if (i == idx) continue;

        const DBscanPoint& other_point = points[i];

        if (!other_point.isValid) continue;

        float distance = hypot(current_point.coordinates.first - other_point.coordinates.first, current_point.coordinates.second - other_point.coordinates.second);

        float distance_from_origin = hypot(current_point.coordinates.first, current_point.coordinates.second);
        float adjusted_radius = alpha + (alpha_attenuation * distance_from_origin);

        if (distance <= adjusted_radius) {
            current_point.neighbours.push_back(i);
        }
        
    }

    if (current_point.neighbours.size() >= N_FOR_CORE) current_point.isCore = true;

    //std::cout << current_point.neighbours.size() << " neighbours\n";
}



std::vector<DBscanCluster> ClusterPoints(std::vector<DBscanPoint>& DBSpoints, int min_pts, int max_pts, float pts_attenuation) {
    std::vector<DBscanCluster> clusters;
    int currClusterID = 0;

    for (int i = 0; i < DBSpoints.size(); i++) {
        if (DBSpoints.at(i).inCluster) continue;    // ignore points already in cluster
        if (!DBSpoints.at(i).isValid) continue;     // ignore invalid points

        if (!DBSpoints.at(i).isCore) continue;      // clustering starts when a core point is found

        DBSpoints[i].inCluster = true;              // core point found
        DBscanCluster currCluster(currClusterID);   // starting cluster
        currCluster.points.push_back(i);
        
        auto start = DBSpoints.at(i).neighbours.begin();
        auto end = DBSpoints.at(i).neighbours.end();
        std::vector<int> daHood(start, end);         // chain link of neighbours

        for (int j = 0; j < daHood.size(); j++) {
            int pIdx = daHood.at(j);
            DBscanPoint* p = &DBSpoints.at(pIdx);

            if (p->inCluster) continue;

            p->inCluster = true;                    // add BORDER or CORE to cluster
            currCluster.points.push_back(pIdx);

            if (p->isCore) {                        // only CORE points expand search
                for (int nIdx : p->neighbours) {
                    // Check if the neighbor is already in a cluster
                    if (!DBSpoints.at(nIdx).inCluster) {
                        daHood.push_back(nIdx);
                    }
                }
            }
        }

        // validate cluster
        calculate_centroid(currCluster, DBSpoints);
        float dist = hypot(currCluster.centroid.first, currCluster.centroid.second);

        int adjusted_min_pts = static_cast<int>(min_pts / (dist * pts_attenuation)); // min_pts increases as objects approach (objects are hit by more points as they approach)
        int adjusted_max_pts = static_cast<int>(max_pts / (dist * pts_attenuation)); // max_pts increases as objects approach

        if (currCluster.points.size() > adjusted_min_pts && currCluster.points.size() < adjusted_max_pts) { currCluster.valid = true; }

        clusters.push_back(currCluster); // cluster finished, add to list
        currClusterID++;                 // prepare for next cluster
        
    }

    // put all noise (non-clustered points) in a cluster of id = -1
    DBscanCluster noiseCluster;
    for(int i = 0; i < DBSpoints.size(); i++) {
        if (!DBSpoints.at(i).isValid) continue; //remove this line to consider invalid points as noise
        if (!DBSpoints.at(i).inCluster) noiseCluster.points.push_back(i);
    }
    clusters.push_back(noiseCluster);

    return clusters;
}




// Calculate centroid of a cluster and update its centroid property
void calculate_centroid(DBscanCluster& cluster, const std::vector<DBscanPoint>& allPoints) {
    if (cluster.points.empty()) {
        cluster.centroid = {-1.0f, -1.0f};
    }

    float x_sum = std::accumulate(cluster.points.begin(), cluster.points.end(), 0.0f, 
        [&allPoints](float sum, int pIdx) {
            return sum + allPoints.at(pIdx).coordinates.first;
        });

    float y_sum = std::accumulate(cluster.points.begin(), cluster.points.end(), 0.0f, 
        [&allPoints](float sum, int pIdx) {
            return sum + allPoints.at(pIdx).coordinates.second;
        });


    float x = x_sum / cluster.points.size();
    float y = y_sum / cluster.points.size();

    float distance_from_origin = hypot(x, y);

    //std::cout << x << " | " << y << "\n";

    cluster.centroid = {x, y};
}


// for debuggin
void printClosestPoint(const std::vector<DBscanPoint>& points) {
    float closest = 15.0f;

    for (const DBscanPoint& p : points) {
        if (!p.isValid) continue;

        float currDist = hypot(p.coordinates.first, p.coordinates.second);


        if (closest > currDist) closest = currDist;
    }

    std::cout << "closest point: " << closest << " m\n";
}



// Function to convert polar to cartesian
void PolarToCartesian(DBscanPoint &point) {

    float distance = point.coordinates.first;
    float angleRad = point.coordinates.second;

    float x = distance * std::cos(angleRad);
    float y = distance * std::sin(angleRad);

    point.coordinates.first = x;
    point.coordinates.second = y;
}



void pointsToImage(std::vector<DBscanPoint>& pointsCart, cv::Size imageSize) {
    cv::Mat image(imageSize, CV_8UC3, cv::Scalar(0, 0, 0));
    float scaleFactor = imageSize.height / 12.0f;

    for (const auto& pCart : pointsCart) {

        if (!pCart.isValid) continue;

        cv::Point2f p(pCart.coordinates.first, pCart.coordinates.second);

        //scale to image pixels
        p.x *= scaleFactor;
        p.y *= scaleFactor;

        //center the point in the top of the image
        p.x += imageSize.width / 2;

        //print the point to the image
        cv::circle(image, p, 1, cv::Scalar(0, 255, 0), -1);
    }

    cv::imshow("Lidar",image);
    cv::waitKey(1);

    //send the image to the ImageProcessor
    //if(evaluate(image)){
    //    std::cout << "Object detected" << std::endl;
    //}
    //else{
    //    std::cout << "No object detected" << std::endl;
    //}
}



bool pointsToImage_saveImg(std::vector<DBscanPoint>& pointsCart, cv::Size imageSize) {
    cv::Mat image(imageSize, CV_8UC3, cv::Scalar(0, 0, 0));
    float scaleFactor = imageSize.height / 12.0f;

    for (const auto& pCart : pointsCart) {

        if (!pCart.isValid) continue;

        cv::Point2f p(pCart.coordinates.first, pCart.coordinates.second);

        //scale to image pixels
        p.x *= scaleFactor;
        p.y *= scaleFactor;

        //center the point in the top of the image
        p.x += imageSize.width / 2;

        //print the point to the image
        cv::circle(image, p, 1, cv::Scalar(0, 255, 0), -1);
    }

    cv::imshow("Lidar", image);
    cv::waitKey(2);

    std::string input;
    std::cout << "Save image? (y t / y f / n): ";
    // Use std::ws to clear any lingering newline characters in the input buffer, 
    // then grab the entire line including the space.
    std::getline(std::cin >> std::ws, input); 
    std::cout << "\n";

    if (input == "y t" || input == "y f") {
        // Shared counter for unique filenames
        static int imageCounter = 0;
        
        // Determine the subfolder based on the input
        std::string subFolder = (input == "y t") ? "true/" : "false/";
        
        // Construct the full file path
        std::string filename = "trainingImgs/" + subFolder + "lidar_scan_" + std::to_string(imageCounter) + ".png";
        
        // Save the image
        bool isSaved = cv::imwrite(filename, image);
        
        if (!isSaved) {
            std::cerr << "Error: Could not save image to " << filename << "\n";
            std::cerr << "Check if the directories 'trainingImgs/true/' and 'trainingImgs/false/' exist.\n";
        } else {
            std::cout << "Saved: " << filename << "\n";
            imageCounter++;
        }
        
        return true;
    } 
    
    // If input is "n", or anything else unrecognized, don't save
    return false; 
}



void clustersToImage(std::vector<DBscanPoint>& pointsCart, std::vector<DBscanCluster>& clusters, cv::Size imageSize, std::vector<cv::Scalar> clusterColors) {
    cv::Mat image(imageSize, CV_8UC3, cv::Scalar(0, 0, 0));
    float scaleFactor = imageSize.height / 12.0f;

    for (DBscanCluster &clust : clusters) {

        cv::Scalar colour(0, 255, 0);

        if (clust.id != -1 ){

            int colourIdx = clust.id % clusterColors.size();
            colour = clusterColors.at(colourIdx);

            cv::Scalar centroidColour(0, 165, 255);   // Orange
            cv::Point2f pC(clust.centroid.first, clust.centroid.second);
            
            pC.x *= scaleFactor;
            pC.y *= scaleFactor;

            pC.x += imageSize.width / 2;

            //std::cout << pC.x << " | " << pC.y << "\n";

            cv::circle(image, pC, 2, centroidColour, 2);
        }

        

        for (int pIdx : clust.points){
            DBscanPoint& pointDB = pointsCart.at(pIdx);
            cv::Point2f p(pointDB.coordinates.first, pointDB.coordinates.second);
            
            p.x *= scaleFactor;
            p.y *= scaleFactor;

            p.x += imageSize.width / 2;

            cv::circle(image, p, 1, colour, -1);

        }

    }

    cv::imshow("Lidar",image);
    cv::waitKey(1);
}



void TrackedToImage(std::vector<DBscanPoint>& pointsCart, std::vector<TrackedObject> activeTracks, std::vector<DBscanCluster>& clusters, cv::Size imageSize, std::vector<cv::Scalar> Colours) {
    cv::Mat image(imageSize, CV_8UC3, cv::Scalar(0, 0, 0));
    float scaleFactor = imageSize.height / 12.0f;

    for (TrackedObject& T : activeTracks) {

        if (T.untrackedSince > 0) continue;

        int colourIdx = T.id % Colours.size();
        cv::Scalar colour = Colours.at(colourIdx);

        cv::Scalar centroidColour(0, 165, 255);   // Orange
        cv::Point2f pC(T.currentCluster.centroid.first, T.currentCluster.centroid.second);
        
        pC.x *= scaleFactor;
        pC.y *= scaleFactor;

        pC.x += imageSize.width / 2;

        if (T.currentCluster.valid == true) {

            // draw centroid
            cv::circle(image, pC, 2, centroidColour, 2);

            //check if tracked object is somewhat moving to avoid jitter fake direction
            if (T.getAvgSpeedMod() > 0.3f && T.currentCluster.valid == true) {
                //creating second point to draw predicted direction
                float tmpX = pC.x + T.getAvgSpeedMod() * 10 * std::cos(T.getDirection());
                float tmpY = pC.y + T.getAvgSpeedMod() * 10 * std::sin(T.getDirection());

                cv::Point2f pA(tmpX, tmpY);

                //draw line from centroid to predicted direction
                cv::line(image, pC, pA, centroidColour, 2);
                
            }
        }


        for (int pIdx : T.currentCluster.points){
            DBscanPoint& pointDB = pointsCart.at(pIdx);
            cv::Point2f p(pointDB.coordinates.first, pointDB.coordinates.second);
            
            p.x *= scaleFactor;
            p.y *= scaleFactor;

            p.x += imageSize.width / 2;

            cv::circle(image, p, 1, colour, -1);

        //save the image with the bounding boxes inside the processed_images folder with name processed_image + timestamp.png
        //std::string filename = "raw/rawImage" + std::to_string(GetSystemTimeStamp()) + ".png";
        }
    }

    cv::Scalar green (0, 255, 0);

    for (int pIdx : clusters.back().points) {
        DBscanPoint& pointDB = pointsCart.at(pIdx);
        cv::Point2f p(pointDB.coordinates.first, pointDB.coordinates.second);
        
        p.x *= scaleFactor;
        p.y *= scaleFactor;

        p.x += imageSize.width / 2;

        cv::circle(image, p, 1, green, -1);
    }

    cv::imshow("Lidar",image);
    cv::waitKey(1);
}




void matchClusters(std::vector<TrackedObject>& activeTracks, std::vector<DBscanCluster>& newClusters, double deltaTimeSec) {

    static size_t newTrackId = 0;
    std::vector<Match> potentialMatches;

    // calculate all posible matches
    for (int t = 0; t < activeTracks.size(); t++) {

        // last tracked centroid coordinates
        float tX = activeTracks.at(t).currentCluster.centroid.first;
        float tY = activeTracks.at(t).currentCluster.centroid.second;

        for (int c = 0; c < newClusters.size() - 1; c++){ // -1 to ignore the last cluster (noise cluster of ID = -1)

            // new cluster centroid coordinates
            float cX = newClusters.at(c).centroid.first;
            float cY = newClusters.at(c).centroid.second;

            float dist = hypot(tX - cX, tY - cY);

            if (dist > MAX_DISTANCE_THRESHOLD) continue; // only consider close enough centroids

            potentialMatches.emplace_back(t, c, dist);
        }
    }

    // sort the matches from closest to furthest
    sort(potentialMatches.begin(), potentialMatches.end(), 
        [](const Match& a, const Match& b) { return a.distance < b.distance; });

    // keep track of which matches/clusters have matched
    std::vector<bool> trackAssigned(activeTracks.size(), false);
    std::vector<bool> clustersAssigned(newClusters.size(), false);

    // Assign closest first
    for (const Match& match : potentialMatches) {

        // see if already has match
        if (trackAssigned[match.trackIdx] || clustersAssigned[match.newClusterIdx]) continue;

        // update match masks
        trackAssigned[match.trackIdx] = true;
        clustersAssigned[match.newClusterIdx] = true;

        // update the tracked object
        DBscanCluster* nC = &newClusters.at(match.newClusterIdx);
        activeTracks.at(match.trackIdx).updatePosition(nC->centroid.first, nC->centroid.second, deltaTimeSec);
        activeTracks.at(match.trackIdx).currentCluster = *nC;
        activeTracks.at(match.trackIdx).untrackedSince = 0;

        //std::cout << "Track " << activeTracks.at(match.trackIdx).id << "moved " << match.distance << "m\n";
    }

    // Update unmatched tracks' "untrackedSince"
    for (int t = activeTracks.size() - 1; t >= 0; t--) { // backwards to allign iteration after erasure
        if (trackAssigned[t]) continue; // ignore matched tracks

        activeTracks.at(t).untrackedSince++;

        if (activeTracks.at(t).untrackedSince > MAX_TRACK_TIME) activeTracks.erase(activeTracks.begin() + t);
    }

    // Create new activeTrack for untracked clusters
    for (int c = 0; c < newClusters.size() - 1; c++) { // -1 to ignore noise cluster
        if (clustersAssigned[c]) continue; // ignore tracked cluster

        activeTracks.emplace_back(newTrackId, newClusters.at(c));
        newTrackId++;
    }
}



int DetectOvertake(const TrackedObject& track) {

    if (track.estPos.second > 5.0f) return -1; // ignore if trackedObject is further than 5 meters
    if (track.getAvgSpeedMod() < 0.3f) return -1; // ifnore if trackedObject is moving too slow (???maybe should allow if object is slowly approaching from behind)

    float direction = track.getDirection() * 180/3.14;
    if (!(direction < -45.0f && direction > -135.0f)) return -1; // ignore objects going away from bike (remember the image axis)

    int approach_zone = -1;

    // find which zone the trackedObject is in image
    if (track.estPos.first < -rear_proximity) approach_zone = 1;    //right
    else if(track.estPos.first > rear_proximity) approach_zone = 3; //left
    else approach_zone = 2;                                         //center

    return approach_zone;

}