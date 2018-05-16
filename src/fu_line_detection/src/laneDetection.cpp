#include "laneDetection.h"
using namespace std;

// #define PAINT_OUTPUT 
#define PUBLISH_DEBUG_OUTPUT 

static const uint32_t MY_ROS_QUEUE_SIZE = 100;
#define PI 3.14159265

#define RATE_HZ 5

#define NUM_STATES 7
#define STATE_WIDTH 20

#define STATE_WIDTH_PIX 13

image_transport::CameraPublisher image_publisher_dbscan;
image_transport::CameraPublisher image_publisher;
image_transport::CameraPublisher image_publisher_ransac;
image_transport::CameraPublisher image_publisher_lane_markings;

double f_u;
double f_v;
double c_u;
double c_v;
double cam_deg;
double cam_height;

// ackerman model params
double kalpha, kbeta;
double polysAngle = 0;

std::string topico_estandarizado;

double actual_speed = 5;
float actual_steering = 0;
int car_center = 0;
int car_text_position = 150;

//msgs head
unsigned int head_sequence_id = 0;
ros::Time head_time_stamp;
std::string rgb_frame_id = "_rgb_optical_frame";
sensor_msgs::CameraInfoPtr rgb_camera_info;

// try kernel width 5 for now
const static int g_kernel1DWidth = 5;
dbscan::point_t *points;
std::string nombre_estado [NUM_STATES] = { "OL",   "LL",   "LC",   "CC",   "RC",   "RR",   "OR"};
int estado_actual = -1;
int estado_deseado = 4; //iniciar con estado RC

// PID kernel
double prevErrorPID = 0.0;
double integralPID = 0.0;
double dist_y_nextPoint = 0.0;

// obtener orientacion del carro con respecto a polylineas
double theta_carro = 1.5707;

cLaneDetectionFu::cLaneDetectionFu(ros::NodeHandle nh)
    : nh_(nh), priv_nh_("~")
{
    std::string node_name = ros::this_node::getName();
    ROS_INFO("Node name: %s",node_name.c_str());
    priv_nh_.param<std::string>(node_name+"/camera_name", camera_name, "/usb_cam/image_raw"); 

    priv_nh_.param<int>(node_name+"/cam_w", cam_w, 640);
    priv_nh_.param<int>(node_name+"/cam_h", cam_h, 480);
    priv_nh_.param<int>(node_name+"/proj_y_start", proj_y_start, 415);
    priv_nh_.param<int>(node_name+"/proj_image_h", proj_image_h, 40);
    priv_nh_.param<int>(node_name+"/proj_image_w", proj_image_w, 80);
    priv_nh_.param<int>(node_name+"/proj_image_horizontal_offset", proj_image_horizontal_offset, 0);
    priv_nh_.param<int>(node_name+"/roi_top_w", roi_top_w, 62);
    priv_nh_.param<int>(node_name+"/roi_bottom_w", roi_bottom_w, 30);

    priv_nh_.param<int>(node_name+"/maxYRoi", maxYRoi, 5);
    priv_nh_.param<int>(node_name+"/minYDefaultRoi", minYDefaultRoi, 39);
    priv_nh_.param<int>(node_name+"/minYPolyRoi", minYPolyRoi, 39);

    priv_nh_.param<int>(node_name+"/defaultXLeft", defaultXLeft, 10);
    priv_nh_.param<int>(node_name+"/defaultXCenter", defaultXCenter, 30);
    priv_nh_.param<int>(node_name+"/defaultXRight", defaultXRight, 50);
    priv_nh_.param<int>(node_name+"/defaultYHorizontal", defaultYHorizontal, 100);

    priv_nh_.param<int>(node_name+"/interestDistancePoly", interestDistancePoly, 10);
    priv_nh_.param<int>(node_name+"/interestDistanceDefault", interestDistanceDefault, 10);
    
    priv_nh_.param<int>(node_name+"/iterationsRansac", iterationsRansac, 10);
    priv_nh_.param<double>(node_name+"/proportionThreshould", proportionThreshould, 0.5);
    
    priv_nh_.param<int>(node_name+"/m_gradientThreshold", m_gradientThreshold, 10);
    priv_nh_.param<int>(node_name+"/m_nonMaxWidth", m_nonMaxWidth, 10);
    priv_nh_.param<int>(node_name+"/laneMarkingSquaredThreshold", laneMarkingSquaredThreshold, 25);

    priv_nh_.param<int>(node_name+"/angleAdjacentLeg", angleAdjacentLeg, 25);
    
    priv_nh_.param<int>(node_name+"/scanlinesVerticalDistance", scanlinesVerticalDistance, 1);
    priv_nh_.param<int>(node_name+"/scanlinesMaxCount", scanlinesMaxCount, 100);

    priv_nh_.param<int>(node_name+"/detectLaneStartX", detectLaneStartX, 38);

    priv_nh_.param<int>(node_name+"/maxAngleDiff", maxAngleDiff, 10);

    priv_nh_.param<int>(node_name+"/polyY1", polyY1, 35);
    priv_nh_.param<int>(node_name+"/polyY2", polyY2, 30);
    priv_nh_.param<int>(node_name+"/polyY3", polyY3, 15);

    int cam_h_half = cam_h/2;

    priv_nh_.param<double>(node_name+"/f_u", f_u, 624.650635); 
    priv_nh_.param<double>(node_name+"/f_v", f_v, 626.987244); 
    priv_nh_.param<double>(node_name+"/c_u", c_u, 309.703230); 
    priv_nh_.param<double>(node_name+"/c_v", c_v, 231.473613); 
    priv_nh_.param<double>(node_name+"/cam_deg", cam_deg, 27); 
    priv_nh_.param<double>(node_name+"/cam_height", cam_height, 18);

    priv_nh_.param<std::string>(node_name+"/topico_estandarizado", topico_estandarizado, "/standarized_vel_ste");
    priv_nh_.param<double>(node_name+"/kalpha", kalpha, 1); 
    priv_nh_.param<double>(node_name+"/kbeta", kbeta, 1); 

    priv_nh_.param<int>(node_name+"/car_center", car_center, 15);
    priv_nh_.param<int>(node_name+"/dbscan_epsilon", dbscan_epsilon, 30);
    priv_nh_.param<int>(node_name+"/dbscan_min_points", dbscan_min_points, 5);

    priv_nh_.param<double>(node_name+"/kp", kp, 0.5);
    priv_nh_.param<double>(node_name+"/ki", ki, 0.001);
    priv_nh_.param<double>(node_name+"/kd", kd, 0.0);

    ipMapper = IPMapper(cam_w, cam_h_half, f_u, f_v, c_u, c_v, cam_deg, cam_height);
    
    proj_image_w_half = proj_image_w/2;

    polyDetectedLeft     = false;
    polyDetectedCenter   = false;
    polyDetectedRight    = false;
    polyDetectedHorizontal = false;

    bestPolyLeft         = std::make_pair(NewtonPolynomial(), 0);
    bestPolyCenter       = std::make_pair(NewtonPolynomial(), 0);
    bestPolyRight        = std::make_pair(NewtonPolynomial(), 0);

    bestPolyHorizontal        = std::make_pair(NewtonPolynomial(), 0);

    laneMarkingsLeft     = std::vector<FuPoint<int>>();
    laneMarkingsCenter   = std::vector<FuPoint<int>>();
    laneMarkingsRight    = std::vector<FuPoint<int>>();

    polyLeft             = NewtonPolynomial();
    polyCenter           = NewtonPolynomial();
    polyRight            = NewtonPolynomial();
    polyHorizontal       = NewtonPolynomial();

    polyDefaultLeft     = NewtonPolynomial();
    polyDefaultCenter   = NewtonPolynomial();
    polyDefaultRight    = NewtonPolynomial();

    supportersLeft       = std::vector<FuPoint<int>>();
    supportersCenter     = std::vector<FuPoint<int>>();
    supportersRight      = std::vector<FuPoint<int>>();

    supportersHorizontal      = std::vector<FuPoint<int>>();

    prevPolyLeft         = NewtonPolynomial();
    prevPolyCenter       = NewtonPolynomial();
    prevPolyRight        = NewtonPolynomial();

    prevPolyHorizontal        = NewtonPolynomial();

    pointsLeft           = std::vector<FuPoint<int>>();
    pointsCenter         = std::vector<FuPoint<int>>();
    pointsRight          = std::vector<FuPoint<int>>();

    pointsHorizontal          = std::vector<FuPoint<int>>();

    lanePoly             = NewtonPolynomial();
    lanePolynomial       = LanePolynomial();

    maxDistance = 1;  

    lastAngle = 0; 

    head_time_stamp = ros::Time::now();
    
    read_images_ = nh.subscribe(nh_.resolveName(camera_name), MY_ROS_QUEUE_SIZE, &cLaneDetectionFu::ProcessInput,this);

    // sub_planning = nh.subscribe("/planning", MY_ROS_QUEUE_SIZE, &cLaneDetectionFu::ProcessPlanning,this);
    sub_localization = nh.subscribe("/localization_array", MY_ROS_QUEUE_SIZE, &cLaneDetectionFu::get_localization, this);
    sub_des_state = nh.subscribe("/desired_state", MY_ROS_QUEUE_SIZE, &cLaneDetectionFu::get_ctrl_desired_state, this);
    // planningxy = nh.subscribe("/planningxy", MY_ROS_QUEUE_SIZE, &cLaneDetectionFu::ProcessPlanningXY,this);

    // sub_vel = nh.subscribe(topico_estandarizado, MY_ROS_QUEUE_SIZE, &cLaneDetectionFu::get_ctrl_action, this);

    
    // no es muy recomendable leer velocidad y steering ya que pueden tener mucho error
    // sub_vel = nh.subscribe(topico_estandarizado, MY_ROS_QUEUE_SIZE, &cLaneDetectionFu::get_ctrl_action, this);
    // probar con imu


    publish_angle = nh.advertise<std_msgs::Float32>("/lane_model/angle", MY_ROS_QUEUE_SIZE);
    pub_right = nh.advertise<nav_msgs::GridCells>("/points/right", MY_ROS_QUEUE_SIZE);
    pub_center = nh.advertise<nav_msgs::GridCells>("/points/center", MY_ROS_QUEUE_SIZE);
    pub_left = nh.advertise<nav_msgs::GridCells>("/points/left", MY_ROS_QUEUE_SIZE);
    pub_horizontal = nh.advertise<nav_msgs::GridCells>("/points/horizontal", MY_ROS_QUEUE_SIZE);
    pub_ransac_right = nh.advertise<nav_msgs::GridCells>("/points/ransac_right", MY_ROS_QUEUE_SIZE);
    pub_ransac_center = nh.advertise<nav_msgs::GridCells>("/points/ransac_center", MY_ROS_QUEUE_SIZE);
    pub_ransac_left = nh.advertise<nav_msgs::GridCells>("/points/ransac_left", MY_ROS_QUEUE_SIZE);
    pub_lane_model = nh.advertise<nav_msgs::GridCells>("/points/lane_model", MY_ROS_QUEUE_SIZE);
    pub_ransac_horizontal = nh.advertise<nav_msgs::GridCells>("/points/ransac_horizontal", MY_ROS_QUEUE_SIZE);
    pub_speed = nh.advertise<geometry_msgs::Twist>(topico_estandarizado, MY_ROS_QUEUE_SIZE);



    pub_orientation = nh.advertise<std_msgs::Float32>("/car_orientation", MY_ROS_QUEUE_SIZE);

    image_transport::ImageTransport image_transport(nh);
    image_publisher = image_transport.advertiseCamera("/lane_model/lane_model_image", MY_ROS_QUEUE_SIZE);

    #ifdef PUBLISH_DEBUG_OUTPUT
        image_publisher_dbscan = image_transport.advertiseCamera("/lane_model/dbscan", MY_ROS_QUEUE_SIZE);
        image_publisher_ransac = image_transport.advertiseCamera("/lane_model/ransac", MY_ROS_QUEUE_SIZE);
        image_publisher_lane_markings = image_transport.advertiseCamera("/lane_model/lane_markings", MY_ROS_QUEUE_SIZE);
    #endif

    if (!rgb_camera_info)
    {
        rgb_camera_info.reset(new sensor_msgs::CameraInfo());
        rgb_camera_info->width = proj_image_w;
        rgb_camera_info->height = proj_image_h + 50;
    }

    // //ROS_INFO_STREAM("Finishing cLaneDetectionFu");
    //from camera properties and ROI etc we get scanlines (=line segments)
    //these line segments are lines in image on whose we look for edges
    //the outer vector represents rows on image, inner vector is vector of line segments of one row, usualy just one line segment
    //we should generate this only once in the beginning! or even just have it pregenerated for our cam
    scanlines = getScanlines();
}

cLaneDetectionFu::~cLaneDetectionFu()
{
}

