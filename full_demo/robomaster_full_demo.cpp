#include <iostream>
#include <opencv2/opencv.hpp>
#include <utils/timer.h>
#include <uniterm/uniterm.h>
#include <structure/enums.hpp>
#include <pointer/pointer.h>
#include <attack/attack.h>
#include <solver/solvepnp.h>
#include <utils/delay.h>
#include <cmath>
#include <cstdio>
#include <queue>
#include <random>
#include <limits>

class RoboMasterAutoAimDemo {
private:
    cv::VideoCapture cap;
    bool is_running = false;
    int frame_count = 0;
    TimePoint start_time;
    
    // Competition simulation
    int hits_scored = 0;
    int shots_fired = 0;
    double match_time = 0.0;
    bool simulation_mode = true;
    
    // RoboMaster Vision Components
    rm::TeamColor our_team = rm::TEAM_COLOR_BLUE;
    rm::ArmorColor enemy_color = rm::ARMOR_COLOR_RED;
    
    // Camera parameters (simulated calibration)
    cv::Mat camera_matrix = (cv::Mat_<double>(3, 3) << 
        800, 0, 320,
        0, 800, 240,
        0, 0, 1);
    cv::Mat distortion_coeffs = cv::Mat::zeros(4, 1, CV_64F);
    
    // Detection parameters (optimized for better detection)
    struct DetectionParams {
        double binary_threshold = 100.0;
        double area_threshold = 100.0;      // Lowered from 500 to match vision_processing
        double aspect_ratio_min = 1.0;      // Lowered from 1.5 to be more permissive
        double aspect_ratio_max = 5.0;      // Raised from 4.0 to be more permissive
        int erosion_size = 2;
        int dilation_size = 3;
    } params;
    
    // Complete detection and targeting results
    struct FullTargetingResult {
        // Vision detection
        bool armor_detected = false;
        cv::Rect armor_bbox;
        rm::ArmorColor detected_color = rm::ARMOR_COLOR_NONE;
        rm::ArmorSize armor_size = rm::ARMOR_SIZE_UNKNOWN;
        std::vector<cv::Point2f> armor_corners;
        double confidence = 0.0;
        cv::Point2f center_2d;
        
        // 3D positioning
        bool position_solved = false;
        cv::Vec3d position_3d;  // X, Y, Z in meters
        double distance_3d = 0.0;
        cv::Vec3d rotation_3d;  // Rotation vector
        
        // Targeting calculations
        double yaw_angle = 0.0;      // Horizontal angle to target
        double pitch_angle = 0.0;    // Vertical angle to target
        double flight_time = 0.0;    // Projectile flight time
        double bullet_drop = 0.0;    // Gravity compensation
        
        // Motion prediction
        cv::Vec3d velocity_3d;       // Target velocity
        cv::Vec3d predicted_pos;     // Where target will be
        
        // Firing solution
        bool can_fire = false;
        double firing_yaw = 0.0;     // Final aiming angles
        double firing_pitch = 0.0;
        double hit_probability = 0.0;
        
        // Competition data
        rm::ArmorID target_id = rm::ARMOR_ID_UNKNOWN;
        int target_priority = 0;
        bool is_valid_target = false;
    };
    
    // Target tracking history
    std::queue<FullTargetingResult> target_history;
    static const size_t MAX_HISTORY = 10;
    
    // Ballistics parameters
    const double BULLET_SPEED = 30.0;  // m/s
    const double GRAVITY = 9.81;       // m/s²
    
    // Known armor dimensions (RoboMaster standard)
    std::vector<cv::Point3f> small_armor_points = {
        cv::Point3f(-67.5, -27.5, 0),  // mm
        cv::Point3f( 67.5, -27.5, 0),
        cv::Point3f( 67.5,  27.5, 0),
        cv::Point3f(-67.5,  27.5, 0)
    };
    
    std::vector<cv::Point3f> large_armor_points = {
        cv::Point3f(-115, -27.5, 0),   // mm  
        cv::Point3f( 115, -27.5, 0),
        cv::Point3f( 115,  27.5, 0),
        cv::Point3f(-115,  27.5, 0)
    };

public:
    bool initialize(int camera_id = 0) {
        std::cout << "🎯 Initializing RoboMaster Auto-Aim Demonstration System..." << std::endl;
        std::cout << "=====================================================" << std::endl;
        
        // Initialize OpenRM systems
        rm::message("Auto-Aim Demo Starting!", rm::MSG_NOTE);
        
        // Try to open camera
        cap.open(camera_id);
        if (!cap.isOpened()) {
            std::cout << "📷 Camera not available - running in SIMULATION MODE" << std::endl;
            simulation_mode = true;
        } else {
            std::cout << "📷 Camera opened successfully - LIVE MODE available" << std::endl;
            simulation_mode = false;
            
            // Set camera properties
            cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
            cap.set(cv::CAP_PROP_FPS, 60);
        }
        
        std::cout << "⚙️  System Configuration:" << std::endl;
        std::cout << "   - Our Team: " << (our_team == rm::TEAM_COLOR_BLUE ? "BLUE" : "RED") << std::endl;
        std::cout << "   - Enemy Color: " << (enemy_color == rm::ARMOR_COLOR_RED ? "RED" : "BLUE") << std::endl;
        std::cout << "   - Bullet Speed: " << BULLET_SPEED << " m/s" << std::endl;
        std::cout << "   - Camera Matrix: " << camera_matrix.at<double>(0,0) << "px focal length" << std::endl;
        
        start_time = getTime();
        is_running = true;
        return true;
    }
    
