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
#define ID_LIDAR       514         
#define ID_ULTRASS     67109634    
// #define ID_HEARTRATE   -1          
// #define ID_INCLINATION -1
// #define ID_SPEED       -1


// task_lidar configuration
#define LIDAR_UPSIDE_DOWN true
#define VALID_LIDAR_CONE_ANGLE 180.0f
#define NUMBER_OF_SECTORS      360
#define CAN_HEADER_1 170 // = AA hex
#define CAN_HEADER_2 85  // = 55 hex
#define CAN_FOOTER   254 // = FE hex

static constexpr float SECTOR_ANGLE_RAD = M_PI / 180.0f * (VALID_LIDAR_CONE_ANGLE / NUMBER_OF_SECTORS); // used to get point angle from position in message (aka sector)
static constexpr int MESSAGE_SIZE = 2 * NUMBER_OF_SECTORS + 4;




std::string getDashboardString(const std::vector<TrackedObject>& activeTracks, float ultrass);
void printDashboard(const std::vector<TrackedObject>& activeTracks, float ultrass);

struct DashboardState {
    std::vector<TrackedObject> tracks;
    float ultrass = 0.0f;
};

std::mutex mutex_dashboard;
DashboardState shared_dash_state;



struct InFrames {
    std::array<uint8_t, MESSAGE_SIZE> lidar_message; 

    struct can_frame closestObject  = {};
    struct can_frame heartRate      = {};
    struct can_frame inclination    = {};

    bool has_new_lidar   = false;
    bool has_new_general = false;
};

std::mutex mutex_inFrames;
std::condition_variable cv_lidar;
std::condition_variable cv_general;

InFrames in_frame_buffer;

std::atomic<bool> program_running{true};

void handle_sigint(int sig) {
    std::cout << "\n[System] Ctrl+C detected. Initiating graceful shutdown...\n";
    program_running = false;
}

void task_can_read() {
    std::cout << "[task_can_read] Starting....\n";

    std::string interface = "can0";

    int can0_socket_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (can0_socket_fd < 0) {
        std::cout << "[task_can_read] Failed input socket initialization (1).\n";
        return;
    }
    
    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ - 1);

    if (ioctl(can0_socket_fd, SIOCGIFINDEX, &ifr) < 0){
        std::cout << "[task_can_read] Failed input socket initialization (2).\n";
        close(can0_socket_fd);
        return;
    }

    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(can0_socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        std::cout << "[task_can_read] Failed input socket initialization (3).\n"; 
        close(can0_socket_fd);
        return;
    }

    struct can_frame inFrame;

    // Local variables for Lidar Assembly
    bool lidar_header_found = false;
    int lidar_message_idx = 0;
    std::array<uint8_t, MESSAGE_SIZE> local_lidar_buffer = {0};

    while (program_running) {

        // read() is a blocking call. If Ctrl+C is pressed, it will return -1 and set errno to EINTR.
        int nbytes = read(can0_socket_fd, &inFrame, sizeof(struct can_frame));

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
            case ID_ULTRASS: {
                {
                    std::lock_guard<std::mutex> lock(mutex_inFrames);
                    in_frame_buffer.closestObject = inFrame;
                    in_frame_buffer.has_new_general = true;
                }
                notify_general = true;
                break;
            }
            // case ID_HEARTRATE: {
            //     {
            //         std::lock_guard<std::mutex> lock(mutex_inFrames);
            //         in_frame_buffer.heartRate = inFrame;
            //         in_frame_buffer.has_new_general = true;
            //     }
            //     notify_general = true;
            //     break;
            // }
            // case ID_INCLINATION: {
            //     {
            //         std::lock_guard<std::mutex> lock(mutex_inFrames);
            //         in_frame_buffer.inclination = inFrame;
            //         in_frame_buffer.has_new_general = true;
            //     }
            //     notify_general = true;
            //     break;
            // }
            default:
                break;
        }

        if (notify_lidar) {
            cv_lidar.notify_one();
        }
        if (notify_general) {
            cv_general.notify_one();
        }
    }
    
    std::cout << "[task_can_read] Shutting down socket...\n";
    close(can0_socket_fd);
}




std::mutex mutex_display;
cv::Mat shared_display_image;
std::atomic<bool> new_display_ready{false};