void cLaneDetectionFu::ProcessInput(const sensor_msgs::Image::ConstPtr& msg)
{
    // clear some stuff from the last cycle
    bestPolyLeft = std::make_pair(NewtonPolynomial(), 0);
    bestPolyCenter = std::make_pair(NewtonPolynomial(), 0);
    bestPolyRight = std::make_pair(NewtonPolynomial(), 0);

    bestPolyHorizontal = std::make_pair(NewtonPolynomial(), 0);

    polyDefaultLeft.clear();
    polyDefaultCenter.clear();
    polyDefaultRight.clear();

    supportersLeft.clear();
    supportersCenter.clear();
    supportersRight.clear();

    supportersHorizontal.clear();

    //use ROS image_proc or opencv instead of ip mapper?
    cv_bridge::CvImagePtr cv_ptr;
    cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::MONO8);
    
    cv::Mat image = cv_ptr->image.clone();

    // ROI 
    Mat cut_image = image(cv::Rect(0, cam_h/2, cam_w, cam_h/2));
    Mat remapped_image = ipMapper.remap(cut_image);

    #ifdef PAINT_OUTPUT
        cv::imshow("IPmapped image", remapped_image);
        cv::waitKey(1);
    #endif

    cv::Mat transformedImage = remapped_image(cv::Rect((cam_w/2)-proj_image_w_half+proj_image_horizontal_offset,
            proj_y_start,proj_image_w,proj_image_h)).clone();
    
    cv::flip(transformedImage, transformedImage, 0);

    /*
    // DETECT HORIZONTAL LINES ADDED
    */

    #ifdef PAINT_OUTPUT
    	cv::imshow("Cut IPmapped image", transformedImage);   
    	cv::waitKey(1);
    #endif
    
    //scanLinesMat = transformedImage.clone();
    //scanlines = getScanlines();

    //scanlines -> edges (in each scanline we find maximum and minimum of kernel fn ~= where the edge is)
    //this is where we use input image!
    vector<vector<EdgePoint>> edges = cLaneDetectionFu::scanImage(transformedImage, RIGHT); // RIGHT es solo por poner algo diferente de HORIZONTAL
    vector<vector<EdgePoint>> edgesHorizontal = cLaneDetectionFu::scanImage(transformedImage, HORIZONTAL); // HORIZONTAL es para obtener lineas horizontales

    cv::Mat transformedImagePaintable;
    cv::Mat transformedImagePaintableHorizontal;

    //---------------------- DEBUG OUTPUT EDGES ---------------------------------//
    #ifdef PUBLISH_DEBUG_OUTPUT
        transformedImagePaintable = transformedImage.clone();
        cv::cvtColor(transformedImagePaintable,transformedImagePaintable,CV_GRAY2BGR);

        for(int i = 0; i < (int)edges.size(); i++)
        {
            for(int j=0; j < edges[i].size(); j++) {
                FuPoint<int> edge = edges[i][j].getImgPos();
                cv::Point edgeLoc = cv::Point(edge.getX(), edge.getY());
                cv::circle(transformedImagePaintable, edgeLoc, 0, cv::Scalar(0, 0, 0), -1);
            }            
        }

        // transformedImagePaintableHorizontal = transformedImage.clone();
        // cv::cvtColor(transformedImagePaintableHorizontal, transformedImagePaintableHorizontal, CV_GRAY2BGR);
        // for(int i = 0; i < (int)edgesHorizontal.size(); i++)
        // {
        //    for(int j=0; j < edgesHorizontal[i].size(); j++) {
        //        FuPoint<int> edge = edgesHorizontal[i][j].getImgPos();
        //        cv::Point edgeLoc = cv::Point(edge.getX(), edge.getY());
        //        cv::circle(transformedImagePaintableHorizontal,edgeLoc,1,cv::Scalar(0, 0, edgesHorizontal[i][j].getValue()), -1);
        //    }
        // }
    #ifdef PAINT_OUTPUT    
	        
        //cv::imshow("ROI, scanlines and edges", transformedImagePaintable);
        cv::imshow("ROI, edgesHorizontal", transformedImagePaintableHorizontal);
        //cv::waitKey(1);

    #endif
    #endif
    //---------------------- END DEBUG OUTPUT EDGES ------------------------------//

    vector<FuPoint<int>> laneMarkings = cLaneDetectionFu::extractLaneMarkings(edges);
    // vector<FuPoint<int>> laneMarkingsH = cLaneDetectionFu::extractLaneMarkingsHorizontal(edgesHorizontal);

    // dbscan sustituye lineas L C R
    unsigned int num_points = laneMarkings.size();
    
    // parametros
    double epsilon = dbscan_epsilon;
    unsigned int minpts = dbscan_min_points;
    unsigned int p_i = 0;

    dbscan::point_t *points;
    unsigned int num_clusters = 0;
    points = (dbscan::point_t *)calloc(num_points, sizeof(dbscan::point_t));

    try {
        while (p_i < num_points) {
              points[p_i].x = laneMarkings[p_i].getX();
              points[p_i].y = laneMarkings[p_i].getY();
              points[p_i].z = 0;
              points[p_i].cluster_id = -1;
              p_i++;
        }
        num_clusters = dbscan::dbscan(points, num_points, epsilon, minpts, dbscan::euclidean_dist);
    }
    catch(...) {
        printf("\n Excepcion dbscan");
    }

    //---------------------- DEBUG OUTPUT LANE MARKINGS ---------------------------------//
    #ifdef PUBLISH_DEBUG_OUTPUT
        // clona nuevamente para no ver edges detectados 
        // transformedImagePaintable = transformedImage.clone();
        // cv::cvtColor(transformedImagePaintable, transformedImagePaintable, CV_GRAY2BGR);

        for(int i = 0; i < num_points; i++)
        {
            if (points[i].cluster_id >= 0 && points[i].cluster_id < num_clusters) {
                cv::Point markingLoc = cv::Point(points[i].x, points[i].y);
                cv::circle(transformedImagePaintable, markingLoc, 1, cv::Scalar(points[i].cluster_id * 60, points[i].cluster_id * 60, 100), -1);
            }
            else {
                cv::Point markingLoc = cv::Point(points[i].x, points[i].y);
                cv::circle(transformedImagePaintable, markingLoc, 2, cv::Scalar(255, 255, 100), -1);
            }
        }

        pubRGBImageMsg(transformedImagePaintable, image_publisher_dbscan);

        /*
        transformedImagePaintableHorizontal = transformedImage.clone();
        cv::cvtColor(transformedImagePaintableHorizontal,transformedImagePaintableHorizontal,CV_GRAY2BGR);
        for(int i = 0; i < (int)laneMarkingsH.size(); i++)
        {         
            FuPoint<int> marking = laneMarkingsH[i];
            cv::Point markingLoc = cv::Point(marking.getX(), marking.getY());
            cv::circle(transformedImagePaintableHorizontal,markingLoc,1,cv::Scalar(0,255,0),-1);         
        }
        */

    #ifdef PAINT_OUTPUT
        //cv::imshow("Lane Markings", transformedImagePaintable);
        //cv::imshow("L. Markings Horizontal", transformedImagePaintableHorizontal);
        //cv::waitKey(1);
    #endif
    #endif
    //---------------------- END DEBUG OUTPUT LANE MARKINGS ------------------------------//

    // start actual execution: Repartir puntos en left center right, pero ahora se hará con clusters
    // buildLaneMarkingsLists(laneMarkings);
    // buildLaneMarkingsListsHorizontal(laneMarkingsH);
    int detected_polys;
    try {
        printf("\n Num points: %d", num_points);
        printf("\n Num clusters: %d", num_clusters);
        detected_polys = buildLaneMarkingsLists(points, num_points, num_clusters);
    } catch (...) {
        printf("buildLaneMarkings Exception");
        detected_polys = 0;
    }

    //---------------------- DEBUG OUTPUT GROUPED LANE MARKINGS ---------------------------------//
    geometry_msgs::Point pt;
    #ifdef PUBLISH_DEBUG_OUTPUT
        // transformedImagePaintable = transformedImage.clone();
        // cv::cvtColor(transformedImagePaintable,transformedImagePaintable,CV_GRAY2BGR);

    	// RULER
        for (int i = 0; i < 200; i = i + 10)
        {
            // //ROS_INFO_STREAM("Printing: " << i);
            cv::Point markingLoc = cv::Point(i, 0);
            cv::circle(transformedImagePaintable,markingLoc,1,cv::Scalar(0,221,237),-1);
            markingLoc = cv::Point(i, 10);
            cv::putText(transformedImagePaintable,std::to_string(i),markingLoc,6,.15,cv::Scalar(0,221,237));
            markingLoc = cv::Point(0, i);
            cv::circle(transformedImagePaintable,markingLoc,1,cv::Scalar(0,221,237),-1);
            markingLoc = cv::Point(10, i);
            cv::putText(transformedImagePaintable,std::to_string(i),markingLoc,6,.15,cv::Scalar(0,237,221));
        }

        array_left.cell_width = (int) laneMarkingsLeft.size() ;
        array_left.cell_height = 1;
        array_center.cell_width = (int) laneMarkingsCenter.size() ;
        array_center.cell_height = 1;
        array_right.cell_width = (int) laneMarkingsRight.size() ;
        array_right.cell_height = 1;

        array_horizontal.cell_width = (int) laneMarkingsHorizontal.size() ;
        array_horizontal.cell_height = 1;

        array_left.cells.clear();
        array_center.cells.clear();
        array_right.cells.clear();
        array_horizontal.cells.clear();
        
        for(int i = 0; i < (int)laneMarkingsLeft.size(); i++)
        {         
            FuPoint<int> marking = laneMarkingsLeft[i];
            cv::Point markingLoc = cv::Point(marking.getX(), marking.getY());
            cv::circle(transformedImagePaintable,markingLoc, 0, cv::Scalar(0,0,139), -1);
            
            pt.x = marking.getX();
            pt.y = marking.getY();
            pt.z = 0;
            array_left.cells.push_back(pt);
        }
        for(int i = 0; i < (int)laneMarkingsCenter.size(); i++)
        {         
            FuPoint<int> marking = laneMarkingsCenter[i];
            cv::Point markingLoc = cv::Point(marking.getX(), marking.getY());
            cv::circle(transformedImagePaintable,markingLoc, 0, cv::Scalar(0,100,0), -1);
            pt.x = marking.getX();
            pt.y = marking.getY();
            pt.z = 0;
            array_center.cells.push_back(pt);
        }
        for(int i = 0; i < (int)laneMarkingsRight.size(); i++)
        {         
            FuPoint<int> marking = laneMarkingsRight[i];
            cv::Point markingLoc = cv::Point(marking.getX(), marking.getY());
            cv::circle(transformedImagePaintable,markingLoc, 0, cv::Scalar(139,0,0), -1);
            pt.x = marking.getX();
            pt.y = marking.getY();
            pt.z = 0;
            array_right.cells.push_back(pt);      
        }

        /*
        for(int i = 0; i < (int)laneMarkingsHorizontal.size(); i++)
        {         
            FuPoint<int> marking = laneMarkingsHorizontal[i];
            cv::Point markingLoc = cv::Point(marking.getX(), marking.getY());
            cv::circle(transformedImagePaintable,markingLoc,1,cv::Scalar(255,0,255),-1);  
            pt.x = marking.getX();
            pt.y = marking.getY();
            pt.z = 0;
            array_horizontal.cells.push_back(pt);      
        }
        */
        
        //ROI
        cv::Point2d p1(proj_image_w_half-(roi_bottom_w/2), maxYRoi-1);
        cv::Point2d p2(proj_image_w_half+(roi_bottom_w/2), maxYRoi-1);
        cv::Point2d p3(proj_image_w_half+(roi_top_w/2), minYPolyRoi);
        cv::Point2d p4(proj_image_w_half-(roi_top_w/2), minYPolyRoi);
        cv::line(transformedImagePaintable, p1, p2, cv::Scalar(0,200,0));
        cv::line(transformedImagePaintable, p2, p3, cv::Scalar(0,200,0));
        cv::line(transformedImagePaintable, p3, p4, cv::Scalar(0,200,0));
        cv::line(transformedImagePaintable, p4, p1, cv::Scalar(0,200,0));

        pub_left.publish(array_left);
        pub_center.publish(array_center);
        pub_right.publish(array_right);
        pub_horizontal.publish(array_horizontal);

        pubRGBImageMsg(transformedImagePaintable, image_publisher_lane_markings);

    #ifdef PAINT_OUTPUT
        cv::imshow("Grouped Lane Markings", transformedImagePaintable);
        cv::waitKey(1);
    #endif
    #endif
    //---------------------- END DEBUG OUTPUT GROUPED LANE MARKINGS ------------------------------//

    
    try {
        ransac();
    } catch (...) {
        printf("RANSAC Exception");
    }

    free(points);

    //---------------------- DEBUG OUTPUT RANSAC POLYNOMIALS ---------------------------------//    
    // cv::Mat transformedImagePaintableLaneModel = transformedImage.clone();
    // cv::cvtColor(transformedImagePaintableLaneModel,transformedImagePaintableLaneModel,CV_GRAY2BGR);

    #ifdef PUBLISH_DEBUG_OUTPUT
        // cv::Mat transformedImagePaintableRansac = transformedImage.clone();
        // cv::cvtColor(transformedImagePaintableRansac,transformedImagePaintableRansac,CV_GRAY2BGR);
        
        //cv::Mat transformedImagePaintableLaneModel = transformedImage.clone();
    	//cv::cvtColor(transformedImagePaintableLaneModel,transformedImagePaintableLaneModel,CV_GRAY2BGR);

        // geometry_msgs::Point pt;
        array_ransac_left.cells.clear();
        array_ransac_center.cells.clear();
        array_ransac_right.cells.clear();
        array_ransac_horizontal.cells.clear();

        array_ransac_left.cell_width = polyDetectedLeft ? maxYRoi - minYPolyRoi : 0;
        array_ransac_left.cell_height = 1;
        array_ransac_center.cell_width = polyDetectedCenter ? maxYRoi - minYPolyRoi : 0;
        array_ransac_center.cell_height = 1;
        array_ransac_right.cell_width = polyDetectedRight ? maxYRoi - minYPolyRoi : 0;
        array_ransac_right.cell_height = 1;

        array_ransac_horizontal.cell_width = 160 - 0;
        array_ransac_horizontal.cell_height = 1;

        for(int i = maxYRoi; i >  minYPolyRoi; i--)
        {
            if (polyDetectedLeft) {
                cv::Point pointLocLeft = cv::Point(polyLeft.at(i), i);
                cv::circle(transformedImagePaintable, pointLocLeft, 0, cv::Scalar(0,0,200), -1);
                pt.x = pointLocLeft.x;
                pt.y = pointLocLeft.y;
                pt.z = 0;
                array_ransac_left.cells.push_back(pt);
            }
            if (polyDetectedCenter) {
                cv::Point pointLocCenter = cv::Point(polyCenter.at(i), i);
                cv::circle(transformedImagePaintable, pointLocCenter, 0, cv::Scalar(0,200,0), -1);
                pt.x = pointLocCenter.x;
                pt.y = pointLocCenter.y;
                pt.z = 0;
                array_ransac_center.cells.push_back(pt);
            }
            if (polyDetectedRight) {
                cv::Point pointLocRight = cv::Point(polyRight.at(i), i);
                cv::circle(transformedImagePaintable, pointLocRight, 0, cv::Scalar(200,0,0), -1);
                pt.x = pointLocRight.x;
                pt.y = pointLocRight.y;
                pt.z = 0;
                array_ransac_right.cells.push_back(pt);
            }
        }

        if (estado_actual >= 0 && (polyDetectedLeft || polyDetectedCenter || polyDetectedRight)) {
        	if (polyDetectedLeft) 
        		printf("\n OK left X: %.2f, LMsize: %d", polyLeft.at(car_text_position), (int) laneMarkingsLeft.size() );
        	if (polyDetectedCenter)
        		printf("\n OK center X: %.2f, LMsize: %d", polyCenter.at(car_text_position), (int) laneMarkingsCenter.size() );
        	if (polyDetectedRight)
        		printf("\n OK right X: %.2f, LMsize: %d", polyRight.at(car_text_position), (int) laneMarkingsRight.size() );
        	printf("\n Creating ackerman points ");
            ackerman_control(transformedImagePaintable, polyLeft, polyCenter, polyRight);
        }

        // get state width aprox
        if (polyDetectedLeft && polyDetectedCenter && polyDetectedRight)
        	printf("\n width state LC: %.2f CR: %.2f \n", 
        		polyCenter.at(car_text_position) - polyLeft.at(car_text_position),
        		polyRight.at(car_text_position) - polyCenter.at(car_text_position) );

		
        // muestra en el estado en que me encuentro
        cv::Point pointTextEstado = cv::Point(car_center - 10, car_text_position);
        if (estado_actual >= 0)
            cv::putText(transformedImagePaintable, nombre_estado[estado_actual], pointTextEstado, FONT_HERSHEY_SIMPLEX,.2,cv::Scalar(200,221,0));
        else
            cv::putText(transformedImagePaintable, "?", pointTextEstado, FONT_HERSHEY_SIMPLEX, .2, cv::Scalar(200,221,0));

        // muestra el estado al que me quiero desplazar
        pointTextEstado = cv::Point(car_center + 10, car_text_position);
        cv::putText(transformedImagePaintable, nombre_estado[estado_deseado], pointTextEstado, FONT_HERSHEY_SIMPLEX,.2,cv::Scalar(200,221,0));
        
        /* mostrar puntos HORIZONTALES
        if(polyDetectedHorizontal){
            for(int i = 0; i < 160; i++) {
                double y = polyHorizontal.at(i);
                cv::Point pointLocHorizontal = cv::Point(i, y);
                cv::circle(transformedImagePaintableLaneModel,pointLocHorizontal,0,cv::Scalar(200,0,200),-1);
                // ROS_INFO_STREAM("POLY HORIZONTAL x:" << i << ", y:" << y);
                pt.x = pointLocHorizontal.x;
                pt.y = pointLocHorizontal.y;
                pt.z = 0;
                array_ransac_horizontal.cells.push_back(pt);
            }
        }
		*/

        pub_ransac_left.publish(array_ransac_left);
        pub_ransac_center.publish(array_ransac_center);
        pub_ransac_right.publish(array_ransac_right);
        pub_ransac_horizontal.publish(array_ransac_right);

        

    #ifdef PAINT_OUTPUT
        cv::imshow("RANSAC results", transformedImagePaintable);
        cv::waitKey(1);
    #endif
    #endif
    //---------------------- END DEBUG OUTPUT RANSAC POLYNOMIALS ------------------------------//

    //---------------------- DEBUG OUTPUT LANE POLYNOMIAL ---------------------------------//
    #ifdef PAINT_OUTPUT
        cv::imshow("Lane polynomial", transformedImagePaintable);
        cv::waitKey(1);
    #endif

    // GET ORIENTATION OF CAR WITH RESPECT TO LINES
    #ifdef PUBLISH_DEBUG_OUTPUT
        // cv::Mat transformedImageOrientation = transformedImage.clone();
        // tamaño de grid en pixeles para obtener orientacion del carro con respecto a polilineas
        
        printf("\n Detected polys %d", detected_polys);
        if (detected_polys > 0) {
            // check gradient function for polys

            // Algoritmo pose carro
            // 0.- restar altura en y para invertir coordenadas
            // 1.- obtener pendiente de lineas detectadas (m, b, angulo)
            //      EXCEPCION si pendiente div/0 entonces lineas paralelas theta_carro = 90;
            // 2.- obtener intersección de lineas detectadas con centro de carro
            //  2.1.- Utilizar linea con valor intersección > 0
            //      2.1.0.- Restar 90 grados al angulo de la pendiente
            //      2.1.1.-     Si pendiente > 0
            //          theta_carro = 90 + angulo
            //      2.1.2.-     else
            //          theta_carro = 90 - angulo
            //      2.1.3.- terminar ciclo

            int y_car = maxYRoi;
            int height_grid = 20; 
            theta_carro = 1.57079;
            float *pendientes = new float[detected_polys];
            for (unsigned int i = 0; i < detected_polys; i++ ) {
                double x1, x2, y1, y2;
                // pendiente de polylinea
                try {
                    if (i == 0) {
                        // right
                        x1 = polyRight.at(y_car);
                        x2 = polyRight.at(y_car-height_grid);
                        y1 = 0;
                        y2 = height_grid;
                        pendientes[i] = (y2 - y1) / (x2 - x1);
                    } else if (i == 1) {
                        // center
                        x1 = polyCenter.at(y_car);
                        x2 = polyCenter.at(y_car-height_grid);
                        y1 = 0;
                        y2 = height_grid;
                        pendientes[i] = (y2 - y1) / (x2 - x1);
                    } else if (i == 2) {
                        // left
                        x1 = polyLeft.at(y_car);
                        x2 = polyLeft.at(y_car-height_grid);
                        y1 = 0;
                        y2 = height_grid;
                        pendientes[i] = (y2 - y1) / (x2 - x1);
                    }
                } catch (...) {
                    // printf("\n excepcion por division entre 0");
                    pendientes[i] = 1.57079;
                }
                
                // carro se asume modelo x = car_center
                // por lo cual solo se obtiene y utilizando la pendiente de la polylinea, si es positiva intersecta arriba
                // que es el objetivo ya que va a intersectar con esa polylinea

                double b = y1 - pendientes[i] * x1;
                double y_interseccion = pendientes[i] * car_center + b;
                if (y_interseccion > 0) {
                    double angulo = theta_carro - atan2(y2 - y1, x2 - x1);
                    theta_carro = theta_carro + angulo;
                    break;
                }
            }
            free(pendientes);

            // printf("\n %.2f", theta_carro);
            std_msgs::Float32 angleOrientation;
            angleOrientation.data = theta_carro;
            pub_orientation.publish(angleOrientation);
            

            int i = -5;
            double x_rep = i * cos(theta_carro) + 140;
            double y_rep = maxYRoi + -i; // i negada solo para graficar correctamente
            cv::Point pt_neg = cv::Point(x_rep, y_rep);
            cv::Point pt_ax_x1 = cv::Point(140, maxYRoi + i);
            cv::Point pt_ax_y1 = cv::Point(140 + i, maxYRoi);

            i = 5;
            x_rep = i * cos(theta_carro) + 140;
            y_rep = maxYRoi + -i; // i negada solo para graficar correctamente
            cv::Point pt_pos = cv::Point(x_rep, y_rep);
            cv::Point pt_ax_x2 = cv::Point(140, maxYRoi + i);
            cv::Point pt_ax_y2 = cv::Point(140 + i, maxYRoi);

            // Mostrar orientacion de carro con respecto a polylineas
            cv::line(transformedImagePaintable, pt_ax_x1, pt_ax_x2, cv::Scalar(255,255,255));
            cv::line(transformedImagePaintable, pt_ax_y1, pt_ax_y2, cv::Scalar(255,255,255));
            cv::line(transformedImagePaintable, pt_neg, pt_pos, cv::Scalar(0,200,0));
            
            
            pubRGBImageMsg(transformedImagePaintable, image_publisher_ransac);

        }
        #ifdef PAINT_OUTPUT
            cv::imshow("Lane polynomial", transformedImageOrientation);
            cv::waitKey(1);
        #endif
    #endif
    //ROS_INFO_STREAM("ProcessInput Ending");
}