    void run() {
        if (!is_running) return;
        
        std::cout << "\n🚀 Starting RoboMaster Auto-Aim Demonstration..." << std::endl;
        std::cout << "🎮 Controls:" << std::endl;
        std::cout << "   - 'q': Quit demonstration" << std::endl;
        std::cout << "   - 't': Toggle target color (Red/Blue)" << std::endl;
        std::cout << "   - 's': Switch between Live/Simulation mode" << std::endl;
        std::cout << "   - 'f': Simulate firing" << std::endl;
        std::cout << "   - 'r': Reset match statistics" << std::endl;
        std::cout << "   - '+/-': Adjust area threshold" << std::endl;
        std::cout << "   - 'a/z': Adjust aspect ratio" << std::endl;
        std::cout << "   - SPACE: Pause/Resume" << std::endl;
        
        cv::Mat frame;
        bool paused = false;
        
        // Create windows
        cv::namedWindow("RoboMaster Auto-Aim - Main View", cv::WINDOW_AUTOSIZE);
        cv::namedWindow("RoboMaster Auto-Aim - Tactical Display", cv::WINDOW_AUTOSIZE);
        cv::namedWindow("RoboMaster Auto-Aim - Match Stats", cv::WINDOW_AUTOSIZE);
        
        while (is_running) {
            auto current_time = getTime();
            match_time = getDoubleOfS(start_time, current_time);
            
            if (!paused) {
                // Get frame (live or simulated)
                if (simulation_mode) {
                    frame = createSimulatedFrame();
                } else {
                    cap >> frame;
                    if (frame.empty()) {
                        std::cout << "📷 Camera frame empty, switching to simulation" << std::endl;
                        simulation_mode = true;
                        continue;
                    }
                }
                
                frame_count++;
                
                // Run complete auto-aim pipeline
                FullTargetingResult result = runCompleteAutoAim(frame);
                
                // Update tracking history
                updateTargetHistory(result);
                
                // Create visualizations
                cv::Mat main_display = createMainDisplay(frame, result, current_time);
                cv::Mat tactical_display = createTacticalDisplay(result);
                cv::Mat stats_display = createStatsDisplay(current_time);
                
                // Show displays
                cv::imshow("RoboMaster Auto-Aim - Main View", main_display);
                cv::imshow("RoboMaster Auto-Aim - Tactical Display", tactical_display);
                cv::imshow("RoboMaster Auto-Aim - Match Stats", stats_display);
                
                // Simulate firing if auto-fire enabled and target locked
                if (result.can_fire && result.hit_probability > 0.7) {
                    simulateFiring(result);
                }
            }
            
            // Handle controls
            char key = cv::waitKey(1) & 0xFF;
            if (key == 'q' || key == 'Q') {
                break;
            } else if (key == 't' || key == 'T') {
                toggleTargetColor();
            } else if (key == 's' || key == 'S') {
                simulation_mode = !simulation_mode;
                std::cout << "🔄 Switched to " << (simulation_mode ? "SIMULATION" : "LIVE") << " mode" << std::endl;
            } else if (key == 'f' || key == 'F') {
                if (!target_history.empty()) {
                    simulateFiring(target_history.back());
                }
            } else if (key == 'r' || key == 'R') {
                resetMatchStats();
            } else if (key == '+' || key == '=') {
                params.area_threshold += 50;
                std::cout << "🔧 Area threshold: " << params.area_threshold << std::endl;
            } else if (key == '-' || key == '_') {
                params.area_threshold = std::max(50.0, params.area_threshold - 50);
                std::cout << "🔧 Area threshold: " << params.area_threshold << std::endl;
            } else if (key == 'a' || key == 'A') {
                params.aspect_ratio_max += 0.5;
                std::cout << "🔧 Aspect ratio range: " << params.aspect_ratio_min << "-" << params.aspect_ratio_max << std::endl;
            } else if (key == 'z' || key == 'Z') {
                params.aspect_ratio_max = std::max(2.0, params.aspect_ratio_max - 0.5);
                std::cout << "🔧 Aspect ratio range: " << params.aspect_ratio_min << "-" << params.aspect_ratio_max << std::endl;
            } else if (key == ' ') {
                paused = !paused;
                std::cout << (paused ? "⏸️  PAUSED" : "▶️  RESUMED") << std::endl;
            }
        }
        
        cleanup();
    }

private:
    FullTargetingResult runCompleteAutoAim(const cv::Mat& frame) {
        FullTargetingResult result;
        
        // STEP 1: Computer Vision Detection
        result = performVisionDetection(frame);
        
        if (!result.armor_detected) {
            return result; // No target found
        }
        
        // STEP 2: 3D Position Estimation (PnP)
        result = calculate3DPosition(result);
        
        if (!result.position_solved) {
            return result; // Couldn't solve 3D position
        }
        
        // STEP 3: Motion Prediction
        result = predictTargetMotion(result);
        
        // STEP 4: Ballistics Calculation
        result = calculateFiringSolution(result);
        
        // STEP 5: Target Validation & Priority
        result = validateTarget(result);
        
        return result;
    }
    
    FullTargetingResult performVisionDetection(const cv::Mat& frame) {
        FullTargetingResult result;
        
        // Color space conversion
        cv::Mat hsv, mask;
        cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);
        