void task_lidar() {
    std::cout << "[task_lidar] Starting...\n";

    // trainable parameters
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

        cv_lidar.wait(lock, []{ 
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

        // apply DBscan neighbour logic on all points
        for(int i = 0; i < allPoints.size(); i++) {
            find_neighbors(allPoints, i, alpha, alpha_attenuation);
        }

        // apply DBscan clustering logic on points with neighbours
        std::vector<DBscanCluster> clusters = ClusterPoints(allPoints, min_pts, max_pts, pts_attenuation);

        // check activeTracks to find most likely (by distance) match between clusters from previous messages and current message
        matchClusters(activeTracks, clusters, deltaTimeSec);

        cv::Mat local_img(imageSize, CV_8UC3, cv::Scalar(0, 0, 0));
        float scaleFactor = imageSize.height / 5.0f;

        // pointsToImage() adaptation
        // ----------------------------------------------------
        // WARNING: cv::show() has to happen in main thread !!!
        // Instead of calling pointsToImage which opens a window, 
        // we just draw to a Mat and pass it to the main thread.
        /*
        
        for (const auto& pCart : allPoints) {
            if (!pCart.isValid) continue;
            cv::Point2f p(pCart.coordinates.first, pCart.coordinates.second);
            p.x = (p.x * scaleFactor) + (imageSize.width / 2);
            p.y = p.y * scaleFactor;
            cv::circle(local_img, p, 1, cv::Scalar(0, 255, 0), -1);
        }
        
        */


        // clustersToImage() adaptation
        // ----------------------------------------------------

        /*

        for (DBscanCluster &clust : clusters) {

            cv::Scalar colour(0, 255, 0);

            if (clust.id != -1 ){
                int colourIdx = clust.id % Colours.size();
                colour = Colours.at(colourIdx);

                cv::Scalar centroidColour(0, 165, 255);   // Orange
                cv::Point2f pC(clust.centroid.first, clust.centroid.second);
                
                pC.x *= scaleFactor;
                pC.y *= scaleFactor;

                pC.x += imageSize.width / 2;

                //std::cout << pC.x << " | " << pC.y << "\n";

                cv::circle(local_img, pC, 2, centroidColour, 2);
            }

            for (int pIdx : clust.points){
                DBscanPoint& pointDB = allPoints.at(pIdx);
                cv::Point2f p(pointDB.coordinates.first, pointDB.coordinates.second);
                
                p.x *= scaleFactor;
                p.y *= scaleFactor;

                p.x += imageSize.width / 2;

                cv::circle(local_img, p, 1, colour, -1);
            }
        }
        
        */


        // tracksToImage() adaptation
        // ----------------------------------------------------
        
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


            for (int pIdx : T.currentCluster.points){
                DBscanPoint& pointDB = allPoints.at(pIdx);
                cv::Point2f p(pointDB.coordinates.first, pointDB.coordinates.second);
                
                p.x *= scaleFactor;
                p.y *= scaleFactor;

                p.x += imageSize.width / 2;

                cv::circle(local_img, p, 1, colour, -1);
            }
        }

        cv::Scalar green (0, 255, 0);

        for (int pIdx : clusters.back().points) {
            DBscanPoint& pointDB = allPoints.at(pIdx);
            cv::Point2f p(pointDB.coordinates.first, pointDB.coordinates.second);
            
            p.x *= scaleFactor;
            p.y *= scaleFactor;

            p.x += imageSize.width / 2;

            cv::circle(local_img, p, 1, green, -1);
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
    std::cout << "[task_general] Starting...\n";

    while (program_running) {
        std::unique_lock<std::mutex> lock(mutex_inFrames);

        cv_general.wait(lock, []{ 
            return in_frame_buffer.has_new_general || !program_running; 
        });

        if (!program_running) break;

        struct can_frame local_ultrass = in_frame_buffer.closestObject;
        struct can_frame local_heart   = in_frame_buffer.heartRate;
        struct can_frame local_inclin  = in_frame_buffer.inclination;

        in_frame_buffer.has_new_general = false; 

        lock.unlock();

        float calculated_ultrasound_cm;
        std::memcpy(&calculated_ultrasound_cm, local_ultrass.data, sizeof(float));

        {
            std::lock_guard<std::mutex> dash_lock(mutex_dashboard);
            shared_dash_state.ultrass = calculated_ultrasound_cm;
        }
        // ...
    }
    std::cout << "[task_general] Shutting down...\n";
}




int main() {
    // Register the Ctrl+C signal handler
    std::signal(SIGINT, handle_sigint);

    // setup web server
    httplib::Server svr;
    
    svr.Get("/", [](const httplib::Request& req, httplib::Response& res) {
        // We use R"( ... )" in C++ to write multi-line strings easily
        const char* html_page = R"(
            <!DOCTYPE html>
            <html lang="en">
            <head>
                <meta charset="UTF-8">
                <meta name="viewport" content="width=device-width, initial-scale=1.0">
                <title>Pi Dashboard</title>
                <style>
                    body { font-family: sans-serif; text-align: center; background: #222; color: white; margin-top: 20px; }
                    img { border: 5px solid #555; border-radius: 10px; width: 100%; max-width: 800px; box-shadow: 0 4px 8px rgba(0,0,0,0.5); }
                    
                    /* Styling to make the text look like a cool terminal */
                    pre { 
                        background: #000; 
                        color: #0f0; /* Hacker green */
                        padding: 20px; 
                        border-radius: 10px; 
                        text-align: left; 
                        display: inline-block; 
                        font-size: 16px; 
                        margin-top: 20px;
                        overflow-x: auto;
                    }
                </style>
            </head>
            <body>
                <h1>LiDAR Live Dashboard</h1>
                <img src="/video_feed" alt="Live Feed"><br>
                
                <!-- The text will be injected here -->
                <pre id="dash_text">Waiting for LiDAR data...</pre>

                <script>
                    // Ask the C++ server for the text every 100 milliseconds
                    setInterval(() => {
                        fetch('/dash_data')
                            .then(response => response.text())
                            .then(text => {
                                // Update the HTML element with the new text
                                document.getElementById('dash_text').innerText = text;
                            });
                    }, 100); // 100ms = 10 updates per second
                </script>
            </body>
            </html>
        )";
        
        // Send the HTML to the browser
        res.set_content(html_page, "text/html");
    });

    // C. Add the Dashboard Text Endpoint
    svr.Get("/dash_data", [](const httplib::Request& req, httplib::Response& res) {
        std::vector<TrackedObject> safe_tracks_copy;
        float safe_ultrass_copy;

        // Safely lock and grab the latest dashboard data, exactly like you do in your while loop
        {
            std::lock_guard<std::mutex> dash_lock(mutex_dashboard);
            safe_tracks_copy = shared_dash_state.tracks;
            safe_ultrass_copy = shared_dash_state.ultrass;
        }

        // Generate the text string
        std::string dash_text = getDashboardString(safe_tracks_copy, safe_ultrass_copy);
        
        // Send it to the browser as plain text
        res.set_content(dash_text, "text/plain");
    });

    // B. Keep your existing Video Feed Endpoint right below it
    svr.Get("/video_feed", [&](const httplib::Request& req, httplib::Response& res) {
        res.set_content_provider(
            "multipart/x-mixed-replace; boundary=frame",
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
    std::cout << "[System] MJPEG Stream active at http://192.168.1.153:8080/\n";

    std::thread thread_can(task_can_read);
    std::thread thread_lidar(task_lidar);
    std::thread thread_general(task_general_data);

    std::cout << "[System] All tasks running. Press Ctrl+C to exit.\n";


    // MAIN THREAD (CLI Dashboard Loop)
    while (program_running) { 
        std::vector<TrackedObject> safe_tracks_copy;
        float safe_ultrass_copy;

        {
            std::lock_guard<std::mutex> dash_lock(mutex_dashboard);
            safe_tracks_copy = shared_dash_state.tracks;
            safe_ultrass_copy = shared_dash_state.ultrass;
        }

        printDashboard(safe_tracks_copy, safe_ultrass_copy);

        // Replace cv::waitKey(10) with standard thread sleep 
        // Controls how fast the terminal dashboard refreshes
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

    std::cout << "[System] Goodbye!\n";
    return 0;
}



// 1. New Helper: Generates the text as a string
std::string getDashboardString(const std::vector<TrackedObject>& activeTracks, float ultrass) {
    std::ostringstream dash; // Works exactly like std::cout or your file stream

    dash << "=== ACTIVE LiDAR TRACKS ========================================================\n";
    
    int visibleCount = 0;
    for (const TrackedObject& t : activeTracks) {
        if (t.untrackedSince > 0) continue;
        if (t.currentCluster.valid == false) continue;

        std::string approach = "No";
        switch(DetectOvertake(t)){
            case(1): approach = "Right"; break;
            case(2): approach = "Center"; break; 
            case(3): approach = "Left";
        }

        dash << " [ Approaching: " << std::setw(6) << approach << " ] [ ID: " << std::setw(3) << t.id << " ]  "
             << "X: " << std::fixed << std::setprecision(2) << std::setw(6) << t.currentCluster.centroid.first << " m  |  "
             << "Y: " << std::fixed << std::setprecision(2) << std::setw(6) << t.currentCluster.centroid.second << " m |  "
             << "S: " << std::fixed << std::setprecision(1) << std::setw(6) << t.getAvgSpeedMod() << " m\n";

        visibleCount++;
    }

    if (visibleCount == 0) {
        dash << "  No objects currently detected.\n";
    }

    dash << "Ultrassonic distance(cm): " << ultrass << "\n"
         << "================================================================================\n";
    
    return dash.str(); // Convert the stream to a standard string and return it
}

// 2. Your modified original function
void printDashboard(const std::vector<TrackedObject>& activeTracks, float ultrass) {
    std::ofstream dash("/dev/shm/lidar_dash.txt", std::ios::trunc); 
    if (dash.is_open()) {
        // Just call the helper function and dump the string into the file!
        dash << getDashboardString(activeTracks, ultrass);
        dash.close(); 
    } else {
        std::cout << "[Warning] Could not open dashboard file!\n";
    }
}