/* compute based on distance y_next_dist the points in pixels that the car needs to head to */
bool cLaneDetectionFu::ackerman_control_next_points(double y_next_dist, cv::Point& pt_car, cv::Point& y_next_pt, cv::Point& y_next_pt2) {
    int next_move_y = maxYRoi-y_next_dist; // sin el 2* funcionaba bien, checar fuera de limite para polylinea
    int next_move2_y = maxYRoi-y_next_dist-10;

    double x_center = 0.0;
    double x_right = 0.0;
    bool center_closer = false;

    // TODO: next_pt and next_pt_2 perpendicular with respect to poly

    // calcular los puntos a moverse de acuerdo al estado actual y una distancia next_move_y
    // para entender un poco mas por que se utiliza esa linea, checar el mismo switch en det_hit en lane_states_node
    switch(estado_actual) {
        case 0: // OL
            if (polyDetectedLeft && pt_car.x < polyCenter.at(next_move_y)) {
                y_next_pt = cv::Point(polyLeft.at(next_move_y) - STATE_WIDTH_PIX, next_move_y);
                y_next_pt2 = cv::Point(polyLeft.at(next_move2_y) - STATE_WIDTH_PIX, next_move2_y);
            } else if (polyDetectedCenter && pt_car.x < polyRight.at(next_move_y)) {
                y_next_pt = cv::Point(polyCenter.at(next_move_y) - STATE_WIDTH_PIX, next_move_y);
                y_next_pt2 = cv::Point(polyCenter.at(next_move2_y) - STATE_WIDTH_PIX, next_move2_y);
            } else if (polyDetectedRight) {
                y_next_pt = cv::Point(polyRight.at(next_move_y) - STATE_WIDTH_PIX, next_move_y);
                y_next_pt2 = cv::Point(polyRight.at(next_move2_y) - STATE_WIDTH_PIX, next_move2_y);
            }
            else {
            	return false;
            }
            break;
        case 1: // LL
            if (polyDetectedLeft && pt_car.x < polyCenter.at(next_move_y)) {
                y_next_pt = cv::Point(polyLeft.at(next_move_y), next_move_y);
                y_next_pt2 = cv::Point(polyLeft.at(next_move2_y), next_move2_y);
            } else if (polyDetectedCenter && pt_car.x < polyRight.at(next_move_y)) {
                y_next_pt = cv::Point(polyCenter.at(next_move_y), next_move_y);
                y_next_pt2 = cv::Point(polyCenter.at(next_move2_y), next_move2_y);
            } else if (polyDetectedRight) {
                y_next_pt = cv::Point(polyRight.at(next_move_y), next_move_y);
                y_next_pt2 = cv::Point(polyRight.at(next_move2_y), next_move2_y);
            }
            else {
            	return false;
            }
            break;
        case 2: // LC
            if (polyDetectedCenter && polyDetectedLeft && pt_car.x < polyCenter.at(next_move_y)) {
                y_next_pt = cv::Point((polyCenter.at(next_move_y) + polyLeft.at(next_move_y))/2, next_move_y);
                y_next_pt2 = cv::Point((polyCenter.at(next_move2_y) + polyLeft.at(next_move2_y))/2, next_move2_y);
            } else if (polyDetectedCenter && polyDetectedRight) {
                y_next_pt = cv::Point((polyCenter.at(next_move_y) + polyRight.at(next_move_y))/2, next_move_y);
                y_next_pt2 = cv::Point((polyCenter.at(next_move2_y) + polyRight.at(next_move2_y))/2, next_move2_y);
            }
            else {
            	return false;
            }
            break;
        case 3: // CC
        	if (polyDetectedCenter && polyDetectedRight) {
    	        x_center = polyCenter.at(next_move_y);
            	x_right = polyRight.at(next_move_y);
        	    center_closer = abs(pt_car.x - x_center) <= abs(pt_car.x - x_right);
    	        y_next_pt = cv::Point(center_closer ? x_center : x_right, next_move_y);
	            y_next_pt2 = cv::Point(center_closer ? polyCenter.at(next_move2_y) : polyRight.at(next_move2_y), next_move2_y);
            } else {
	        	return false;
	        }
            break;
        case 4: // RC
        	if (polyDetectedCenter && polyDetectedRight) {
                    if (car_center > polyRight.at(next_move_y)) {
                        y_next_pt = cv::Point(polyRight.at(next_move_y) + STATE_WIDTH_PIX, next_move_y);
                        y_next_pt2 = cv::Point(polyRight.at(next_move2_y) + STATE_WIDTH_PIX, next_move2_y);
                    } else {
                        y_next_pt = cv::Point((polyCenter.at(next_move_y) + polyRight.at(next_move_y))/2, next_move_y);
                        y_next_pt2 = cv::Point((polyCenter.at(next_move2_y) + polyRight.at(next_move2_y))/2, next_move2_y);
                    }
            } else {
	        	return false;
	        }
            break;
        case 5: // RR
        	if (polyDetectedRight) {
            	y_next_pt = cv::Point(polyRight.at(next_move_y), next_move_y);
	            y_next_pt2 = cv::Point(polyRight.at(next_move2_y), next_move2_y);
	        } else {
	        	return false;
	        }
            break;
        case 6: // OR
        	if (polyDetectedRight) {
                y_next_pt = cv::Point(polyRight.at(next_move_y) + STATE_WIDTH_PIX, next_move_y);
                y_next_pt2 = cv::Point(polyRight.at(next_move2_y) + STATE_WIDTH_PIX, next_move2_y);
            } else {
            	return false;
            }
            break;
    }

    return true;
}

