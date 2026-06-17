#include <iostream>
#include <fstream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <array>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <opencv2/opencv.hpp>
#include <condition_variable>
#include <csignal>
#include <sstream>
#include "include/DBscan.h"
#include "include/httplib.h"

// incoming CAN frame IDs ("can0" 500KBPS extended CAN)
#define ID_LIDAR    0x010
#define ID_ULTRASS  0x020
////#define ID_RFID     0x030

int global_can0_fd = -1;
int global_can1_fd = -1;

cv::Mat mapImage = cv::imread("lidar_map.png");


// task_lidar configuration
#define LIDAR_UPSIDE_DOWN false
#define VALID_LIDAR_CONE_ANGLE 180.0f
#define NUMBER_OF_SECTORS      360
#define CAN_HEADER_1 170 // = AA hex
#define CAN_HEADER_2 85  // = 55 hex
#define CAN_FOOTER   254 // = FE hex

static constexpr float SECTOR_ANGLE_RAD = M_PI / 180.0f * (VALID_LIDAR_CONE_ANGLE / NUMBER_OF_SECTORS); // used to get point angle from position in message (aka sector)
static constexpr int MESSAGE_SIZE = 2 * NUMBER_OF_SECTORS + 4;


// // Map of allowed UIDs from RFID readings
// const std::unordered_map<uint, std::string> rfid_names = {
//     // Beware that the UIDs are read backwards (ex: Enrico <- CAN : 59 C4 11 07)
//     {0x0711C459, "Enrico"}, //card
//     {0X0728AECB, "Iara"},   //blue tag
// };

// Generates the string that will be showed at the web page's dashboard
//std::string getDashboardString(const std::vector<TrackedObject>& activeTracks, float ultrass, const std::vector<uint>& present_rfids);
std::string getDashboardString(const std::vector<TrackedObject>& activeTracks, float ultrass);

// Saves the dashboard string into a local file for logging pourposes. I think is also overkill for the projects
//void printDashboard(const std::vector<TrackedObject>& activeTracks, float ultrass, const std::vector<uint>& present_rfids);
void printDashboard(const std::vector<TrackedObject>& activeTracks, float ultrass);


bool init_can_sockets();

// Struct with coontrolled access data that is shared by all threads. Saves the state that should be updated into dashboard.
struct DashboardState {
    std::vector<TrackedObject> tracks;
    float ultrass = 0.0f;
    //std::vector<uint> present_rfids;
};

std::mutex mutex_dashboard;
DashboardState shared_dash_state;


// Is a global buffer of most recent recieved frames of each kind (and full lidar Cone message). Also has controlled access.
struct InFrames {
    std::array<uint8_t, MESSAGE_SIZE> lidar_message; 

    struct can_frame closestObject  = {};
    //struct can_frame rfid     = {};

    bool has_new_lidar   = false;
    //bool has_new_rfid    = false;
    bool has_new_ultrass = false;
};

std::mutex mutex_inFrames;
std::condition_variable cv_lidar;
std::condition_variable cv_general;

InFrames in_frame_buffer;


std::atomic<bool> program_running{true};
// This will control program_running, the condition for the threads to shut on ctrl + C.
void handle_sigint(int sig) {
    std::cout << "\n[main] Ctrl+C detected. Initiating graceful shutdown...\n";
    program_running = false;
}