        // Create color mask
        if (enemy_color == rm::ARMOR_COLOR_RED) {
            cv::Mat mask1, mask2;
            cv::inRange(hsv, cv::Scalar(0, 100, 100), cv::Scalar(10, 255, 255), mask1);
            cv::inRange(hsv, cv::Scalar(170, 100, 100), cv::Scalar(180, 255, 255), mask2);
            mask = mask1 | mask2;
        } else {
            cv::inRange(hsv, cv::Scalar(100, 100, 100), cv::Scalar(130, 255, 255), mask);
        }
        
        // Morphological operations (match vision_processing parameters)
        cv::Mat kernel_erode = cv::getStructuringElement(cv::MORPH_ELLIPSE, 
                                                        cv::Size(params.erosion_size, params.erosion_size));
        cv::Mat kernel_dilate = cv::getStructuringElement(cv::MORPH_ELLIPSE, 
                                                         cv::Size(params.dilation_size, params.dilation_size));
        cv::erode(mask, mask, kernel_erode);
        cv::dilate(mask, mask, kernel_dilate);
        
        // Find contours
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        
        // Analyze contours
        double best_score = 0;
        int best_contour = -1;
        
        for (int i = 0; i < contours.size(); i++) {
            double area = cv::contourArea(contours[i]);
            if (area < params.area_threshold) continue;
            
            cv::RotatedRect rect = cv::minAreaRect(contours[i]);
            double aspect_ratio = std::max(rect.size.width, rect.size.height) / 
                                 std::min(rect.size.width, rect.size.height);
            
            if (aspect_ratio >= params.aspect_ratio_min && aspect_ratio <= params.aspect_ratio_max) {
                double score = area / (aspect_ratio * 100);  // Match vision_processing scoring
                if (score > best_score) {
                    best_score = score;
                    best_contour = i;
                }
            }
        }
        
        // Extract results
        if (best_contour >= 0) {
            result.armor_detected = true;
            result.confidence = std::min(100.0, best_score / 10.0);
            result.detected_color = enemy_color;
            
            cv::RotatedRect rect = cv::minAreaRect(contours[best_contour]);
            result.armor_bbox = rect.boundingRect();
            result.center_2d = rect.center;
            
            // Get precise corner points
            cv::Point2f corners[4];
            rect.points(corners);
            
            // Order corners consistently (top-left, top-right, bottom-right, bottom-left)
            std::vector<cv::Point2f> ordered_corners;
            for (int i = 0; i < 4; i++) {
                ordered_corners.push_back(corners[i]);
            }
            
            // Simple ordering by y-coordinate then x-coordinate
            std::sort(ordered_corners.begin(), ordered_corners.end(), 
                     [](const cv::Point2f& a, const cv::Point2f& b) {
                         if (abs(a.y - b.y) < 5) return a.x < b.x;
                         return a.y < b.y;
                     });
            
            result.armor_corners = ordered_corners;
            
            // Determine armor size
            double area = cv::contourArea(contours[best_contour]);
            result.armor_size = (area > 2000) ? rm::ARMOR_SIZE_BIG_ARMOR : rm::ARMOR_SIZE_SMALL_ARMOR;
        }
        