void cLaneDetectionFu::ackerman_control(cv::Mat& imagePaint, NewtonPolynomial& polyLeft, NewtonPolynomial& polyCenter,
    NewtonPolynomial& polyRight) {

    //
    // SI estado actual es DIFERENTE a estado requerido
    // 1.- Steering requerido para cambiar de estado
    //      (positivo o negativo dependiendo del estado actual)
    // 2.- Ciertos pixeles en eje y (marco carro) por ejemplo 10 para hacer un cambio suave
    //      con base a y, calcular steering de acuerdo a velocidad
    // 3.- El 10 es positivo o negativo de acuerdo a si nuevo estado es mayor o menor
    // 4.- Enviar este steering como accion de control
    //

    // car location (position bottom to up with respect to next points, to compute angle)
    cv::Point ptCar = cv::Point(car_center, maxYRoi);
    cv::circle(imagePaint, ptCar, 2, cv::Scalar(0,255,255), -1);
    actual_steering = 0;

    // calcular siguiente punto sobre eje Y (X con respecto al carro) de acuerdo a velocidad y steering actual
    // aqui obtener velocidad de IMU y orientacion el carro
    // TODO IMU
    dist_y_nextPoint = 2; // actual_speed * 5 * sin(PI/2 - actual_steering); //*;
    
    double dist_yPoly = 40 * abs(estado_deseado - estado_actual);

    cv::Point nextPoint, nextPoint2, pointSlope;
    
    // siguente punto en el estado en que se encuentra
    bool puntosValidos;
    // primera opcion para TRAYECTORIA
    // if (estado_actual != estado_deseado) 
    //     puntosValidos = ackerman_control_next_points(dist_yPoly, ptCar, nextPoint, nextPoint2);
    // else
    puntosValidos = ackerman_control_next_points(dist_y_nextPoint, ptCar, nextPoint, nextPoint2);

    if (puntosValidos) {
        // Cambio de estado, puede haber una forma mas suave
        if (estado_actual >= 0) {
            if (estado_actual < estado_deseado) {

                nextPoint.x = nextPoint.x + STATE_WIDTH_PIX * (estado_deseado - estado_actual);
                nextPoint2.x = nextPoint2.x + STATE_WIDTH_PIX * (estado_deseado - estado_actual);

            } else if (estado_actual > estado_deseado) {

                nextPoint.x = nextPoint.x - STATE_WIDTH_PIX * (estado_actual - estado_deseado);
                nextPoint2.x = nextPoint2.x - STATE_WIDTH_PIX * (estado_actual - estado_deseado);

            }
        }
        /* SMOOTH MOVE 
        // almacenar diferencia en x, para mantener posteriormente
        double diff_x = nextPoint2.x - nextPoint.x;
        
        // TRAYECTORIA, Hay (x,y) origen (car_center), faltan puntos intermedios para alcanzar objetivo (x, dist_y)
        if (estado_actual != estado_deseado) {
            // SMOOTH MOVE 
            NewtonPolynomial poly = NewtonPolynomial();
            double p1X = ptCar.x;
            double p1Y = ptCar.y;
            double p2X = nextPoint.x;
            double p2Y = nextPoint.y;
            poly.addData(p1X, p1Y);
            poly.addData(p2X, p2Y);

            for (int i = p1Y; i > p2Y; i--) {
                cv::Point pt = cv::Point(poly.at(i), i);
                cv::circle(imagePaint, pt, 0, cv::Scalar(255, 255, 255), -1);
            }

            nextPoint = cv::Point(poly.at(maxYRoi - dist_y), maxYRoi - dist_y);
            nextPoint2 = cv::Point(poly.at(maxYRoi - dist_y) + diff_x, maxYRoi - dist_y - 10);
        }
        */

        // -- visualize points ---------
        cv::circle(imagePaint, nextPoint, 2, cv::Scalar(0, 100, 255), -1);
        cv::circle(imagePaint, nextPoint2, 2, cv::Scalar(0 ,100, 255), -1);


        cv::circle(imagePaint, pointSlope, 2, cv::Scalar(245,245,0), -1);


        // ------------- ACKERMAN CONTROL -------------------
        // TODO falta considerar THETA_CARRO
        // ---angles
        // intercambio de coordenadas por frame rotado


        double G_x_cord = -nextPoint.y - -ptCar.y;
        double G_y_cord = ptCar.x - nextPoint.x;
        // alpha, angel between car and GOAL in radians
        double alpha = atan2(G_y_cord, G_x_cord);

        /*
        // intercambio de coordenadas de punto 2
        double G_sup_x = -nextPoint2.y - -ptCar.y;
        double G_sup_y = ptCar.x - nextPoint2.x;

        // law of cosines to compute beta, the angle of the goal with respect to alpha
        double a = sqrt(pow(G_x_cord, 2) + pow(G_y_cord, 2));
        double b = sqrt(pow(G_x_cord - G_sup_x, 2) + pow(G_y_cord - G_sup_y, 2));
        double c = sqrt(pow(-G_sup_x, 2) + pow(-G_sup_y,2));
        double beta = acos((pow(a, 2) + pow(b, 2) - pow(c, 2)) / (2 * a * b));
        beta = PI - beta;
        printf("\n alpha: %+010.2f, beta: %+010.2f", alpha, beta);	
        double steering_cont = kalpha * alpha + kbeta * beta;
        */

        /*

        double theta = yaw;
        double beta = - theta - alpha;
        double steering = kalpha * alpha + kbeta * beta;
        printf("\n alpha: %+010.2f, beta: %+010.2f", alpha, beta);

        */



        //-----PUBLISH ------
        // utilizando pixeles
        double y = ptCar.x - nextPoint.x;
        double error = atan2(y, 20);
        printf ("\n PID: car: %.2f, next: %.2f", ptCar.x, nextPoint.x);
        double steering = PID(error, 0.2, kp, ki, kd);

        // utilizando grados
        // double steering = PID(steering_cont, 0.2, 0.5, 0.001, 0.0);

        // -------------- FINISH ACKERMAN CONTROL -----------
        if (!std::isnan(steering)) {
            float steering_rounded = roundf(steering * 100) / 100;

            printf("\n steering: %+04.2f", steering_rounded);

            // intercambio de coordenadas de punto 2
            // double G_x_above = -pointSlope.y - -ptCar.y;
            // double G_y_above = ptCar.x - pointSlope.x;

            // compute angle of point1 vs point2 to tilt lines to detect polys
            // used above
            // polysAngle = atan2(G_y_above - G_y_cord, G_x_above - G_x_cord);
            // cv::putText(imagePaint,std::to_string(polysAngle),markingLoc,6,.15,cv::Scalar(0,237,221));
            
            geometry_msgs::Twist vel;
            vel.angular.z = steering_rounded;
            vel.linear.x = actual_speed;
            // speed is constant

            // falta PID
            pub_speed.publish(vel);
        }
    }
}

double cLaneDetectionFu::PID(double error, double dt, double Kp, double Ki, double Kd){
	// ROS_INFO_STREAM("PID time");


	double pOut = Kp * error;
	integralPID += error * dt;
	double iOut = Ki * integralPID;
	double derivative = (error - prevErrorPID) / dt;
	double dOut = Kd * derivative;
	double output = pOut + iOut + dOut;
	prevErrorPID = error;

	// Restriction
	double out_restringido = output;
	if( output > .7 )
		out_restringido = .7;
	else if( output <= -0.7 )
		out_restringido = -0.7;

	printf("\n Error theta: %+010.4f, Res PID: %+010.4f, Senal Servo: %+010.4f, p: %.2f i: %.2f d: %.2f", error, output, out_restringido, pOut, iOut, dOut );

	return out_restringido;
}

/* EdgeDetector methods */

/**
 * Compute scanlines. Each may consist of multiple segments, split at regions
 * that should not be inspected by the kernel.
 * @param side
 * @return vector of segments of scanlines, walk these segments with the kernel
 */
vector<vector<LineSegment<int>> > cLaneDetectionFu::getScanlines() {
    //ROS_INFO_STREAM("getScanlines beginning");
    vector<vector<LineSegment<int>> > scanlines;

    vector<cv::Point> checkContour;
    checkContour.push_back(cv::Point(proj_image_w_half-(roi_bottom_w/2),maxYRoi-1));
    checkContour.push_back(cv::Point(proj_image_w_half+(roi_bottom_w/2),maxYRoi-1));
    checkContour.push_back(cv::Point(proj_image_w_half+(roi_top_w/2),minYPolyRoi));
    checkContour.push_back(cv::Point(proj_image_w_half-(roi_top_w/2),minYPolyRoi));
    
    int scanlineStart = 0;
    int scanlineEnd = proj_image_w;

    int segmentStart = -1;
    vector<LineSegment<int>> scanline;
    for (int i = 1;
        (i/scanlinesVerticalDistance) < scanlinesMaxCount && i <= proj_image_h;
        i += scanlinesVerticalDistance) {
        scanline = vector<LineSegment<int>>();
        
        // walk along line
        for (int j = scanlineStart; j <= scanlineEnd; j ++) {
            bool isInside = pointPolygonTest(checkContour, cv::Point(j, i),false) >= 0; // esta en ROI
            
            // start new scanline segment
            if (isInside && j < scanlineEnd) {
                if (segmentStart == -1) segmentStart = j;
            // found end of scanline segment, reset start
            } else if (segmentStart != -1) {                
                scanline.push_back(
                        LineSegment<int>(
                                FuPoint<int>(segmentStart, i),
                                FuPoint<int>(j-1, i)
                            )
                        );
                segmentStart = -1;
            }
        }
        // push segments found
        if (scanline.size()) {
            scanlines.push_back(scanline);
        }
    }

    return scanlines;    
}

/**
 * Walk with prewitt/sobel kernel along all scanlines.
 * @param image
 * @return All edgePoints on side, sorted by scanlines.
 */