// This task will read all incoming frames, save them on the global buffer and recustruct the LiDAR message
void task_can_read() {
    std::cout << "[task_can_read] Starting....\n";

    struct can_frame inFrame;

    // Local variables for Lidar Assembly
    bool lidar_header_found = false;
    int lidar_message_idx = 0;
    std::array<uint8_t, MESSAGE_SIZE> local_lidar_buffer = {0};

    while (program_running) {

        // read() is a blocking call. If Ctrl+C is pressed and LiDAR is off program will freeze.
        int nbytes = read(global_can0_fd, &inFrame, sizeof(struct can_frame));

        if (nbytes <= 0) {
            // Check if we failed to read because the program is shutting down
            if (!program_running) break; 
            
            std::cout << "[task_can_read] could not read frame.\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        uint32_t actual_id;
        
        if (inFrame.can_id & CAN_EFF_FLAG) {
            actual_id = inFrame.can_id & CAN_EFF_MASK; // Extended Frame
        } else {
            actual_id = inFrame.can_id & CAN_SFF_MASK; // Standard Frame
        }

        bool notify_lidar = false;
        bool notify_general = false;

        switch(actual_id) {
            case ID_LIDAR: {
                // Frames from the LiDAR will be constantly being concatnated into the message buffer.
                // Once the message buffer has a complete Cone, it will signal the task_lidar() to do its thing

                bool isHeader = (inFrame.can_dlc >= 2 
                              && inFrame.data[0] == CAN_HEADER_1 
                              && inFrame.data[1] == CAN_HEADER_2);

                if (isHeader) {
                    lidar_header_found = true;
                    lidar_message_idx = 0;
                }

                if (lidar_header_found) {
                    for (int i = 0; i < inFrame.can_dlc; i++) {
                        if (lidar_message_idx + i < MESSAGE_SIZE) {
                            local_lidar_buffer[lidar_message_idx + i] = inFrame.data[i];
                        }
                    }
                    lidar_message_idx += inFrame.can_dlc;

                    if (lidar_message_idx >= MESSAGE_SIZE) {
                        if (local_lidar_buffer[MESSAGE_SIZE - 1] == CAN_FOOTER) {
                            // Lock the mutex and pass the whole array to the task_lidar thread.
                            {
                                std::lock_guard<std::mutex> lock(mutex_inFrames);
                                in_frame_buffer.lidar_message = local_lidar_buffer;
                                in_frame_buffer.has_new_lidar = true;
                            }
                            notify_lidar = true;
                        } else {
                            std::cout << "[task_can_read] Broken lidar message (bad footer).\n";
                        }
                        // Reset state to wait for the next header
                        lidar_header_found = false; 
                    }
                }
                break;
            }
            // case ID_RFID: {
            //     // Update most recent RFID frame on global buffer and notify task_general_data() of update

            //     {
            //         std::lock_guard<std::mutex> lock(mutex_inFrames);
            //         in_frame_buffer.rfid = inFrame;
            //         in_frame_buffer.has_new_rfid = true;
            //     }
            //     notify_general = true;
            //     break;
            // }
            case ID_ULTRASS: {
                // Same as above

                {
                    std::lock_guard<std::mutex> lock(mutex_inFrames);
                    in_frame_buffer.closestObject = inFrame;
                    in_frame_buffer.has_new_ultrass = true;
                }
                notify_general = true;
                break;
            }
            default:
                break;
        }

        // Tasks notification
        if (notify_lidar) {
            cv_lidar.notify_one();
        }
        if (notify_general) {
            cv_general.notify_one();
        }
    }
    
    std::cout << "[task_can_read] Shutting down socket...\n";
}




std::mutex mutex_display;
cv::Mat shared_display_image;
std::atomic<bool> new_display_ready{false};

void task_lidar() {
    // This task is responsible for taking the complete Cone message and making it into a image.
    // This process includes everything from extracting the points from the message to keeping track of objects.
    // Image will be sent to main() where the dashboard is being mantained, and tracks data is being saved on global buffer.

    std::cout << "[task_lidar] Starting...\n";

    // f trainable parameters
    static constexpr float alpha = 0.2f;                // radius search for neighbour
    static constexpr float alpha_attenuation = 0.08f;   // compensation for point spread over distance
    static constexpr int max_pts = 75;                  // maximum emount of points for a cluster to be valid
    static constexpr int min_pts = 20;                  // minimum emount of points for a cluster to be valid
    static constexpr float pts_attenuation = 1.5f;      // objects that are further are hit by less points

    // colours for clustering
    std::vector<cv::Scalar> Colours = {
        cv::Scalar(0, 0, 255),     // Red
        cv::Scalar(255, 0, 0),     // Blue
        cv::Scalar(0, 255, 255),   // Yellow
        cv::Scalar(255, 0, 255),   // Magenta
        cv::Scalar(255, 255, 0),   // Cyan
        cv::Scalar(255, 255, 255), // White
        cv::Scalar(128, 0, 255),   // Purple
        cv::Scalar(203, 192, 255), // Pink
        cv::Scalar(0, 215, 255),   // Gold
        cv::Scalar(235, 206, 135)  // Sky Blue
    };

    std::vector<DBscanPoint> allPoints(NUMBER_OF_SECTORS);
    std::vector<TrackedObject> activeTracks;
    cv::Size imageSize(800, 400);


    auto lastFrameTime = std::chrono::steady_clock::now(); // time tracking for avg cluster speed calculation
    while (program_running) {
        std::unique_lock<std::mutex> lock(mutex_inFrames);

        cv_lidar.wait(lock, []{ // thread should sleep untill it is signaled by task_can_read() or main() (program ending)
            return in_frame_buffer.has_new_lidar || !program_running; 
        });

        if (!program_running) break;

        // time tracking
        auto currFrameTime = std::chrono::steady_clock::now();
        double deltaTimeSec = std::chrono::duration<double>(currFrameTime - lastFrameTime).count();
        lastFrameTime = currFrameTime;

        std::array<uint8_t, MESSAGE_SIZE> complete_lidar_message = in_frame_buffer.lidar_message;
        in_frame_buffer.has_new_lidar = false;

        lock.unlock();
        
        // Reset all points to invalid before re-populating
        for (auto& p : allPoints) {
            p = DBscanPoint(); // Overwrites the old object with a brand new, empty one
            // p.isValid is natively false when instantiated
        }

        if (LIDAR_UPSIDE_DOWN) { 
            // Reverse order
            for (int i = MESSAGE_SIZE - 3, sector = 0; i > 3 && sector < NUMBER_OF_SECTORS; i -= 2, sector++) {
                uint16_t distRaw = (complete_lidar_message[i + 1] << 8) | complete_lidar_message[i];
                
                if (distRaw != 0xFFFF && distRaw >= 0x0050) {
                    allPoints[sector] = DBscanPoint(distRaw / 1000.0f, SECTOR_ANGLE_RAD * sector);
                    PolarToCartesian(allPoints[sector]);
                    allPoints[sector].isValid = true;
                }
            }
        } else {
            // Normal order
            for (int i = 3, sector = 0; i < MESSAGE_SIZE - 1 && sector < NUMBER_OF_SECTORS; i += 2, sector++) {
                uint16_t distRaw = (complete_lidar_message[i + 1] << 8) | complete_lidar_message[i];
                
                if (distRaw != 0xFFFF && distRaw >= 0x0050) {
                    allPoints[sector] = DBscanPoint(distRaw / 1000.0f, SECTOR_ANGLE_RAD * sector);
                    PolarToCartesian(allPoints[sector]);
                    allPoints[sector].isValid = true;
                }
            }
        }



        // calculate the exact same scale factor you use for drawing
        float scaleFactor = imageSize.height / 5.0f;

        for (int i = 0; i < allPoints.size(); i++) {
            // skip points that are already invalid (e.g., filler 0xFFFF reads)
            if (!allPoints[i].isValid) continue;

            cv::Point2f p(allPoints[i].coordinates.first, allPoints[i].coordinates.second);
            
            p.x *= scaleFactor;
            p.y *= scaleFactor;
            p.x += mapImage.cols / 2;

            int pX = static_cast<int>(std::round(p.x));
            int pY = static_cast<int>(std::round(p.y));

            // ensure coordinates are inside the map boundaries
            if (pX >= 0 && pX < mapImage.cols && pY >= 0 && pY < mapImage.rows) {
                
                // read the pixel from your static, saved map
                cv::Vec3b pixel = mapImage.at<cv::Vec3b>(pY, pX);
                
                bool isPixelEmpty = (pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0);

                // if the pixel is NOT empty, it means this LiDAR point hit an existing wall
                if (!isPixelEmpty) {
                    // Invalidate it! DBscan will now ignore this point completely.
                    allPoints[i].isValid = false; 
                }
            } else {
                // optional: Invalidate points that fall entirely outside the map
                allPoints[i].isValid = false; 
            }
        }



        // apply DBscan neighbour logic on all points
        for(int i = 0; i < allPoints.size(); i++) {
            find_neighbors(allPoints, i, alpha, alpha_attenuation);
        }

        // apply DBscan clustering logic on points with neighbours
        std::vector<DBscanCluster> clusters = ClusterPoints(allPoints, min_pts, max_pts, pts_attenuation);

        // check activeTracks to find most likely (greedy by distance) match between clusters from previous messages and current message
        matchClusters(activeTracks, clusters, deltaTimeSec);

        cv::Mat local_img = mapImage.clone();
        //cv::Mat local_img(imageSize, CV_8UC3, cv::Scalar(0, 0, 0));


        for (DBscanCluster &clust : clusters) {

            // Default colour is green (for noise)
            cv::Scalar colour(0, 255, 0);

            // Select a ciclic colour for cluster from its ID
            if (clust.id != -1 ){
                int colourIdx = clust.id % Colours.size();
                colour = Colours.at(colourIdx);
            }

            // Scale points to image and print them with respective cluster colour
            for (int pIdx : clust.points){
                DBscanPoint& pointDB = allPoints.at(pIdx);
                cv::Point2f p(pointDB.coordinates.first, pointDB.coordinates.second);
                
                p.x *= scaleFactor;
                p.y *= scaleFactor;

                p.x += imageSize.width / 2;

                cv::circle(local_img, p, 1, colour, -1);
            }
        }

        // Print trackedObject points with difference of also printing the centroid and drawing line of estimated speed
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
                cv::circle(local_img, pC, 2, centroidColour, 2);

                //check if tracked object is somewhat moving to avoid jitter fake direction
                if (T.getAvgSpeedMod() > 0.3f && T.currentCluster.valid == true) {
                    //creating second point to draw predicted direction
                    float tmpX = pC.x + T.getAvgSpeedMod() * 10 * std::cos(T.getDirection());
                    float tmpY = pC.y + T.getAvgSpeedMod() * 10 * std::sin(T.getDirection());

                    cv::Point2f pA(tmpX, tmpY);

                    //draw line from centroid to predicted direction
                    cv::line(local_img, pC, pA, centroidColour, 2);
                    
                }
            }
        }
        
        // Safely hand the image over to the main thread for displaying
        {
            std::lock_guard<std::mutex> disp_lock(mutex_display);
            shared_display_image = local_img;
            new_display_ready = true;
        }
        
        // Safely hand the tracks over for the dashboard
        {
            std::lock_guard<std::mutex> dash_lock(mutex_dashboard);
            shared_dash_state.tracks = activeTracks;
        }

    }
    std::cout << "[task_lidar] Shutting down...\n";
}