        return result;
    }
    
    FullTargetingResult calculate3DPosition(FullTargetingResult result) {
        if (!result.armor_detected || result.armor_corners.size() != 4) {
            return result;
        }
        
        // Select armor model based on detected size
        std::vector<cv::Point3f> armor_model = (result.armor_size == rm::ARMOR_SIZE_BIG_ARMOR) 
                                               ? large_armor_points : small_armor_points;
        
        // Solve PnP
        cv::Mat rvec, tvec;
        bool success = cv::solvePnP(armor_model, result.armor_corners,
                                   camera_matrix, distortion_coeffs,
                                   rvec, tvec, false, cv::SOLVEPNP_IPPE);
        
        if (success) {
            // Extract position (tvec is already in mm from PnP solver)
            result.position_3d = cv::Vec3d(tvec.at<double>(0),  // Keep in mm for now
                                          tvec.at<double>(1),
                                          tvec.at<double>(2));
            
            result.rotation_3d = cv::Vec3d(rvec.at<double>(0),
                                          rvec.at<double>(1), 
                                          rvec.at<double>(2));
            
            // Check if PnP solution is valid (not NaN)
            if (std::isfinite(result.position_3d[0]) && std::isfinite(result.position_3d[1]) && std::isfinite(result.position_3d[2])) {
                result.position_solved = true;
                // Use 3D distance but convert to cm for display consistency
                result.distance_3d = cv::norm(result.position_3d) / 10.0;  // mm to cm
            } else {
                // PnP returned NaN - force fallback
                success = false;
            }
            
            // Fallback: if 3D distance seems wrong, use 2D approximation like vision_processing
            bool using_fallback = false;
            if (!success || result.distance_3d < 5.0 || result.distance_3d > 1000.0) {
                // Use the working 2D method as fallback
                std::vector<cv::Point> contour_points;
                for (const auto& corner : result.armor_corners) {
                    contour_points.push_back(cv::Point(corner.x, corner.y));
                }
                cv::RotatedRect rect = cv::minAreaRect(contour_points);
                double armor_width_pixels = std::max(rect.size.width, rect.size.height);
                result.distance_3d = (100.0 * 100.0) / armor_width_pixels; // Same as vision_processing
                
                // For 2D fallback, create reasonable 3D position estimate
                // Place target at calculated distance in front of camera
                double center_x = (result.armor_corners[0].x + result.armor_corners[2].x) / 2.0;
                double center_y = (result.armor_corners[0].y + result.armor_corners[2].y) / 2.0;
                
                // Convert pixel coordinates to world coordinates using simple projection
                double world_x = (center_x - 320.0) * result.distance_3d * 10.0 / 800.0; // mm
                double world_y = (center_y - 240.0) * result.distance_3d * 10.0 / 800.0; // mm  
                double world_z = result.distance_3d * 10.0; // mm (distance from camera)
                
                result.position_3d = cv::Vec3d(world_x, -world_y, world_z); // Y inverted for camera coordinate
                result.position_solved = true;
                using_fallback = true;
            }
            
            // Store method used for debugging
            result.confidence = using_fallback ? std::max(0.0, result.confidence - 10) : result.confidence; // Slight penalty for fallback
            
            // Calculate angles with safety checks and debugging
            if (std::isfinite(result.position_3d[0]) && std::isfinite(result.position_3d[1]) && std::isfinite(result.position_3d[2])) {
                if (result.position_3d[0] != 0.0 || result.position_3d[2] != 0.0) {
                    result.yaw_angle = atan2(result.position_3d[0], result.position_3d[2]) * 180.0 / M_PI;
                    
                    double horizontal_dist = sqrt(result.position_3d[0] * result.position_3d[0] + 
                                                 result.position_3d[2] * result.position_3d[2]);
                    if (horizontal_dist > 0.001) {  // Avoid division by zero
                        result.pitch_angle = atan2(-result.position_3d[1], horizontal_dist) * 180.0 / M_PI;
                    } else {
                        result.pitch_angle = 0.0;
                    }
                } else {
                    result.yaw_angle = 0.0;
                    result.pitch_angle = 0.0;
                }
            } else {
                // Debug: Print problematic position values
                std::cout << "⚠️  Invalid 3D position: [" << result.position_3d[0] << ", " 
                         << result.position_3d[1] << ", " << result.position_3d[2] << "]" << std::endl;
                result.yaw_angle = 0.0;
                result.pitch_angle = 0.0;
            }
        } else {
            // PnP failed completely - use 2D fallback method
            std::vector<cv::Point> contour_points;
            for (const auto& corner : result.armor_corners) {
                contour_points.push_back(cv::Point(corner.x, corner.y));
            }
            cv::RotatedRect rect = cv::minAreaRect(contour_points);
            double armor_width_pixels = std::max(rect.size.width, rect.size.height);
            result.distance_3d = (100.0 * 100.0) / armor_width_pixels; // Same as vision_processing
            
            // Create reasonable 3D position estimate from 2D
            double center_x = (result.armor_corners[0].x + result.armor_corners[2].x) / 2.0;
            double center_y = (result.armor_corners[0].y + result.armor_corners[2].y) / 2.0;
            
            // Convert pixel coordinates to world coordinates
            double world_x = (center_x - 320.0) * result.distance_3d * 10.0 / 800.0; // mm
            double world_y = (center_y - 240.0) * result.distance_3d * 10.0 / 800.0; // mm  
            double world_z = result.distance_3d * 10.0; // mm
            
            result.position_3d = cv::Vec3d(world_x, -world_y, world_z);
            result.position_solved = true;
            
            // Calculate angles from 2D-derived 3D position
            result.yaw_angle = atan2(result.position_3d[0], result.position_3d[2]) * 180.0 / M_PI;
            double horizontal_dist = sqrt(result.position_3d[0] * result.position_3d[0] + 
                                         result.position_3d[2] * result.position_3d[2]);
            if (horizontal_dist > 0.001) {
                result.pitch_angle = atan2(-result.position_3d[1], horizontal_dist) * 180.0 / M_PI;
            } else {
                result.pitch_angle = 0.0;
            }
            
            result.confidence = std::max(0.0, result.confidence - 20); // Penalty for complete PnP failure
        }
        
        return result;
    }
    
    FullTargetingResult predictTargetMotion(FullTargetingResult result) {
        if (!result.position_solved) {
            return result;
        }
        
        // Simple motion prediction using history
        if (target_history.size() >= 2) {
            auto prev_result = target_history.back();
            if (prev_result.position_solved) {
                // Calculate velocity
                double dt = 0.033; // Assume 30 FPS
                result.velocity_3d = cv::Vec3d(
                    (result.position_3d[0] - prev_result.position_3d[0]) / dt,
                    (result.position_3d[1] - prev_result.position_3d[1]) / dt,
                    (result.position_3d[2] - prev_result.position_3d[2]) / dt
                );
                
                // Predict future position
                result.flight_time = result.distance_3d / BULLET_SPEED;
                result.predicted_pos = cv::Vec3d(
                    result.position_3d[0] + result.velocity_3d[0] * result.flight_time,
                    result.position_3d[1] + result.velocity_3d[1] * result.flight_time,
                    result.position_3d[2] + result.velocity_3d[2] * result.flight_time
                );
            } else {
                result.predicted_pos = result.position_3d;
            }
        } else {
            result.predicted_pos = result.position_3d;
        }
        
        return result;
    }
    
    FullTargetingResult calculateFiringSolution(FullTargetingResult result) {
        if (!result.position_solved) {
            return result;
        }
        
        // Calculate flight time to predicted position
        double predicted_distance = cv::norm(result.predicted_pos);
        result.flight_time = predicted_distance / BULLET_SPEED;
        
        // Calculate bullet drop due to gravity
        result.bullet_drop = 0.5 * GRAVITY * result.flight_time * result.flight_time;
        
        // Calculate firing angles with compensation and safety checks
        if (std::isfinite(result.predicted_pos[0]) && std::isfinite(result.predicted_pos[1]) && std::isfinite(result.predicted_pos[2])) {
            if (result.predicted_pos[0] != 0.0 || result.predicted_pos[2] != 0.0) {
                result.firing_yaw = atan2(result.predicted_pos[0], result.predicted_pos[2]) * 180.0 / M_PI;
                
                double horizontal_dist = sqrt(result.predicted_pos[0] * result.predicted_pos[0] + 
                                             result.predicted_pos[2] * result.predicted_pos[2]);
                if (horizontal_dist > 0.001) {  // Avoid division by zero
                    result.firing_pitch = atan2(-(result.predicted_pos[1] - result.bullet_drop), horizontal_dist) * 180.0 / M_PI;
                } else {
                    result.firing_pitch = 0.0;
                }
            } else {
                result.firing_yaw = 0.0;
                result.firing_pitch = 0.0;
            }
        } else {
            // Debug: Print problematic predicted position
            std::cout << "⚠️  Invalid predicted position: [" << result.predicted_pos[0] << ", " 
                     << result.predicted_pos[1] << ", " << result.predicted_pos[2] << "]" << std::endl;
            result.firing_yaw = 0.0;
            result.firing_pitch = 0.0;
        }
        
        // Calculate hit probability based on various factors
        double distance_factor = std::max(0.0, 1.0 - result.distance_3d / 8.0);  // Closer = better
        double confidence_factor = result.confidence / 100.0;
        double velocity_factor = cv::norm(result.velocity_3d) < 2.0 ? 1.0 : 0.5;  // Slower targets easier
        
        result.hit_probability = distance_factor * confidence_factor * velocity_factor;
        result.can_fire = result.hit_probability > 0.5 && result.distance_3d > 1.0 && result.distance_3d < 8.0;
        
        return result;
    }
    
    FullTargetingResult validateTarget(FullTargetingResult result) {
        if (!result.position_solved) {
            return result;
        }
        
        // Simulate different robot types (for demonstration)
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> robot_type_dist(1, 5);
        
        int robot_type = robot_type_dist(gen);
        switch (robot_type) {
            case 1: result.target_id = rm::ARMOR_ID_HERO; result.target_priority = 1; break;
            case 2: result.target_id = rm::ARMOR_ID_SENTRY; result.target_priority = 2; break;
            case 3: result.target_id = rm::ARMOR_ID_INFANTRY_3; result.target_priority = 3; break;
            case 4: result.target_id = rm::ARMOR_ID_INFANTRY_4; result.target_priority = 4; break;
            case 5: result.target_id = rm::ARMOR_ID_INFANTRY_5; result.target_priority = 5; break;
        }
        
        // Validate using OpenRM attack system
        char valid_mask = rm::VALID_BYTE_MASK_ALL;  // Allow all targets for demo
        result.is_valid_target = rm::isValidArmorID(result.target_id, valid_mask);
        
        return result;
    }
    
    cv::Mat createSimulatedFrame() {
        // Create a simulated battlefield frame
        cv::Mat frame = cv::Mat::zeros(480, 640, CV_8UC3);
        
        // Add background
        cv::rectangle(frame, cv::Rect(0, 0, 640, 480), cv::Scalar(40, 40, 40), -1);
        
        // Add simulated armor plates moving around
        static double time_offset = 0;
        time_offset += 0.05;
        
        // Simulated enemy robot
        int center_x = 320 + 150 * sin(time_offset);
        int center_y = 240 + 100 * cos(time_offset * 0.7);
        
        cv::Scalar armor_color = (enemy_color == rm::ARMOR_COLOR_RED) ? 
                                cv::Scalar(0, 0, 255) : cv::Scalar(255, 0, 0);
        
        // Draw armor plate
        cv::RotatedRect armor_rect(cv::Point2f(center_x, center_y), 
                                  cv::Size2f(80, 40), time_offset * 10);
        
        cv::Point2f vertices[4];
        armor_rect.points(vertices);
        
        std::vector<cv::Point> armor_points;
        for (int i = 0; i < 4; i++) {
            armor_points.push_back(cv::Point(vertices[i]));
        }
        
        cv::fillPoly(frame, armor_points, armor_color);
        
        // Add some noise and realistic elements
        cv::putText(frame, "SIMULATED BATTLEFIELD", cv::Point(10, 30), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(100, 100, 100), 2);
        
        // Add timestamp
        cv::putText(frame, "T+" + std::to_string((int)match_time) + "s", 
                   cv::Point(540, 30), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 1);
        
        return frame;
    }
    
    cv::Mat createMainDisplay(const cv::Mat& frame, const FullTargetingResult& result, const TimePoint& current_time) {
        cv::Mat display = frame.clone();
        
        // Performance info
        double elapsed = getDoubleOfS(start_time, current_time);
        double fps = frame_count / elapsed;
        
        // Header
        cv::putText(display, "RoboMaster Auto-Aim System", 
                   cv::Point(10, 25), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 255), 2);
        
        std::string fps_text = "FPS: " + std::to_string((int)fps);
        cv::putText(display, fps_text, 
                   cv::Point(10, 50), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
        
        // Detection status
        if (result.armor_detected) {
            cv::putText(display, "TARGET LOCKED", 
                       cv::Point(10, 80), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
            
            // Display confidence level
            char conf_buffer[32];
            sprintf(conf_buffer, "%.1f", result.confidence);
            std::string conf_text = "Confidence: " + std::string(conf_buffer) + "%";
            
            // Color code confidence: Green (>80%), Yellow (50-80%), Red (<50%)
            cv::Scalar conf_color;
            if (result.confidence >= 80.0) {
                conf_color = cv::Scalar(0, 255, 0);  // Green
            } else if (result.confidence >= 50.0) {
                conf_color = cv::Scalar(0, 255, 255);  // Yellow
            } else {
                conf_color = cv::Scalar(0, 100, 255);  // Orange/Red
            }
            
            cv::putText(display, conf_text, 
                       cv::Point(10, 110), cv::FONT_HERSHEY_SIMPLEX, 0.6, conf_color, 2);
            
            // Draw detection box
            cv::rectangle(display, result.armor_bbox, cv::Scalar(0, 255, 0), 3);
            
            // Draw corner points
            for (const auto& corner : result.armor_corners) {
                cv::circle(display, corner, 5, cv::Scalar(255, 0, 0), -1);
            }
            
            // Draw center crosshair
            cv::Point center((int)result.center_2d.x, (int)result.center_2d.y);
            cv::line(display, cv::Point(center.x-20, center.y), 
                    cv::Point(center.x+20, center.y), cv::Scalar(0, 0, 255), 3);
            cv::line(display, cv::Point(center.x, center.y-20), 
                    cv::Point(center.x, center.y+20), cv::Scalar(0, 0, 255), 3);
            
            // 3D Position info
            if (result.position_solved) {
                std::string pos_text = "3D Pos: (" + 
                    std::to_string((int)(result.position_3d[0] / 10)) + "," +
                    std::to_string((int)(result.position_3d[1] / 10)) + "," +
                    std::to_string((int)(result.position_3d[2] / 10)) + ")cm";
                cv::putText(display, pos_text, 
                           cv::Point(10, 140), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 0), 1);
                
                std::string dist_text = "Distance: " + std::to_string((int)result.distance_3d) + "cm";
                cv::putText(display, dist_text, 
                           cv::Point(10, 160), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 0), 1);
                
                // Safe angle display with validation and improved formatting
                std::string yaw_str, pitch_str;
                if (std::isfinite(result.yaw_angle) && !std::isnan(result.yaw_angle)) {
                    // Debug: Print actual angle values occasionally
                    static int debug_counter = 0;
                    if (debug_counter++ % 30 == 0) {
                        std::cout << "🔧 Debug angles: yaw=" << result.yaw_angle << "°, pitch=" << result.pitch_angle << "°" << std::endl;
                    }
                    // Use sprintf for guaranteed string formatting with 3 decimal places
                    char yaw_buffer[32];
                    sprintf(yaw_buffer, "%.3f", result.yaw_angle);
                    yaw_str = std::string(yaw_buffer);
                } else {
                    yaw_str = "ERR";
                }
                
                if (std::isfinite(result.pitch_angle) && !std::isnan(result.pitch_angle)) {
                    char pitch_buffer[32];
                    sprintf(pitch_buffer, "%.3f", result.pitch_angle);
                    pitch_str = std::string(pitch_buffer);
                } else {
                    pitch_str = "ERR";
                }
                
                std::string angle_text = "Angles: Y=" + yaw_str + "deg P=" + pitch_str + "deg";
                
                // Debug: Print the actual strings being displayed
                static int display_debug_counter = 0;
                if (display_debug_counter++ % 30 == 0) {
                    std::cout << "📺 Display strings: yaw_str='" << yaw_str << "', pitch_str='" << pitch_str << "'" << std::endl;
                    std::cout << "📺 Full text: '" << angle_text << "'" << std::endl;
                }
                
                cv::putText(display, angle_text, 
                           cv::Point(10, 180), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 0), 1);
                
                // Firing solution
                if (result.can_fire) {
                    cv::putText(display, "FIRING SOLUTION READY", 
                               cv::Point(10, 200), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
                    
                    // Safe firing angle display with improved formatting
                    std::string fire_yaw_str, fire_pitch_str;
                    if (std::isfinite(result.firing_yaw) && !std::isnan(result.firing_yaw)) {
                        char fire_yaw_buffer[32];
                        sprintf(fire_yaw_buffer, "%.3f", result.firing_yaw);
                        fire_yaw_str = std::string(fire_yaw_buffer);
                    } else {
                        fire_yaw_str = "ERR";
                    }
                    
                    if (std::isfinite(result.firing_pitch) && !std::isnan(result.firing_pitch)) {
                        char fire_pitch_buffer[32];
                        sprintf(fire_pitch_buffer, "%.3f", result.firing_pitch);
                        fire_pitch_str = std::string(fire_pitch_buffer);
                    } else {
                        fire_pitch_str = "ERR";
                    }
                    
                    std::string fire_text = "Fire: Y=" + fire_yaw_str + "deg P=" + fire_pitch_str + "deg";
                    cv::putText(display, fire_text, 
                               cv::Point(10, 220), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
                    
                    std::string prob_text = "Hit Probability: " + std::to_string((int)(result.hit_probability * 100)) + "%";
                    cv::putText(display, prob_text, 
                               cv::Point(10, 240), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
                    
                    // Draw firing indicator
                    cv::circle(display, center, 30, cv::Scalar(0, 255, 0), 3);
                    cv::putText(display, "FIRE", cv::Point(center.x - 15, center.y + 5), 
                               cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 2);
                } else {
                    cv::putText(display, "CALCULATING...", 
                               cv::Point(10, 180), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 0), 2);
                }
            }
            
        } else {
            cv::putText(display, "SEARCHING TARGETS...", 
                       cv::Point(10, 80), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 255), 2);
        }
        
        // Mode indicator
        std::string mode_text = simulation_mode ? "SIMULATION MODE" : "LIVE MODE";
        cv::putText(display, mode_text, 
                   cv::Point(display.cols - 200, 25), cv::FONT_HERSHEY_SIMPLEX, 0.6, 
                   simulation_mode ? cv::Scalar(255, 255, 0) : cv::Scalar(0, 255, 0), 2);
        
        return display;
    }
    
    cv::Mat createTacticalDisplay(const FullTargetingResult& result) {
        cv::Mat tactical = cv::Mat::zeros(400, 400, CV_8UC3);
        
        // Background
        cv::rectangle(tactical, cv::Rect(0, 0, 400, 400), cv::Scalar(20, 20, 20), -1);
        
        // Title
        cv::putText(tactical, "TACTICAL DISPLAY", 
                   cv::Point(10, 25), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
        
        // Draw coordinate system
        cv::Point center(200, 300);  // Our robot position
        cv::circle(tactical, center, 5, cv::Scalar(0, 255, 0), -1);
        cv::putText(tactical, "US", cv::Point(center.x - 10, center.y + 20), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 255, 0), 1);
        
        // Draw range circles
        for (int r = 1; r <= 8; r += 2) {
            int pixel_radius = r * 20;  // 20 pixels per meter
            cv::circle(tactical, center, pixel_radius, cv::Scalar(50, 50, 50), 1);
            cv::putText(tactical, std::to_string(r) + "m", 
                       cv::Point(center.x + pixel_radius - 10, center.y), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.3, cv::Scalar(100, 100, 100), 1);
        }
        
        // Draw target if detected
        if (result.position_solved) {
            // Convert 3D position to tactical display coordinates
            int target_x = center.x + (int)(result.position_3d[0] * 20);  // 20 pixels per meter
            int target_z = center.y - (int)(result.position_3d[2] * 20);  // Invert Z for display
            
            cv::Point target_pos(target_x, target_z);
            
            // Draw target
            cv::circle(tactical, target_pos, 8, cv::Scalar(0, 0, 255), -1);
            cv::putText(tactical, "ENEMY", cv::Point(target_pos.x - 15, target_pos.y - 15), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 0, 255), 1);
            
            // Draw line from us to target
            cv::line(tactical, center, target_pos, cv::Scalar(255, 255, 0), 2);
            
            // Draw predicted position
            if (cv::norm(result.velocity_3d) > 0.1) {
                int pred_x = center.x + (int)(result.predicted_pos[0] * 20);
                int pred_z = center.y - (int)(result.predicted_pos[2] * 20);
                cv::Point pred_pos(pred_x, pred_z);
                
                cv::circle(tactical, pred_pos, 6, cv::Scalar(255, 0, 255), 2);
                cv::line(tactical, target_pos, pred_pos, cv::Scalar(255, 0, 255), 1);
                cv::putText(tactical, "PRED", cv::Point(pred_pos.x - 15, pred_pos.y - 15), 
                           cv::FONT_HERSHEY_SIMPLEX, 0.3, cv::Scalar(255, 0, 255), 1);
            }
            
            // Target info
            std::string info = "D: " + std::to_string((int)result.distance_3d) + "cm";
            cv::putText(tactical, info, cv::Point(10, 360), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
            
            std::string vel_info = "V: " + std::to_string((int)(cv::norm(result.velocity_3d) * 100)) + "cm/s";
            cv::putText(tactical, vel_info, cv::Point(10, 380), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
        }
        
        return tactical;
    }
    
    cv::Mat createStatsDisplay(const TimePoint& current_time) {
        cv::Mat stats = cv::Mat::zeros(300, 400, CV_8UC3);
        
        // Background
        cv::rectangle(stats, cv::Rect(0, 0, 400, 300), cv::Scalar(30, 30, 30), -1);
        
        // Title
        cv::putText(stats, "MATCH STATISTICS", 
                   cv::Point(10, 25), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
        
        // Match time
        int minutes = (int)match_time / 60;
        int seconds = (int)match_time % 60;
        std::string time_text = "Match Time: " + std::to_string(minutes) + ":" + 
                               (seconds < 10 ? "0" : "") + std::to_string(seconds);
        cv::putText(stats, time_text, 
                   cv::Point(10, 60), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 2);
        
        // Performance stats
        double elapsed = getDoubleOfS(start_time, current_time);
        double fps = frame_count / elapsed;
        
        cv::putText(stats, "Performance:", 
                   cv::Point(10, 100), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 0), 2);
        
        std::string fps_text = "  FPS: " + std::to_string((int)fps);
        cv::putText(stats, fps_text, 
                   cv::Point(10, 125), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
        
        std::string frames_text = "  Frames: " + std::to_string(frame_count);
        cv::putText(stats, frames_text, 
                   cv::Point(10, 145), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
        
        // Combat stats
        cv::putText(stats, "Combat:", 
                   cv::Point(10, 180), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 0), 2);
        
        std::string shots_text = "  Shots Fired: " + std::to_string(shots_fired);
        cv::putText(stats, shots_text, 
                   cv::Point(10, 205), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
        
        std::string hits_text = "  Hits: " + std::to_string(hits_scored);
        cv::putText(stats, hits_text, 
                   cv::Point(10, 225), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
        
        double accuracy = (shots_fired > 0) ? (double)hits_scored / shots_fired * 100.0 : 0.0;
        std::string acc_text = "  Accuracy: " + std::to_string((int)accuracy) + "%";
        cv::putText(stats, acc_text, 
                   cv::Point(10, 245), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
        
        // Target info
        if (!target_history.empty() && target_history.back().armor_detected) {
            auto& last_target = target_history.back();
            cv::putText(stats, "Current Target:", 
                       cv::Point(200, 100), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 0), 1);
            
            std::string target_type = "  Type: ";
            switch (last_target.target_id) {
                case rm::ARMOR_ID_HERO: target_type += "HERO"; break;
                case rm::ARMOR_ID_SENTRY: target_type += "SENTRY"; break;
                case rm::ARMOR_ID_INFANTRY_3: target_type += "INF-3"; break;
                case rm::ARMOR_ID_INFANTRY_4: target_type += "INF-4"; break;
                case rm::ARMOR_ID_INFANTRY_5: target_type += "INF-5"; break;
                default: target_type += "UNKNOWN"; break;
            }
            cv::putText(stats, target_type, 
                       cv::Point(200, 120), cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(255, 255, 255), 1);
            
            std::string priority_text = "  Priority: " + std::to_string(last_target.target_priority);
            cv::putText(stats, priority_text, 
                       cv::Point(200, 140), cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(255, 255, 255), 1);
        }
        
        return stats;
    }
    
    void updateTargetHistory(const FullTargetingResult& result) {
        target_history.push(result);
        if (target_history.size() > MAX_HISTORY) {
            target_history.pop();
        }
    }
    
    void simulateFiring(const FullTargetingResult& result) {
        if (!result.can_fire) return;
        
        shots_fired++;
        
        // Simulate hit probability
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_real_distribution<> hit_dist(0.0, 1.0);
        
        bool hit = hit_dist(gen) < result.hit_probability;
        if (hit) {
            hits_scored++;
            rm::message("🎯 HIT! Target eliminated", rm::MSG_NOTE);
            std::cout << "🎯 DIRECT HIT! Accuracy: " << result.hit_probability * 100 << "%" << std::endl;
        } else {
            rm::message("❌ MISS! Adjusting aim", rm::MSG_WARNING);
            std::cout << "❌ Shot missed. Recalibrating..." << std::endl;
        }
        
        // Simulate serial communication to turret
        std::cout << "📡 Turret Command: Yaw=" << result.firing_yaw << "° Pitch=" << result.firing_pitch << "°" << std::endl;
    }
    
    void toggleTargetColor() {
        enemy_color = (enemy_color == rm::ARMOR_COLOR_RED) ? rm::ARMOR_COLOR_BLUE : rm::ARMOR_COLOR_RED;
        std::string color_name = (enemy_color == rm::ARMOR_COLOR_RED) ? "RED" : "BLUE";
        std::cout << "🎯 Target color changed to: " << color_name << std::endl;
        rm::message("Target color: " + color_name, rm::MSG_NOTE);
    }
    
    void resetMatchStats() {
        hits_scored = 0;
        shots_fired = 0;
        start_time = getTime();
        frame_count = 0;
        std::cout << "🔄 Match statistics reset" << std::endl;
        rm::message("Match stats reset", rm::MSG_NOTE);
    }
    
    void cleanup() {
        if (cap.isOpened()) {
            cap.release();
        }
        cv::destroyAllWindows();
        
        auto end_time = getTime();
        double total_time = getDoubleOfS(start_time, end_time);
        double avg_fps = frame_count / total_time;
        
        std::cout << "\n🏁 RoboMaster Auto-Aim Demo Session Complete!" << std::endl;
        std::cout << "=================================================" << std::endl;
        std::cout << "⏱️  Total time: " << total_time << " seconds" << std::endl;
        std::cout << "🎬 Total frames: " << frame_count << std::endl;
        std::cout << "📈 Average FPS: " << avg_fps << std::endl;
        std::cout << "🔫 Shots fired: " << shots_fired << std::endl;
        std::cout << "🎯 Hits scored: " << hits_scored << std::endl;
        
        if (shots_fired > 0) {
            double accuracy = (double)hits_scored / shots_fired * 100.0;
            std::cout << "🎯 Accuracy: " << accuracy << "%" << std::endl;
        }
        
        rm::message("Auto-Aim demo ended. Avg FPS: " + std::to_string(avg_fps), rm::MSG_NOTE);
        is_running = false;
    }
};

int main() {
    std::cout << "🤖 RoboMaster Auto-Aim Complete System Demonstration" << std::endl;
    std::cout << "====================================================" << std::endl;
    std::cout << "🎯 This demo shows the complete auto-aim pipeline:" << std::endl;
    std::cout << "   1. Computer Vision Detection" << std::endl;
    std::cout << "   2. 3D Position Estimation (PnP)" << std::endl;
    std::cout << "   3. Motion Prediction" << std::endl;
    std::cout << "   4. Ballistics Calculation" << std::endl;
    std::cout << "   5. Target Validation" << std::endl;
    std::cout << "   6. Firing Decision" << std::endl;
    std::cout << "\n🚀 Initializing system..." << std::endl;
    
    RoboMasterAutoAimDemo demo;
    
    if (!demo.initialize(0)) {
        std::cout << "❌ Failed to initialize auto-aim system" << std::endl;
        return -1;
    }
    
    demo.run();
    
    std::cout << "\n🎉 RoboMaster Auto-Aim Demonstration Complete!" << std::endl;
    
    return 0;
}