vector<vector<EdgePoint>> cLaneDetectionFu::scanImage(cv::Mat image, ePosition position) {
    //ROS_INFO_STREAM("scanImage beginning");
    ////ROS_INFO_STREAM("scanImage() - " << scanlines.size() << " scanlines.");
    vector<vector<EdgePoint>> edgePoints;
    
    //const Image &image = getImage();
    //const ImageDimensions &imgDim = getImageDimensions();
    //const OmnidirectionalCameraMatrix &cameraMatrix = getOmnidirectionalCameraMatrix();

    // scanline length can maximal be image height/width
    int scanlineMaxLength = image.cols;
    
    // store kernel results on current scanline in here
    vector<int> scanlineVals(scanlineMaxLength, 0);

    // walk over all scanlines
    for (auto scanline : scanlines) {
        // set all brightness values on scanline to 0;
        std::fill(scanlineVals.begin(), scanlineVals.end(), 0);
        int offset = 0;
        if (scanline.size()) {
            offset = scanline.front().getStart().getY();
        }

        // scanline consisting of multiple segments
        // walk over each but store kernel results for whole scanline
        
        for (auto segment : scanline) {         
            int start = segment.getStart().getX();
            int end = segment.getEnd().getX();
            
            // walk along segment
            for (int i = start; i < end - g_kernel1DWidth; i++) {
                int sum_x = 0;                

                //cv::Mat uses ROW-major system -> .at(y,x)
                // use kernel width 5 and try sobel kernel
                sum_x -= image.at<uint8_t>(offset-1, i+1);
                sum_x -= image.at<uint8_t>(offset-1, i);
                // kernel is 0
                sum_x += image.at<uint8_t>(offset-1, i+2);
                sum_x += image.at<uint8_t>(offset-1, i+4);

                sum_x -= 2*image.at<uint8_t>(offset, i);
                sum_x -= 2*image.at<uint8_t>(offset, i+1);
                // kernel is 0
                sum_x += 2*image.at<uint8_t>(offset, i+2);
                sum_x += 2*image.at<uint8_t>(offset, i+4);

                sum_x -= image.at<uint8_t>(offset+1, i);
                sum_x -= image.at<uint8_t>(offset+1, i+1);
                // kernel is 0
                sum_x += image.at<uint8_t>(offset+1, i+2);
                sum_x += image.at<uint8_t>(offset+1, i+4);

                // intento por mostrar lineas horizontales
                int sum_y = 0;

                if(position == HORIZONTAL){
                    sum_y -= image.at<uint8_t>(offset-1, i);
                    sum_y -= 2*image.at<uint8_t>(offset-1, i+1);
                    sum_y -= 2*image.at<uint8_t>(offset-1, i+2);
                    sum_y -= image.at<uint8_t>(offset-1, i+3);

                    //sum_y -= image.at<uint8_t>(offset, i);
                    //sum_y -= 2*image.at<uint8_t>(offset, i+1);
                    //sum_y -= 2*image.at<uint8_t>(offset, i+2);
                    //sum_y -= image.at<uint8_t>(offset, i+3);

                    sum_y += image.at<uint8_t>(offset+1, i);
                    sum_y += 2*image.at<uint8_t>(offset+1, i+1);
                    sum_y += 2*image.at<uint8_t>(offset+1, i+2);
                    sum_y += image.at<uint8_t>(offset+1, i+3);

                    //sum_y += image.at<uint8_t>(offset+3, i);
                    //sum_y += 2*image.at<uint8_t>(offset+3, i+1);
                    //sum_y += 2*image.at<uint8_t>(offset+3, i+2);
                    //sum_y += image.at<uint8_t>(offset+3, i+3);
                }
                
                int gradient = sqrt(sum_x*sum_x+sum_y*sum_y);
                int angle = atan2(sum_y,sum_x) * 180.0 / CV_PI;

                // +4 because of sobel weighting
                sum_x = sum_x / (3 * g_kernel1DWidth + 4);
                sum_y = sum_y / (3 * g_kernel1DWidth + 4);



                ////ROS_INFO_STREAM(sum << " is kernel sum.");
                // sustitui sum por gradient
                if(position == HORIZONTAL){
                    // los bordes verticales tienen un angulo de 0, por tanto los horizontales 90
                    if (std::abs(sum_y) > m_gradientThreshold && (angle >= 80 && angle <= 130) ) {
                        // set scanlineVals at center of kernel
                        scanlineVals[i + g_kernel1DWidth/2] = sum_y;
                    }
                }
                else {
                    if (std::abs(sum_x) > m_gradientThreshold) {
                        // set scanlineVals at center of kernel
                        scanlineVals[i + g_kernel1DWidth/2] = sum_x;
                    }
                }
            }
            // ROS_INFO_STREAM("max: " << maxG << ", min: "<< minG << ", mean: " << mean/suma);
        }

        // after walking over all segments of one scanline
        // do non-max-suppression
        // for both minima and maxima at same time
        // TODO: Jannis: find dryer way
        int indexOfLastMaximum = 0;
        int valueOfLastMaximum = 0;
        int indexOfLastMinimum = 0;
        int valueOfLastMinimum = 0;
        for (int i = 1; i < scanlineMaxLength -1; i++) {
            // check if maximum
            if (scanlineVals[i] > 0) {
                if (scanlineVals[i] < scanlineVals[i-1] or scanlineVals[i] < scanlineVals[i+1]) {
                    scanlineVals[i] = 0;
                }
                else {
                    // this pixel can just survive if the next maximum is not too close
                    if (i - indexOfLastMaximum > m_nonMaxWidth) {
                        // this is a new maximum
                        indexOfLastMaximum = i;
                        valueOfLastMaximum = scanlineVals[i];
                    }
                    else {
                        if (valueOfLastMaximum < scanlineVals[i]) {
                            // this is a new maximum
                            // drop the old maximum
                            scanlineVals[indexOfLastMaximum] = 0;
                            indexOfLastMaximum = i;
                            valueOfLastMaximum = scanlineVals[i];
                        }
                        else {
                            scanlineVals[i] = 0;
                        }
                    }
                }
            }
            // check if minimum
            if (scanlineVals[i] < 0) {
                if (scanlineVals[i] > scanlineVals[i-1] or scanlineVals[i] > scanlineVals[i+1]) {
                    scanlineVals[i] = 0;
                }
                else {
                    // this pixel can just survive if the next minimum is not too close
                    if (i - indexOfLastMinimum > m_nonMaxWidth) {
                        // this is a new minimum
                        indexOfLastMinimum = i;
                        valueOfLastMinimum = scanlineVals[i];
                    }
                    else {
                        if (valueOfLastMinimum > scanlineVals[i]) {
                            // this is a new maximum
                            // drop the old maximum
                            scanlineVals[indexOfLastMinimum] = 0;
                            indexOfLastMinimum = i;
                            valueOfLastMinimum = scanlineVals[i];
                        }
                        else {
                            scanlineVals[i] = 0;
                        }
                    }
                }
            }
        }
        // collect all the edgePoints for scanline
        vector<EdgePoint> scanlineEdgePoints;
        for (int i = 0; i < static_cast<int>(scanlineVals.size()); i++) {
            if (scanlineVals[i] != 0) {
                FuPoint<int> imgPos = FuPoint<int>(i, offset);
                
                FuPoint<Meter> relPos = FuPoint<Meter>();//offset, i);//cameraMatrix.transformToLocalCoordinates(imgPos);
                scanlineEdgePoints.push_back(EdgePoint(imgPos, relPos, scanlineVals[i]));
            }
        }
        edgePoints.push_back(std::move(scanlineEdgePoints));
    }
    // after walking along all scanlines
    // return edgePoints
    //ROS_INFO_STREAM("scanImage ending");
    return edgePoints;
}

void cLaneDetectionFu::get_localization(const std_msgs::Float32MultiArray& locArray) {
    int estadoPrevio = estado_actual;

    float max=0;
    for(int i = 0; i < NUM_STATES * STATE_WIDTH; i++) {
        if(locArray.data[i]>max) {
            max=locArray.data[i];
        }
    }

    int countStates=0;
    int state = -1;
    for(int i = NUM_STATES * STATE_WIDTH - 1; i >= 0; i--) {
        if(locArray.data[i]==max) {
            int temp_state = (int)floor(i / STATE_WIDTH);
            if (temp_state != state){
                state = temp_state;
                countStates++;
            }
        }
    }

    if (countStates==1)
        estado_actual = state;
    else
        estado_actual = -1; // no se pudo determinar el estado, ya que hay mas de uno posible
}

/*
void cLaneDetectionFu::get_ctrl_action(const geometry_msgs::Twist& val) {
    // negative is forward
    actual_speed = sqrt(val.linear.x * val.linear.x);
    actual_steering = val.angular.z; //steering
    // printf("\n vel: %.2f ", speed);
}
*/

void cLaneDetectionFu::get_ctrl_desired_state(const std_msgs::Int16& val) {
    // ctrl_estado = val.data;
    // prevenir un estado deseado fuera de limites
    if (estado_deseado + val.data < 0)
        estado_deseado = 0;
    else if (estado_deseado + val.data > NUM_STATES)
        estado_deseado = NUM_STATES;
    else
        estado_deseado += val.data;
}

/* LaneMarkingDetector methods */


//uses Edges to extract lane markings
std::vector<FuPoint<int>> cLaneDetectionFu::extractLaneMarkings(const std::vector<std::vector<EdgePoint>>& edges) {
    //ROS_INFO_STREAM("extractLaneMarkings beginning");
    vector<FuPoint<int>> result;

    for (const auto& line : edges) {
        if (line.empty()) continue;
    
        for (
            auto edgePosition = line.begin(), nextEdgePosition = edgePosition + 1;
            nextEdgePosition != line.end();
            edgePosition = nextEdgePosition, ++nextEdgePosition
        ) {
            if (edgePosition->isPositive() and not nextEdgePosition->isPositive()) {
                FuPoint<int> candidateStartEdge = edgePosition->getImgPos();
                FuPoint<int> candidateEndEdge = nextEdgePosition->getImgPos();
                if ((candidateStartEdge - candidateEndEdge).squaredMagnitude() < laneMarkingSquaredThreshold) {
                    result.push_back(center(candidateStartEdge, candidateEndEdge));
                }
            }
        }
    }
    //ROS_INFO_STREAM("extractLaneMarkings ending");
    return result;
}

//uses Edges to extract lane markings
std::vector<FuPoint<int>> cLaneDetectionFu::extractLaneMarkingsHorizontal(const std::vector<std::vector<EdgePoint>>& edges) {
    //ROS_INFO_STREAM("extractLaneMarkings beginning");
    vector<FuPoint<int>> result;

    for (const auto& line : edges) {
        if (line.empty()) continue;
    
        for (
            auto edgePosition = line.begin(), nextEdgePosition = edgePosition + 1;
            nextEdgePosition != line.end();
            edgePosition = nextEdgePosition, ++nextEdgePosition
        ) {
            if (edgePosition->isPositive() and nextEdgePosition->isPositive()) {
                FuPoint<int> candidateStartEdge = edgePosition->getImgPos();
                FuPoint<int> candidateEndEdge = nextEdgePosition->getImgPos();
                if ((candidateStartEdge - candidateEndEdge).squaredMagnitude() < laneMarkingSquaredThreshold) {
                    result.push_back(center(candidateStartEdge, candidateEndEdge));
                }
            }
        }
    }
    //ROS_INFO_STREAM("extractLaneMarkings ending");
    return result;
}


/**
 * Creates three vectors of lane marking points out of the given lane marking
 * point vector.
 *
 * A point has to lie within the ROI of the previously detected lane polynomial
 * or within the default ROI, if no polynomial was detected.
 * The lists are the input data for the RANSAC algorithm.
 *
 * @param laneMarkings  a vector containing all detected lane markings
 * return num detected polys
 */
int cLaneDetectionFu::buildLaneMarkingsLists(
        const dbscan::point_t *points, const int num_points, const unsigned int num_clusters ) {
    //ROS_INFO_STREAM("buildLaneMarkingsLists beginning");
    laneMarkingsLeft.clear();
    laneMarkingsCenter.clear();
    laneMarkingsRight.clear();

    std::vector<FuPoint<int>> groupedClusters [num_clusters];
    NewtonPolynomial poly [num_clusters];
    bool detectedPoly [num_clusters];

    for (unsigned int i = 0; i < num_clusters; i++) {
        groupedClusters[i] = std::vector<FuPoint<int>> ();
        poly[i] = NewtonPolynomial();
        detectedPoly[i] = false;
    }
    
    for ( unsigned int i = 0; i < num_points; i++ ) {
        if (points[i].cluster_id >= 0 && points[i].cluster_id < num_clusters) {
            FuPoint<int> point = FuPoint<int>(points[i].x, points[i].y);
            groupedClusters[points[i].cluster_id].push_back(point);
        }
    }

    // crear un polinomio utilizando los puntos de cada cluster con el objetivo de ordenar los polinomios
    // y detectar L C R
    
    unsigned int numDetectedPolys = 0;
    
    for (unsigned int i = 0; i < num_clusters; i++) {
        // la posicion no importa mucho
        std::vector<FuPoint<int>> supporters = std::vector<FuPoint<int>>();
        std::vector<FuPoint<int>> best_points = std::vector<FuPoint<int>>();
        NewtonPolynomial prevPoly = NewtonPolynomial();
        std::pair<NewtonPolynomial, double>  bestPoly = std::make_pair(NewtonPolynomial(), 0);

        detectedPoly[i] = ransacInternal(LEFT, groupedClusters[i], bestPoly,
            poly[i], supporters, prevPoly, best_points);

        if (detectedPoly[i])
            numDetectedPolys++;
    }

    // printf("\n Detectados: %d ", numDetectedPolys);

    std::vector<FuPoint<int>> detectedClusters [numDetectedPolys];
    NewtonPolynomial detectedPolys [numDetectedPolys];

    unsigned int num_detected = 0;
    for (unsigned int i = 0; i < num_clusters; i++) {
        if (detectedPoly[i]) {
            detectedClusters[num_detected] = groupedClusters[i];
            detectedPolys[num_detected] = poly[i];
            num_detected++;
        }
    }

    // el peor metodo de ordenamiento (bubble sort), pero espero sean máximo 3 clusters
    for (unsigned int j = 0; j < (numDetectedPolys - 1); j++) { 
        // Last i elements are already in place
        for (unsigned int i = 0; i < (numDetectedPolys - j - 1); i++ ) {
            // obtener Y de un polinomio
            // ordenar de manera decreciente
            if (detectedPolys[i].at(minYPolyRoi) < detectedPolys[i + 1].at(minYPolyRoi)) {

                // swap points
                std::vector<FuPoint<int>> tempPoints = detectedClusters[i];
                detectedClusters[i] = detectedClusters[i + 1];
                detectedClusters[i + 1] = tempPoints;

                // swap detected poly
                NewtonPolynomial tempPoly = detectedPolys[i];
                detectedPolys[i] = detectedPolys[i + 1];
                detectedPolys[i + 1] = tempPoly;                
            }
        }
    }

    for (unsigned int i = 0; i < numDetectedPolys; i++ ) {
        if (i == 0)
            laneMarkingsRight = detectedClusters[i];
        if (i == 1)
            laneMarkingsCenter = detectedClusters[i];
        if (i == 2)
            laneMarkingsLeft = detectedClusters[i];
    }

    return numDetectedPolys;
}