void task_general_data() {
    // Data other than LiDAR does not need intensive computing, so they can share same thread
    // This thread keeps track of which RFID UIDs are currintly "inside" and processes the ultrassonic message into the global buffer

    std::cout << "[task_general] Starting...\n";

    // Local list to remember who is currently checked in
    //std::vector<uint> present_rfids; 

    while (program_running) {
        std::unique_lock<std::mutex> lock(mutex_inFrames);

        cv_general.wait(lock, []{ 
            //return in_frame_buffer.has_new_rfid || in_frame_buffer.has_new_ultrass || !program_running;
            return in_frame_buffer.has_new_ultrass || !program_running;
        });

        if (!program_running) break;

        //bool process_rfid = in_frame_buffer.has_new_rfid;
        bool process_ultrass = in_frame_buffer.has_new_ultrass;

        //struct can_frame local_rfid = in_frame_buffer.rfid;
        struct can_frame local_ultrass = in_frame_buffer.closestObject;

        //in_frame_buffer.has_new_rfid = false; 
        in_frame_buffer.has_new_ultrass = false;

        lock.unlock();

        // Process Ultrasound
        if (process_ultrass) {
            float calculated_ultrasound_cm;
            std::memcpy(&calculated_ultrasound_cm, local_ultrass.data, sizeof(float));
            
            std::lock_guard<std::mutex> dash_lock(mutex_dashboard);
            shared_dash_state.ultrass = calculated_ultrasound_cm;
        }

        // Process RFID
    //     if (process_rfid) {
    //         uint rfid = 0;
    //         std::memcpy(&rfid, local_rfid.data, sizeof(uint));

    //         bool is_entry = true;

    //         // Toggle entry/exit in our local list
    //         for (auto it = present_rfids.begin(); it != present_rfids.end(); ++it) {
    //             if (*it == rfid) {
    //                 present_rfids.erase(it);
    //                 is_entry = false;
    //                 break;
    //             }
    //         }

    //         if (is_entry) {
    //             present_rfids.push_back(rfid);
    //             //std::cout << "[general] RFID: " << rfid << " entry.\n";
    //         } else {
    //             //std::cout << "[general] RFID: " << rfid << " exit.\n";
    //         }

    //         // Update the dashboard state with the new list of people
    //         {
    //             std::lock_guard<std::mutex> dash_lock(mutex_dashboard);
    //             shared_dash_state.present_rfids = present_rfids;
    //         }
    //     }
    }
    std::cout << "[task_general] Shutting down...\n";
}




