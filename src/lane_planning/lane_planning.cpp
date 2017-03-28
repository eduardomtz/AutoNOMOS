#include <ros/ros.h>
#include <std_msgs/Header.h>
#include <iostream>
#include <geometry_msgs/Point.h>
#include <std_msgs/Int32MultiArray.h>
#include <std_msgs/Float32MultiArray.h>
#include <nav_msgs/GridCells.h>
#include <std_msgs/Int16.h>

#define NUM_STATES 6
#define LEFT 0
#define RIGHT 1

static const uint32_t MY_ROS_QUEUE_SIZE = 1;

double rate_hz = 1;
ros::Publisher pub_path;
// ros::Publisher pub_lidar;

std::string nombre;

nav_msgs::GridCells arr_left; 
nav_msgs::GridCells arr_center;
nav_msgs::GridCells arr_right;

nav_msgs::GridCells path_planned;

// estados: 	 NSI,   FI,   CI,   CD,   FD, NSD
void get_pts_left(const nav_msgs::GridCells& array)
{
	arr_left.cells = array.cells;
	arr_left.cell_width = array.cell_width;
	// lines_sensed = array.cell_width > 0 ?  lines_sensed | 4 : lines_sensed | 0;
}

void get_pts_center(const nav_msgs::GridCells& array)
{
	arr_center.cells = array.cells;
	arr_center.cell_width = array.cell_width;
	// lines_sensed = array.cell_width > 0 ?  lines_sensed | 2 : lines_sensed | 0;
}

void get_pts_right(const nav_msgs::GridCells& array)
{
	arr_right.cells = array.cells;
	arr_right.cell_width = array.cell_width;
	// ROS_INFO_STREAM("Array: " << array);
	// lines_sensed = array.cell_width > 0 ?  lines_sensed | 1 : lines_sensed | 0;
}

// sapruder
void get_path(int lane){
	// ROS_INFO_STREAM("L: " << arr_left.cell_width << "C: " << arr_center.cell_width << "R: " << arr_right.cell_width);

	geometry_msgs::Point pt;
	path_planned.cell_width = arr_center.cell_width;
    path_planned.cell_height = 1;
    path_planned.cells.clear();
    
	for(int i=0;i<arr_center.cell_width;i++){
		if(arr_right.cell_width > 0 && lane == RIGHT && arr_right.cells[i].x > 0){
			pt.x = (arr_center.cells[i].x + arr_right.cells[i].x)/2;
            pt.y = (arr_center.cells[i].y + arr_right.cells[i].y)/2;
            pt.z = 0;
		}
		else if(arr_left.cell_width > 0 && lane == LEFT && arr_left.cells[i].x > 0){
			pt.x = (arr_center.cells[i].x + arr_left.cells[i].x)/2;
            pt.y = (arr_center.cells[i].y + arr_left.cells[i].y)/2;
            pt.z = 0;
		}
		else{
			// al menos la linea del centro se ve
		}
        path_planned.cells.push_back(pt);
	}
}

void planning(){
	get_path(RIGHT);
	// nav_msgs::GridCells* path_right = &get_path(RIGHT);
	// path_right.cells = 
	// path_right.cell_width = arr_center.cell_width;
	ROS_INFO_STREAM("Planned path: ");
	for(int i=0; i<path_planned.cell_width; i++){
		ROS_INFO_STREAM("i: " << i << ", x: " << path_planned.cells[i].x << ", y: " << path_planned.cells[i].y);
	}

	pub_path.publish(path_planned);
}




int main(int argc, char** argv){
	ros::init(argc, argv, "lane_states_node");
	ROS_INFO_STREAM("lane_states_node initialized");
	ros::NodeHandle nh;
	ros::Rate loop_rate(rate_hz);
	
	pub_path = nh.advertise<nav_msgs::GridCells>("/planning", rate_hz);
	
	ros::Subscriber sub_pts_left = nh.subscribe("/points/ransac_left",1, &get_pts_left);
	ros::Subscriber sub_pts_center = nh.subscribe("/points/ransac_center",1, &get_pts_center);
	ros::Subscriber sub_pts_right = nh.subscribe("/points/ransac_right",1, &get_pts_right);
	
	
	while(nh.ok())
	{

	    ros::spinOnce();
	    
	    planning();
	    // p = move(p);
	    //pub_path.publish(p);

	    loop_rate.sleep();
	}
	return 0;
};