/**
 * Creates three vectors of lane marking points out of the given lane marking
 * point vector.
 *
 * A point has to lie within the ROI of the previously detected lane polynomial
 * or within the default ROI, if no polynomial was detected.
 * The lists are the input data for the RANSAC algorithm.
 *
 * @param laneMarkings  a vector containing all detected lane markings
 */
void cLaneDetectionFu::buildLaneMarkingsListsHorizontal(
        const std::vector<FuPoint<int>> &laneMarkings) {
    //ROS_INFO_STREAM("buildLaneMarkingsLists beginning");
    laneMarkingsHorizontal.clear();

    for (FuPoint<int> laneMarking : laneMarkings) {
        
        if (polyDetectedHorizontal) {
            if (isInPolyRoi(polyHorizontal, laneMarking)) {
                laneMarkingsHorizontal.push_back(laneMarking);
                continue;
            }
        }


        if (isInDefaultRoi(HORIZONTAL, laneMarking)) {
            laneMarkingsHorizontal.push_back(laneMarking);
            continue;
        }
    }

    //ROS_INFO_STREAM("buildLaneMarkingsLists ending");
}



/**
 * Calculates the horizontal distance between a point and the default line given
 * by its position.
 *
 * @param line  The position of the default line (LEFT, CENTER or RIGHT)
 * @param p     The given point
 * @return      The horizontal distance between default line and point, horizontal distance = difference in X!!!
 */
int cLaneDetectionFu::horizDistanceToDefaultLine(ePosition &line, FuPoint<int> &p) {
    //ROS_INFO_STREAM("horizDistanceToDefaultLine beginning");
    double pX = p.getX();
    double distance = 0;

    switch (line) {
    case LEFT:
        distance = std::abs(pX - defaultXLeft);
        break;
    case CENTER:
        distance = std::abs(pX - defaultXCenter);
        break;
    case RIGHT:
        distance = std::abs(pX - defaultXRight);
        break;
    }
    //ROS_INFO_STREAM("horizDistanceToDefaultLine ending");
    return distance;
}

/**
 * Calculates the horizontal distance between a point and the default line given
 * by its position.
 *
 * @param line  The position of the default line (LEFT, CENTER or RIGHT)
 * @param p     The given point
 * @return      The horizontal distance between default line and point, horizontal distance = difference in X!!!
 */
int cLaneDetectionFu::vertDistanceToDefaultLine(ePosition &line, FuPoint<int> &p) {
    //ROS_INFO_STREAM("horizDistanceToDefaultLine beginning");
    double pY = p.getY();
    double distance = 0;

    distance = std::abs(pY - defaultYHorizontal);
    //ROS_INFO_STREAM("horizDistanceToDefaultLine ending");
    return distance;
}

/**
 * Calculates the horizontal distance between a point and a polynomial.
 *
 * @param poly  The given polynomial
 * @param p     The given point
 * @return      The horizontal distance between the polynomial and the point, horizontal distance = difference in X!!!
 */
int cLaneDetectionFu::horizDistanceToPolynomial(NewtonPolynomial& poly, FuPoint<int> &p) {
    //ROS_INFO_STREAM("horizDistanceToPolynomial beginning");
    double pY = p.getY();
    double pX = p.getX();

    double polyX = poly.at(pY);
    double distance = std::abs(pX - polyX);
    //ROS_INFO_STREAM("horizDistanceToPolynomial ending");
    return distance;
}

/**
 * Calculates the horizontal distance between a point and a polynomial.
 *
 * @param poly  The given polynomial
 * @param p     The given point
 * @return      The horizontal distance between the polynomial and the point, horizontal distance = difference in X!!!
 */
int cLaneDetectionFu::vertDistanceToPolynomial(NewtonPolynomial& poly, FuPoint<int> &p) {
    //ROS_INFO_STREAM("horizDistanceToPolynomial beginning");
    double pY = p.getY();
    double pX = p.getX();

    double polyY = poly.at(pX);
    double distance = std::abs(pY - polyY);
    //ROS_INFO_STREAM("horizDistanceToPolynomial ending");
    return distance;
}

/**
 * Method, that checks if a point lies within the default ROI of a position.
 *
 * @param position  The position of the default ROI
 * @param p         The given point, which is checked
 * @return          True, if the point lies within the default ROI
 */
bool cLaneDetectionFu::isInDefaultRoi(ePosition position, FuPoint<int> &p) {
    //ROS_INFO_STREAM("isInDefaultRoi beginning");
    double dist = horizDistanceToDefaultLine(position, p);
    // printf("distancia a Linea %d: (%d,%d) %.2f \n", position, p.getX(),p.getY(), dist);

    if (p.getY() < minYDefaultRoi || p.getY() > maxYRoi) {
        // printf("FALSE 1\n");
        return false;
    }
    else if (position == HORIZONTAL && vertDistanceToDefaultLine(position, p)
            <= interestDistanceDefault){
        // printf("HORIZONTAL\n");
        return true;
    }
    else if (dist
            <= interestDistanceDefault) {
        // printf("TRUE\n");
        return true;
    }
    else {
        // printf("FALSE 2\n");
        return false;
    }
    //ROS_INFO_STREAM("isInDefaultRoi ending");
}

/**
 * Method, that checks if a point lies within the the ROI of a polynomial.
 *
 * @param poly      The polynomial, whose ROI is used
 * @param p         The point, which is checked
 * @return          True, if the point lies within the polynomial's ROI
 */
bool cLaneDetectionFu::isInPolyRoi(NewtonPolynomial &poly, FuPoint<int> &p) {
    //ROS_INFO_STREAM("isInPolyRoi beginning");
    if (p.getY() < minYPolyRoi || p.getY() > maxYRoi) {
        return false;
    }
    else if (horizDistanceToPolynomial(poly, p) <= interestDistancePoly) {
        return true;
    }
    else {
        return false;
    }
    //ROS_INFO_STREAM("isInPolyRoi ending");
}

/**
 * Calculates the horizontal distance between two points.
 *
 * @param p1    The first point
 * @param p2    The second point
 * @return      The horizontal distance between the two points, horizontal distance = difference in X!!!
 */
int cLaneDetectionFu::horizDistance(FuPoint<int> &p1, FuPoint<int> &p2) {
    //ROS_INFO_STREAM("horizDistance beginning");
    double x1 = p1.getX();
    double x2 = p2.getX();
    //ROS_INFO_STREAM("horizDistance ending");
    return std::abs(x1 - x2);
}

/**
 * Calculates the distance between two points.
 *
 * @param p1    The first point
 * @param p2    The second point
 * @return      The distance between the two points
 */
double cLaneDetectionFu::distanceBetweenPoints(FuPoint<int> &p1, FuPoint<int> &p2) {
    //ROS_INFO_STREAM("horizDistance beginning");
    double x1 = p1.getX();
    double x2 = p2.getX();
    double y1 = p1.getY();
    double y2 = p2.getY();
    //ROS_INFO_STREAM("horizDistance ending");
    return std::sqrt((x2 - x1)*(x2 - x1)+(y2 - y1)*(y2 - y1));
}

/**
 * Calculates the gradient of a polynomial at a given x value. The used formula
 * was obtained by the following steps:
 * - start with the polynomial of 2nd degree in newton basis form:
 *   p(x) = c0 + c1(x - x0) + c2(x - x0)(x - x1)
 * - expand the equation and sort it by descending powers of x
 * - form the first derivative
 *
 * Applying the given x value then results in the wanted gradient.
 *
 * @param x         The given x value
 * @param points    The data points used for interpolating the polynomial
 * @param coeffs    The coefficients under usage of the newton basis
 * @return          The gradient of the polynomial at x
 */
double cLaneDetectionFu::gradient(double x, std::vector<FuPoint<int>> &points,
        std::vector<double> coeffs) {
    return 2 * coeffs[2] * x + coeffs[1] - coeffs[2] * points[1].getY()
            - coeffs[2] * points[0].getY();
}

/**
 * Calculates the x value of the point where the normal of the tangent of a
 * polynomial at a given point p intersects with a second polynomial.
 *
 * The formula for the intersection point is obtained by setting equal the
 * following two formula:
 *
 * 1. the formula of the normal in point-slope-form:
 *     y - p_y = -(1 / m) * (x - p_x) which is the same as
 *           y = -(x / m) + (p_x / m) + p_y
 *
 * 2. the formula of the second polynomial of 2nd degree in newton basis form:
 *           y = c0 + c1(x - x0) + c2(x - x0)(x - x1)
 *
 * Expanding everything and moving it to the right side gives a quadratic
 * equation in the general form of 0 = ax^2 + bx + c, which can be solved using
 * the general quadratic formula x = (-b +- sqrt(b^2 - 4ac)) / 2a
 *
 * The three cases for the discriminant are taken into account.
 *
 * @param p         The point of the first poly at which its tangent is used
 * @param m         The gradient of the tangent
 * @param points    The data points used for interpolating the second polynomial
 * @param coeffs    The coeffs of the second polynomial with newton basis
 * @return          The x value of the intersection point of normal and 2nd poly
 */
double cLaneDetectionFu::intersection(FuPoint<double> &p, double &m,
        std::vector<FuPoint<int>> &points, std::vector<double> &coeffs) {
    double a = coeffs[2];
    double b = coeffs[1] - (coeffs[2] * points[1].getY())
            - (coeffs[2] * points[0].getY()) + (1.0 / m);
    double c = coeffs[0] - (coeffs[1] * points[0].getY())
            + (coeffs[2] * points[0].getY() * points[1].getY())
            - p.getY() - (p.getX() / m);

    double dis = std::pow(b, 2) - (4 * a * c);
    double x1 = 0;
    double x2 = 0;

    if (dis < 0) {
        return -1;
    }
    else if (dis == 0) {
        return -b / (2 * a);
    }
    else {
        x1 = (-b + std::sqrt(std::pow(b, 2) - (4 * a * c))) / (2 * a);
        x2 = (-b - std::sqrt(std::pow(b, 2) - (4 * a * c))) / (2 * a);
    }

    return fmax(x1, x2);
}

/**
 * Calculates the gradient of a second polynomial at the point, at which the
 * normal of the tangent of the first polynomial at the given point
 * intersects with the second polynomial.
 *
 * @param x         The given x value of the point on the first polynomial
 * @param poly1     The first polynomial
 * @param points1   The data points used for interpolating the first poly
 * @param points2   The data points used for interpolating the second poly
 * @param coeffs1   The coeffs of the first poly using newton basis
 * @param coeffs2   The coeffs of the second poly using newton basis
 * @param m1        The gradient of the first poly at x
 * @return          The gradient of the second poly at the intersection point
 */
double cLaneDetectionFu::nextGradient(double x, NewtonPolynomial &poly1,
        std::vector<FuPoint<int>> &points1, std::vector<FuPoint<int>> &points2,
        std::vector<double> coeffs1, std::vector<double> coeffs2, double m1) {

    FuPoint<double> p = FuPoint<double>(x, poly1.at(x));
    double x2 = intersection(p, m1, points2, coeffs2);

    return gradient(x2, points2, coeffs2);
}

/**
 * Check two gradients for similarity. Return true if the difference in degree
 * is less than 10.
 *
 * @param m1    The first gradient
 * @param m2    The second gradient
 * @return      True, if the diffenence between the gradients is less than 10 degrees
 */