int main() {
    // This main thread initializes all others and keeps the HMI in the web page as server
    // To connect, be on same network and connect to <RaspIP>:8080

    // Register the Ctrl+C signal handler
    std::signal(SIGINT, handle_sigint);

    if (!init_can_sockets()) {
        std::cout << "[System] Failed to initialize CAN sockets. Exiting.\n";
        return -1;
    }

    if (mapImage.empty()) {
        std::cout << "[System] Could not open or find the image 'lidar_map.png'\n";
        return -1;
    }

    // setup web server
    httplib::Server svr;
    
    // This is run when a client connects to the page, giving them the root page.
    // In this page there is a a place holder for the dashboard string and another for a "image".
    // A simple JavaScript will be contantly asking for new data, so the page does not have to be refreshed
    // This is run when a client connects to the page, giving them the root page.
    svr.Get("/", [](const httplib::Request& req, httplib::Response& res) {
        std::ifstream file("/home/nap/Desktop/aut2/raspi/html/page1.html");
        
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            res.set_content(buffer.str(), "text/html");
        } else {
            // Fallback in case the file is missing or path is wrong
            res.status = 404;
            res.set_content("<h1>404 Not Found</h1><p>index.html is missing.</p>", "text/html");
        }
    });

    // Add the Dashboard Text Endpoint
    svr.Get("/dash_data", [](const httplib::Request& req, httplib::Response& res) {
        std::vector<TrackedObject> safe_tracks_copy;
        //std::vector<uint> safe_rfids_copy; // NEW
        float safe_ultrass = 0.0f;

        {
            std::lock_guard<std::mutex> dash_lock(mutex_dashboard);
            safe_tracks_copy = shared_dash_state.tracks;
            safe_ultrass = shared_dash_state.ultrass;
            //safe_rfids_copy = shared_dash_state.present_rfids; // NEW
        }

        std::string dash_text = getDashboardString(safe_tracks_copy, safe_ultrass);
        res.set_content(dash_text, "text/plain");
    });

    // Keep the existing Video Feed Endpoint updated when asked by the JavaScript
    svr.Get("/video_feed", [&](const httplib::Request& req, httplib::Response& res) {
        res.set_content_provider(
            "multipart/x-mixed-replace; boundary=frame", // Tells the browser to keep the connection open and replacing incomming image continuosly
            [&](size_t offset, httplib::DataSink& sink) {
                
                while (program_running) {
                    cv::Mat frame_to_encode;
                    
                    {
                        std::lock_guard<std::mutex> disp_lock(mutex_display);
                        if (shared_display_image.empty()) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(30));
                            continue;
                        }
                        frame_to_encode = shared_display_image.clone(); 
                        new_display_ready = false; 
                    }

                    // Converts the image into a string to send it togheter with dashboard
                    std::vector<uchar> buf;
                    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 80};
                    cv::imencode(".jpg", frame_to_encode, buf, params);
                    std::string img_data(buf.begin(), buf.end());

                    std::string chunk = "--frame\r\n"
                                        "Content-Type: image/jpeg\r\n"
                                        "Content-Length: " + std::to_string(img_data.size()) + "\r\n\r\n" +
                                        img_data + "\r\n";
                    
                    if (!sink.write(chunk.data(), chunk.size())) {
                        return false; 
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(30)); 
                }
                return true;
            });
    });


    // start tasks
    std::thread thread_webserver([&]() { svr.listen("0.0.0.0", 8080); });
    std::cout << "[main] MJPEG Stream active at http://rpi-756.local:8080/\n";

    std::thread thread_can(task_can_read);
    std::thread thread_lidar(task_lidar);
    std::thread thread_general(task_general_data);

    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    std::cout << "[main] All tasks running. Press Ctrl+C to exit.\n";


    // This loop keeps the dashboard updated for when the web clients asks for new data.
    while (program_running) { 
        std::vector<TrackedObject> safe_tracks_copy;
        //std::vector<uint> safe_rfids_copy; // NEW
        float safe_ultrass = 0.0f;

        {
            std::lock_guard<std::mutex> dash_lock(mutex_dashboard);
            safe_tracks_copy = shared_dash_state.tracks;
            safe_ultrass = shared_dash_state.ultrass; 
            //safe_rfids_copy = shared_dash_state.present_rfids; // NEW
        }

        printDashboard(safe_tracks_copy, safe_ultrass);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
   
    // clean shutdown 
    // Stop the web server so its thread can finish
    svr.stop(); 
    
    // Wake up any sleeping threads 
    cv_lidar.notify_all();
    cv_general.notify_all();

    // Wait for all threads to finish
    if (thread_webserver.joinable()) thread_webserver.join();
    if (thread_can.joinable()) thread_can.join();
    if (thread_lidar.joinable()) thread_lidar.join();
    if (thread_general.joinable()) thread_general.join();

    close(global_can0_fd);

    std::cout << "[main] Goodbye!\n";
    return 0;
}



// creates the string that contains the image and dashboard data
//std::string getDashboardString(const std::vector<TrackedObject>& activeTracks, float ultrass, const std::vector<uint>& present_rfids) {
std::string getDashboardString(const std::vector<TrackedObject>& activeTracks, float ultrass) {
    std::ostringstream dash; 

    dash << "=== SENSOR DASHBOARD ===========================================================\n";
    dash << "  Ultrasound Distance: " << std::fixed << std::setprecision(2) << ultrass << " cm\n";
    
    // // Print People Inside with Name Translation
    // dash << "  People Inside: ";
    // if (present_rfids.empty()) {
    //     dash << "None";
    // } else {
    //     for (uint id : present_rfids) {
    //         // Check if the ID exists in our map
    //         auto it = rfid_names.find(id);
    //         if (it != rfid_names.end()) {
    //             dash << "[" << it->second << "] "; // Found it! Print the name.
    //         } else {
    //             dash << "[Unknown ID: " << id << "] "; // Not in map, print the raw ID.
    //         }
    //     }
    // }
    dash << "\n--------------------------------------------------------------------------------\n";
    dash << "  ACTIVE LiDAR TRACKS:\n";
    
    int visibleCount = 0;
    for (const TrackedObject& t : activeTracks) {
        if (t.untrackedSince > 0) continue;
        if (t.currentCluster.valid == false) continue;

        std::string zone = "Safe";
        switch(DetectZone(t)){
            case(1): zone = "Waring..."; break;
            case(2): zone = "DANGER!!!"; break;
        }

        dash << " [ Zone: " << std::setw(6) << zone << " ] [ Track ID: " << std::setw(3) << t.id << " ]  "
             << "X: " << std::fixed << std::setprecision(2) << std::setw(6) << t.currentCluster.centroid.first << " m  |  "
             << "Y: " << std::fixed << std::setprecision(2) << std::setw(6) << t.currentCluster.centroid.second << " m |  "
             << "S: " << std::fixed << std::setprecision(1) << std::setw(6) << t.getAvgSpeedMod() << " m\n";

        visibleCount++;
    }

    if (visibleCount == 0) {
        dash << "  No objects currently detected.\n";
    }

    dash << "================================================================================\n";
    
    return dash.str(); 
}