bool cLaneDetectionFu::gradientsSimilar(double &m1, double &m2) {
    double a1 = atan(m1) * 180 / PI;
    double a2 = atan(m2) * 180 / PI;

    if (abs(a1 - a2) < 10) {
        return true;
    }
    else {
        return false;
    }
}

/**
 * Finds the position of the polynomial with the highest proportion.
 * @return The position of the best polynomial
 */
ePosition cLaneDetectionFu::maxProportion() {
    ePosition maxPos = LEFT;
    double maxVal = bestPolyLeft.second;

    if (bestPolyCenter.second > maxVal) {
        maxPos = CENTER;
        maxVal = bestPolyCenter.second;
    }

    if (bestPolyRight.second > maxVal) {
        maxPos = RIGHT;
    }

    return maxPos;
}

/**
 * Create the lane polynomial starting from the detected polynomial of the
 * given position. A lane polynomial is formed by shifting points with
 * different x-values of the used polynomial along the normals of the polynomial
 * at this points to the distance, where the respective lane polynomial is
 * expected to lie.
 *
 * @param position  The position of the detected polynomial used as reference
 */
void cLaneDetectionFu::createLanePoly(ePosition position) {

    //ROS_INFO_STREAM("createLanePoly beginning");
    lanePoly.clear();

    double coef = 1.2;

    double x1 = minYPolyRoi+5;
    double x2 = minYPolyRoi + ((proj_image_h-minYPolyRoi)/2);
    double x3 = proj_image_h-5;

    FuPoint<double> pointRight1;
    FuPoint<double> pointRight2;
    FuPoint<double> pointRight3;

    double dRight = 0;

    NewtonPolynomial usedPoly;

    double y1;
    double y2;
    double y3;
 
    if (position == LEFT) {
        usedPoly = polyLeft;
        dRight = defaultXLeft-5;
    }
    else if (position == CENTER) {
        usedPoly = polyCenter;
        //dRight = defaultXCenter-5;
        dRight = defaultXCenter+5;
    }
    else if (position == RIGHT) {
        usedPoly = polyRight;
        dRight = defaultXRight+5;
    }


    pointRight1 = FuPoint<double>(x1, usedPoly.at(x1) - dRight);
    pointRight2 = FuPoint<double>(x2, usedPoly.at(x2) - dRight);
    pointRight3 = FuPoint<double>(x3, usedPoly.at(x3) - dRight);

    // create the lane polynomial out of the shifted points
    lanePoly.addDataXY(pointRight1);
    lanePoly.addDataXY(pointRight2);
    lanePoly.addDataXY(pointRight3);

    lanePolynomial.setLanePoly(lanePoly);
    lanePolynomial.setDetected();
    lanePolynomial.setLastUsedPosition(position);

    //ROS_INFO_STREAM("createLanePoly ending");
}

/**
 * Create the lane polynomial starting from the detected polynomial of the
 * given position. A lane polynomial is formed by shifting points with
 * different x-values of the used polynomial along the normals of the polynomial
 * at this points to the distance, where the respective lane polynomial is
 * expected to lie.
 *
 * @param position  The position of the detected polynomial used as reference
 */
void cLaneDetectionFu::createLanePolyHorizontal(ePosition position) {
    //ROS_INFO_STREAM("createLanePoly beginning");
    lanePoly.clear();

    double coef = 1.2;

    double x1 = minYPolyRoi+5;
    double x2 = minYPolyRoi + ((proj_image_h-minYPolyRoi)/2);
    double x3 = proj_image_h-5;

    FuPoint<double> pointRight1;
    FuPoint<double> pointRight2;
    FuPoint<double> pointRight3;

    double dRight = 0;

    NewtonPolynomial usedPoly;

    double y1;
    double y2;
    double y3;
 
    if (position == LEFT) {
        usedPoly = polyLeft;
        dRight = defaultXLeft-5;
    }
    else if (position == CENTER) {
        usedPoly = polyCenter;
        dRight = defaultXCenter-5;
    }
    else if (position == RIGHT) {
        usedPoly = polyRight;
        dRight = defaultXRight+5;
    }
    else if (position == HORIZONTAL) {

        x1 = 0;
        x2 = (proj_image_w)/2;
        x3 = proj_image_w-5;

        usedPoly = polyHorizontal;
        dRight = defaultYHorizontal+5;
    }


    pointRight1 = FuPoint<double>(x1, usedPoly.at(x1) - dRight);
    pointRight2 = FuPoint<double>(x2, usedPoly.at(x2) - dRight);
    pointRight3 = FuPoint<double>(x3, usedPoly.at(x3) - dRight);

    ROS_INFO_STREAM("Crear poly horizontal: (" << x1 << "," << usedPoly.at(x1) - dRight << "), " << "(" << x2 << "," << usedPoly.at(x2) - dRight << "), " << "(" << x3 << "," << usedPoly.at(x3) - dRight << ")");

    /*
    if (position == HORIZONTAL) {
        pointRight1 = FuPoint<double>(x1, usedPoly.at(x1) - dRight);
        pointRight2 = FuPoint<double>(x2, usedPoly.at(x2) - dRight);
        pointRight3 = FuPoint<double>(x3, usedPoly.at(x3) - dRight);
    }
    */

    // create the lane polynomial out of the shifted points
    
    
    lanePoly.addDataXY(pointRight1);
    lanePoly.addDataXY(pointRight2);
    lanePoly.addDataXY(pointRight3);

    lanePolynomial.setLanePoly(lanePoly);
    lanePolynomial.setDetected();
    lanePolynomial.setLastUsedPosition(position);
    
    
    //ROS_INFO_STREAM("createLanePoly ending");
}

/**
 * Decide, which of the detected polynomials (if there are any) should be used
 * as reference for creating the lane polynomials.
 *
 * @param startX    The x-value, starting from which we compare the detected polys
 */
void cLaneDetectionFu::detectLane() {
    //ROS_INFO_STREAM("detectLane beginning");
    int startX = detectLaneStartX;

    if (polyDetectedLeft && !polyDetectedCenter && !polyDetectedRight) {
        createLanePoly(LEFT);
    }
    else if (!polyDetectedLeft && polyDetectedCenter && !polyDetectedRight) {
        createLanePoly(CENTER);
    }
    else if (!polyDetectedLeft && !polyDetectedCenter && polyDetectedRight) {
        createLanePoly(RIGHT);
    }
    else if (polyDetectedLeft && polyDetectedCenter && !polyDetectedRight) {
        double gradLeft = gradient(startX, pointsLeft,
                polyLeft.getCoefficients());

        double gradCenter = nextGradient(startX, polyLeft, pointsLeft,
                pointsCenter, polyLeft.getCoefficients(),
                polyCenter.getCoefficients(), gradLeft);

        if (gradientsSimilar(gradLeft, gradCenter)) {
            createLanePoly(CENTER);
        }
        else {
            if (bestPolyLeft.second >= bestPolyCenter.second) {
                createLanePoly(LEFT);
            }
            else {
                createLanePoly(CENTER);
            }
        }
    }
    else if (!polyDetectedLeft && polyDetectedCenter && polyDetectedRight) {
        double gradCenter = gradient(startX, pointsCenter,
                polyCenter.getCoefficients());

        double gradRight = nextGradient(startX, polyCenter, pointsCenter,
                pointsRight, polyCenter.getCoefficients(),
                polyRight.getCoefficients(), gradCenter);

        if (gradientsSimilar(gradCenter, gradRight)) {
            createLanePoly(RIGHT);
            // printf("Similar \n");
        }
        else {
            if (bestPolyCenter.second >= bestPolyRight.second) {
                createLanePoly(CENTER);
                // printf("Best poly center\n");
            }
            else {
                createLanePoly(RIGHT);
                // printf("else best poly\n");
            }
        }
    }
    else if (polyDetectedLeft && !polyDetectedCenter && polyDetectedRight) {
        double gradLeft = gradient(startX, pointsLeft,
                polyLeft.getCoefficients());

        double gradRight = nextGradient(startX, polyLeft, pointsLeft,
                pointsRight, polyLeft.getCoefficients(),
                polyRight.getCoefficients(), gradLeft);

        if (gradientsSimilar(gradLeft, gradRight)) {
            createLanePoly(RIGHT);
        }
        else {
            if (bestPolyLeft.second >= bestPolyRight.second) {
                createLanePoly(LEFT);
            }
            else {
                createLanePoly(RIGHT);
            }
        }
    }
    else if (polyDetectedLeft && polyDetectedCenter && polyDetectedRight) {
        double gradRight = gradient(startX, pointsRight,
                polyRight.getCoefficients());

        double gradCenter2 = gradient(startX, pointsCenter,
                polyCenter.getCoefficients());

        double gradCenter1 = nextGradient(startX, polyRight, pointsRight,
                pointsCenter, polyRight.getCoefficients(),
                polyCenter.getCoefficients(), gradRight);

        //double gradLeft1 = nextGradient(startX, polyRight, pointsRight,
        //        pointsLeft, polyRight.getCoefficients(),
        //        polyLeft.getCoefficients(), gradRight);

        double gradLeft2 = nextGradient(startX, polyCenter, pointsCenter,
                pointsLeft, polyCenter.getCoefficients(),
                polyLeft.getCoefficients(), gradCenter2);

        if (gradientsSimilar(gradRight, gradCenter1)) {
            // ?!
            //if (gradientsSimilar(gradCenter1, gradLeft1)) {
                createLanePoly(RIGHT);
            //}
            //else {
            //    createLanePoly(RIGHT);
            //}
        }
        else {
            if (gradientsSimilar(gradCenter2, gradLeft2)) {
                createLanePoly(CENTER);
            }
            else {
                //ROS_ERROR("Creating lane according to max proportion.");
                ePosition maxPos = maxProportion();
                
                createLanePoly(maxPos);         
            }
        }
    }
    else if (!polyDetectedLeft && !polyDetectedCenter && !polyDetectedRight) {
        lanePoly.clear();
        lanePolynomial.setNotDetected();
    }
    //ROS_INFO_STREAM("detectLane ending");

    // if (polyDetectedHorizontal) {
    //     createLanePolyHorizontal(HORIZONTAL);
    // }
}

/**
 * Starts the RANSAC algorithm for detecting each of the three lane marking
 * polynomials.
 */
void cLaneDetectionFu::ransac() {
    //ROS_INFO_STREAM("ransac beginning");
    if (laneMarkingsLeft.size() > 0)
        polyDetectedLeft = ransacInternal(LEFT, laneMarkingsLeft, bestPolyLeft,
            polyLeft, supportersLeft, prevPolyLeft, pointsLeft);
    else
        polyDetectedLeft = false;
    if (laneMarkingsCenter.size() > 0)
        polyDetectedCenter = ransacInternal(CENTER, laneMarkingsCenter,
            bestPolyCenter, polyCenter, supportersCenter, prevPolyCenter,
            pointsCenter);
    else
        polyDetectedCenter = false;
    if (laneMarkingsRight.size() > 0)
        polyDetectedRight = ransacInternal(RIGHT, laneMarkingsRight, bestPolyRight,
            polyRight, supportersRight, prevPolyRight, pointsRight);
    else
        polyDetectedRight = false;

    polyDetectedHorizontal = ransacInternal(HORIZONTAL, laneMarkingsHorizontal, bestPolyHorizontal,
            polyHorizontal, supportersHorizontal, prevPolyHorizontal, pointsHorizontal);
    // ROS_INFO_STREAM("ransac ending");
}

/**
 * Detects a polynomial with RANSAC in a given list of lane marking edge points.
 *
 * @param position      The position of the wanted polynomial
 * @param laneMarkings  A reference to the list of lane marking edge points
 * @param bestPoly      A reference to a pair containing the present best
 *                      detected polynomial and a value representing the fitting
 *                      quality called proportion
 * @param poly          A reference to the polynomial that gets detected
 * @param supporters    A reference to the supporter points of the present best
 *                      polynomial
 * @param prevPoly      A reference to the previous polynomial detected at this
 *                      position
 * @param points        A reference to the points selected for interpolating the
 *                      present best polynomial
 * @return              true if a polynomial could be detected and false when not
 */