// Saves resulting string into a local file
//void printDashboard(const std::vector<TrackedObject>& activeTracks, float ultrass, const std::vector<uint>& present_rfids) {
void printDashboard(const std::vector<TrackedObject>& activeTracks, float ultrass) {
    std::ofstream dash("/dev/shm/lidar_dash.txt", std::ios::trunc); 
    if (dash.is_open()) {
        //dash << getDashboardString(activeTracks, ultrass, present_rfids);
        dash << getDashboardString(activeTracks, ultrass);
        dash.close(); 
    } else {
        std::cout << "[Warning] Could not open dashboard file!\n";
    }
}

bool init_can_sockets() {
    // initialize can0
    global_can0_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (global_can0_fd < 0) return false;

    struct ifreq ifr0;
    std::strncpy(ifr0.ifr_name, "can0", IFNAMSIZ - 1);
    if (ioctl(global_can0_fd, SIOCGIFINDEX, &ifr0) < 0) return false;

    struct sockaddr_can addr0;
    std::memset(&addr0, 0, sizeof(addr0));
    addr0.can_family = AF_CAN;
    addr0.can_ifindex = ifr0.ifr_ifindex;

    if (bind(global_can0_fd, (struct sockaddr *)&addr0, sizeof(addr0)) < 0) return false;

    // initialize can1
    global_can1_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (global_can1_fd < 0) return false;

    struct ifreq ifr1;
    std::strncpy(ifr1.ifr_name, "can1", IFNAMSIZ - 1);
    if (ioctl(global_can1_fd, SIOCGIFINDEX, &ifr1) < 0) return false;

    struct sockaddr_can addr1;
    std::memset(&addr1, 0, sizeof(addr1));
    addr1.can_family = AF_CAN;
    addr1.can_ifindex = ifr1.ifr_ifindex;

    if (bind(global_can1_fd, (struct sockaddr *)&addr1, sizeof(addr1)) < 0) return false;

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000; // 100 milliseconds timeout

    // apply Read and Write timeouts to can0
    setsockopt(global_can0_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(global_can0_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    // apply Read and Write timeouts to can1
    setsockopt(global_can1_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(global_can1_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    return true;
}