bool cLaneDetectionFu::ransacInternal(ePosition position,
        std::vector<FuPoint<int>>& laneMarkings,
        std::pair<NewtonPolynomial, double>& bestPoly, NewtonPolynomial& poly,
        std::vector<FuPoint<int>>& supporters, NewtonPolynomial& prevPoly,
        std::vector<FuPoint<int>>& points) {
    //ROS_INFO_STREAM("ransacInternal beginning");

    if (laneMarkings.size() < 7) {
        return false;
    }

    int iterations = 0;

    // sort the lane marking edge points
    std::vector<FuPoint<int>> sortedMarkings = laneMarkings;

    std::sort(sortedMarkings.begin(), sortedMarkings.end(),
            [](FuPoint<int> a, FuPoint<int> b) {
                return a.getY() < b.getY();
            });

    std::vector<FuPoint<int>> tmpSupporters = std::vector<FuPoint<int>>();

    // vectors for points selected from the bottom, mid and top of the sorted
    // point vector
    std::vector<FuPoint<int>> markings1 = std::vector<FuPoint<int>>();
    std::vector<FuPoint<int>> markings2 = std::vector<FuPoint<int>>();
    std::vector<FuPoint<int>> markings3 = std::vector<FuPoint<int>>();

    bool highEnoughY = false;

    // Points are selected from the bottom, mid and top. The selection regions
    // are spread apart for better results during RANSAC
    for (std::vector<FuPoint<int>>::size_type i = 0; i != sortedMarkings.size();
            i++) {
        if(position == HORIZONTAL) {
            if (i < double(sortedMarkings.size())*1/3) {
                markings1.push_back(sortedMarkings[i]);
            }
            else if (i >= (double(sortedMarkings.size())*1/3)
                    && i < (double(sortedMarkings.size())*2/3)) {
                markings2.push_back(sortedMarkings[i]);
            }
            else if (i >= (double(sortedMarkings.size())*2/3)) {
                markings3.push_back(sortedMarkings[i]);
            }
        }
        else {
            if (i < double(sortedMarkings.size()) / 7) {
                markings1.push_back(sortedMarkings[i]);
            }
            else if (i >= (double(sortedMarkings.size()) / 7) * 3
                    && i < (double(sortedMarkings.size()) / 7) * 4) {
                markings2.push_back(sortedMarkings[i]);
            }
            else if (i >= (double(sortedMarkings.size()) / 7) * 6) {
                markings3.push_back(sortedMarkings[i]);
            }
            if (sortedMarkings[i].getY() > 5) {
                highEnoughY = true;
            }
        }
    }

    //what is this for?
    if (position == CENTER) {
        if (!highEnoughY) {
            prevPoly = poly;
            poly.clear();
            return false;
        }
    }

    // save the polynomial from the previous picture
    prevPoly = poly;

    while (iterations < iterationsRansac) {
        iterations++;

        // randomly select 3 different lane marking points from bottom, mid and
        // top
        int pos1 = rand() % markings1.size();
        int pos2 = rand() % markings2.size();
        int pos3 = rand() % markings3.size();

        FuPoint<int> p1 = markings1[pos1];
        FuPoint<int> p2 = markings2[pos2];
        FuPoint<int> p3 = markings3[pos3];

        double p1X = p1.getX();
        double p1Y = p1.getY();
        double p2X = p2.getX();
        double p2Y = p2.getY();
        double p3X = p3.getX();
        double p3Y = p3.getY();

        // clear poly for reuse
        poly.clear();

        // create a polynomial with the selected points
        if(position==HORIZONTAL){
            poly.addData(p1Y, p1X);
            poly.addData(p2Y, p2X);
            poly.addData(p3Y, p3X);
        }
        else
        {
            poly.addData(p1X, p1Y);
            poly.addData(p2X, p2Y);
            poly.addData(p3X, p3Y);
        }

        // check if this polynomial is not useful 
        // BASED ON DISTANCE TO DEFAULT LINES AND PREVIOUS POLY
        // OMITIDO por eliminar uso de lineas en favor de dbscan
        //if (!polyValid(position, poly, prevPoly)) {
        //    poly.clear();
        //    continue;
        //}

        // count the supporters and save them for debugging
        int count1 = 0;
        int count2 = 0;
        int count3 = 0;

        // find the supporters
        tmpSupporters.clear();

        for (FuPoint<int> p : markings1) {
            if (position == HORIZONTAL)
            {
                if (vertDistanceToPolynomial(poly, p) <= maxDistance) {
                    count1++;
                    tmpSupporters.push_back(p);
                }
            }
            else {
                if (horizDistanceToPolynomial(poly, p) <= maxDistance) {
                    count1++;
                    tmpSupporters.push_back(p);
                }
            }
        }

        for (FuPoint<int> p : markings2) {
            if (position == HORIZONTAL)
            {
                if (vertDistanceToPolynomial(poly, p) <= maxDistance) {
                    count2++;
                    tmpSupporters.push_back(p);
                }
            }
            else {
                if (horizDistanceToPolynomial(poly, p) <= maxDistance) {
                    count2++;
                    tmpSupporters.push_back(p);
                }
            }
        }

        for (FuPoint<int> p : markings3) {
            if (position == HORIZONTAL)
            {
                if (vertDistanceToPolynomial(poly, p) <= maxDistance) {
                    count3++;
                    tmpSupporters.push_back(p);
                }
            }
            else {
                if (horizDistanceToPolynomial(poly, p) <= maxDistance) {
                    count3++;
                    tmpSupporters.push_back(p);
                }
            }
        }

        if (count1 == 0 || count2 == 0 || count3 == 0) {
            poly.clear();
            //DEBUG_TEXT(dbgMessages, "Poly had no supporters in one of the regions");
            continue;
        }

        // calculate the proportion of supporters of all lane markings
        double proportion = (double(count1) / markings1.size()
                + double(count2) / markings2.size()
                + 3 * (double(count3) / markings3.size())) / 5;

        

        if (proportion < proportionThreshould) {
            poly.clear();
            //DEBUG_TEXT(dbgMessages, "Poly proportion was smaller than threshold");
            continue;
        }

        // check if poly is better than bestPoly
        if (proportion > bestPoly.second) {
            bestPoly = std::make_pair(poly, proportion);
            supporters = tmpSupporters;

            points.clear();
            points.push_back(p1);
            points.push_back(p2);
            points.push_back(p3);

            /*
            if(position == HORIZONTAL){
                ROS_INFO_STREAM("Buscando HORIZONTAL: " << proportion << "," << count1 << "," << count2 << "," << count3);
            
                ROS_INFO_STREAM("P1 x: " << p1.getX() << ", y: " << p1.getY());
                ROS_INFO_STREAM("P2 x: " << p2.getX() << ", y: " << p2.getY());
                ROS_INFO_STREAM("P3 x: " << p3.getX() << ", y: " << p3.getY());

                ROS_INFO_STREAM("P1C x: " << p1.getX() << ", y: " << poly.at(p1.getX()));
            }
            */
        }
    }

    poly = bestPoly.first;

    /*
    if(position == HORIZONTAL){
            ROS_INFO_STREAM("Poly degree: " << poly.getDegree());
        }
    */

    if (poly.getDegree() == -1) {
        return false;
    }
    //ROS_INFO_STREAM("ransacInternal ending");
    return true;
}

/**
 * Method, that checks, if a polynomial produced during RANSAC counts as usable.
 *
 * @param position  The position of the polynomial, that is checked
 * @param poly      The polynomial, that is checked
 * @param prevPoly  The previous polynomial detected at this position
 * @return          True, if the polynomial counts as valid
 */
bool cLaneDetectionFu::polyValid(ePosition position, NewtonPolynomial poly,
        NewtonPolynomial prevPoly) {
    //ROS_INFO_STREAM("polyValid");
    FuPoint<int> p1 = FuPoint<int>(poly.at(polyY1), polyY1);    //y = 75

    if (horizDistanceToDefaultLine(position, p1) > interestDistancePoly) { // 10) {
        //ROS_INFO("Poly was to far away from default line at y = 25");
        return false;
    }

    FuPoint<int> p2 = FuPoint<int>(poly.at(polyY2), polyY2);    //y = 60

    if (horizDistanceToDefaultLine(position, p2) > interestDistancePoly) { // 20) {
        //ROS_INFO("Poly was to far away from default line at y = 25");
        return false;
    }

    FuPoint<int> p3 = FuPoint<int>(poly.at(polyY3), polyY3);    //y = 40

    if (horizDistanceToDefaultLine(position, p3) > interestDistancePoly) { // 40) {
        //ROS_INFO("Poly was to far away from default line at y = 30");
        return false;
    }

    if (prevPoly.getDegree() != -1) {
        FuPoint<int> p4 = FuPoint<int>(poly.at(polyY1), polyY1);
        FuPoint<int> p5 = FuPoint<int>(prevPoly.at(polyY1), polyY1);

        if (horizDistance(p4, p5) > 5) { //0.05 * meters) {
            //ROS_INFO("Poly was to far away from previous poly at y = 25");
            return false;
        }

        FuPoint<int> p6 = FuPoint<int>(poly.at(polyY2), polyY2);
        FuPoint<int> p7 = FuPoint<int>(prevPoly.at(polyY2), polyY2);

        if (horizDistance(p6, p7) > 5) { //0.05 * meters) {
            //ROS_INFO("Poly was to far away from previous poly at y = 30");
            return false;
        }
    }

    //ROS_INFO_STREAM("polyValid ending");

    return true;
}



void cLaneDetectionFu::pubRGBImageMsg(cv::Mat& rgb_mat, image_transport::CameraPublisher publisher)
{
    //ROS_INFO_STREAM("pubRGBImageMsg beginning");
    sensor_msgs::ImagePtr rgb_img(new sensor_msgs::Image);

    rgb_img->header.seq = head_sequence_id;
    rgb_img->header.stamp = head_time_stamp;
    rgb_img->header.frame_id = rgb_frame_id;

    rgb_img->width = rgb_mat.cols;
    rgb_img->height = rgb_mat.rows;

    rgb_img->encoding = sensor_msgs::image_encodings::BGR8;
    rgb_img->is_bigendian = 0;

    int step = sizeof(unsigned char) * 3 * rgb_img->width;
    int size = step * rgb_img->height;
    rgb_img->step = step;
    rgb_img->data.resize(size);
    memcpy(&(rgb_img->data[0]), rgb_mat.data, size);

    rgb_camera_info->header.frame_id = rgb_frame_id;
    rgb_camera_info->header.stamp = head_time_stamp;
    rgb_camera_info->header.seq = head_sequence_id;

    publisher.publish(rgb_img, rgb_camera_info);
    //ROS_INFO_STREAM("pubRGBImageMsg ending");
}

void cLaneDetectionFu::pubAngle()
{
    //ROS_INFO_STREAM("pubAngle beginning");
    if (!lanePolynomial.hasDetected()) {
	return;
    }

    double oppositeLeg = lanePolynomial.getLanePoly().at(proj_image_h-angleAdjacentLeg);    
    double result = atan (oppositeLeg/angleAdjacentLeg) * 180 / PI;

    if (std::abs(result - lastAngle) > maxAngleDiff) {
        if (result - lastAngle > 0) {
            result = lastAngle + maxAngleDiff;
        } else {
            result = lastAngle - maxAngleDiff;
        }
    }

    lastAngle = result;

    std_msgs::Float32 angleMsg;

    angleMsg.data = result;

    publish_angle.publish(angleMsg);
    //ROS_INFO_STREAM("pubAngle ending");
}

void cLaneDetectionFu::pubGradientAngle()
{
    int position = proj_image_h - angleAdjacentLeg;

    double val1 = lanePolynomial.getLanePoly().at(position);
    double val2 = lanePolynomial.getLanePoly().at(position+1);

    double result = atan (val1-val2) * 180 / PI;

    lastAngle = result;

    std_msgs::Float32 angleMsg;

    angleMsg.data = result;

    publish_angle.publish(angleMsg);
}

void cLaneDetectionFu::config_callback(line_detection_fu::LaneDetectionConfig &config, uint32_t level) {
    ROS_INFO_STREAM("Reconfigure Request");

    defaultXLeft = config.defaultXLeft;
    defaultXCenter = config.defaultXCenter;
    defaultXRight = config.defaultXRight;
    interestDistancePoly = config.interestDistancePoly;
    interestDistanceDefault= config.interestDistanceDefault;
    iterationsRansac = config.iterationsRansac;
    maxYRoi = config.maxYRoi;
    minYDefaultRoi = config.minYDefaultRoi;
    minYPolyRoi = config.minYPolyRoi;
    polyY1 = config.polyY1;
    polyY2 = config.polyY2;
    polyY3 = config.polyY3;
    detectLaneStartX = config.detectLaneStartX;
    maxAngleDiff = config.maxAngleDiff;
    proj_y_start = config.proj_y_start;
    roi_top_w = config.roi_top_w;
    roi_bottom_w = config.roi_bottom_w;
    proportionThreshould = config.proportionThreshould;
    m_gradientThreshold = config.m_gradientThreshold;
    m_nonMaxWidth = config.m_nonMaxWidth;
    laneMarkingSquaredThreshold = config.laneMarkingSquaredThreshold;
    angleAdjacentLeg = config.angleAdjacentLeg;
    scanlinesVerticalDistance = config.scanlinesVerticalDistance;
    scanlinesMaxCount = config.scanlinesMaxCount;

    scanlines = getScanlines();
    //ROS_INFO_STREAM("after getScanlines");
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "cLaneDetectionFu");
    ros::NodeHandle nh;
    ros::Rate loop_rate(RATE_HZ);

    cLaneDetectionFu node = cLaneDetectionFu(nh);

    dynamic_reconfigure::Server<line_detection_fu::LaneDetectionConfig> server;
    dynamic_reconfigure::Server<line_detection_fu::LaneDetectionConfig>::CallbackType f;
    f = boost::bind(&cLaneDetectionFu::config_callback, &node, _1, _2);
    server.setCallback(f);

    while(ros::ok())
    {
        ros::spinOnce();
        loop_rate.sleep();
    }

    return 0;
}
