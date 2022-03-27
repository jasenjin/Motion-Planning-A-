#include <iostream>
#include <fstream>
#include <math.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <ros/ros.h>
#include <ros/console.h>
#include <sensor_msgs/PointCloud2.h>

#include <nav_msgs/Odometry.h>
#include <nav_msgs/Path.h>
#include <geometry_msgs/PoseStamped.h>
#include <visualization_msgs/MarkerArray.h>
#include <visualization_msgs/Marker.h>

#include "Astar_searcher.h"
#include "JPS_searcher.h"
#include "backward.hpp"

using namespace std;
using namespace Eigen;

namespace backward {
backward::SignalHandling sh;
}

// simulation param from launch file
double _resolution, _inv_resolution, _cloud_margin;//���ȣ����ȵ��������Ƽ�϶
double _x_size, _y_size, _z_size;    //��ͼx y z �ĳߴ�

// useful global variables
bool _has_map   = false;

Vector3d _start_pt;//Vector3d ��һ���࣬��xyz��������     ������ʼ��ָ��
Vector3d _map_lower, _map_upper;//��ͼ���½�
int _max_x_id, _max_y_id, _max_z_id;//��ͼx y z ���id

// ros related
ros::Subscriber _map_sub, _pts_sub;
ros::Publisher  _grid_path_vis_pub, _visited_nodes_vis_pub, _grid_map_vis_pub;

AstarPathFinder * _astar_path_finder     = new AstarPathFinder();
JPSPathFinder   * _jps_path_finder       = new JPSPathFinder();

void rcvWaypointsCallback(const nav_msgs::Path & wp);
void rcvPointCloudCallBack(const sensor_msgs::PointCloud2 & pointcloud_map);

void visGridPath( vector<Vector3d> nodes, bool is_use_jps );// ʵ�廯��������·��
void visVisitedNode( vector<Vector3d> nodes );// ʵ�廯��ʾ���з��ʹ��Ľڵ�
void pathFinding(const Vector3d start_pt, const Vector3d target_pt);// ·���滮����

void rcvWaypointsCallback(const nav_msgs::Path & wp)
{     
    if( wp.poses[0].pose.position.z < 0.0 || _has_map == false )// ����յ���Ϣ��zֵС��0,����û�е�ͼ��Ϣ,��ֱ�ӷ���
        return;

    Vector3d target_pt;
    //��ȡ����ʽ����������յ�����
    // ���յ���Ϣ���ݸ��յ�ָ��
    target_pt << wp.poses[0].pose.position.x,
                 wp.poses[0].pose.position.y,
                 wp.poses[0].pose.position.z;

    ROS_INFO("[node] receive the planning target");// �ն˴�ӡ�Ѿ����յ��յ���Ϣ
    //������㡢�յ㣬���� pathFind ����
    pathFinding(_start_pt, target_pt); // ·���滮
}

void rcvPointCloudCallBack(const sensor_msgs::PointCloud2 & pointcloud_map)
{   
    if(_has_map ) return;// ����Ѿ��е�ͼ��Ϣ,�򷵻�

    pcl::PointCloud<pcl::PointXYZ> cloud;//pcl::PointCloud<pcl::PointXYZ>���Ǳ�ʾ PCL �����ڴ洢 3D �㼯�ϵĻ���
    pcl::PointCloud<pcl::PointXYZ> cloud_vis;
    sensor_msgs::PointCloud2 map_vis;//sensor_msgs::PointCloud2��һ��������ݽṹ

    pcl::fromROSMsg(pointcloud_map, cloud);//���ݸ�ʽת��
    // �����Ƹ�ʽΪsensor_msgs/PointCloud2 ��ʽתΪ pcl/PointCloud(��RVIZ����ʾ�ĵ��Ƶ����ݸ�ʽsensor_msgs::PointCloud2)
    
    if( (int)cloud.points.size() == 0 ) return; // �����û�е�������,�򷵻�

    pcl::PointXYZ pt;
    for (int idx = 0; idx < (int)cloud.points.size(); idx++)
    {    
        pt = cloud.points[idx];        

        // set obstalces into grid map for path planning
        // ���ϰ�����Ϣ���ý���դ�񻯵�ͼ��Ϊ����·���滮��׼��
        _astar_path_finder->setObs(pt.x, pt.y, pt.z);
        _jps_path_finder->setObs(pt.x, pt.y, pt.z);

        // for visualize only   // ���ӻ���ͼ����
        Vector3d cor_round = _astar_path_finder->coordRounding(Vector3d(pt.x, pt.y, pt.z));
        pt.x = cor_round(0);
        pt.y = cor_round(1);
        pt.z = cor_round(2);
        cloud_vis.points.push_back(pt); // ����ѹ���ջ
    }

    cloud_vis.width    = cloud_vis.points.size();
    cloud_vis.height   = 1;
    cloud_vis.is_dense = true;

    pcl::toROSMsg(cloud_vis, map_vis);// pcl::toROSMsg (pcl::PointCloud<pcl::PointXYZ>,sensor_msgs::PointCloud2);

    map_vis.header.frame_id = "/world";
    _grid_map_vis_pub.publish(map_vis);//������ͼ��Ϣ

    _has_map = true;
}

void pathFinding(const Vector3d start_pt, const Vector3d target_pt)
{
    //Call A* to search for a path
    //ʹ�� A*����·������
    _astar_path_finder->AstarGraphSearch(start_pt, target_pt);

    //Retrieve the path
    //��ȡ�滮��·��
    auto grid_path     = _astar_path_finder->getPath();
    auto visited_nodes = _astar_path_finder->getVisitedNodes();

    //Visualize the result
    //���ӻ����
    visGridPath (grid_path, false);
    visVisitedNode(visited_nodes);// ��ʾ���з��ʹ��Ľڵ�(����û����,������ʾ������,û�ҵ�����)

    //Reset map for next call
    //Ϊ�´ι滮���õ�ͼ
    _astar_path_finder->resetUsedGrids();

    //���� JPS ·���滮��дʱ����_use_jps ��ֵ��Ϊ 1 ����
    //_use_jps = 0 -> Do not use JPS
    //_use_jps = 1 -> Use JPS
    //you just need to change the #define value of _use_jps
#define _use_jps 0
#if _use_jps
    {
        //Call JPS to search for a path
        //ʹ�� JPS ����·������
        _jps_path_finder -> JPSGraphSearch(start_pt, target_pt);

        //Retrieve the path
        auto grid_path     = _jps_path_finder->getPath();
        auto visited_nodes = _jps_path_finder->getVisitedNodes();

        //Visualize the result
        visGridPath   (grid_path, _use_jps);
        visVisitedNode(visited_nodes);

        //Reset map for next call
        _jps_path_finder->resetUsedGrids();
    }
#endif
}

int main(int argc, char** argv)
{
    ros::init(argc, argv, "demo_node");//ROS�ڵ��ʼ��
    ros::NodeHandle nh("~");//�����ڵ���
    //���ĵ���ͼ��Ϣ�Ļص�����
    _map_sub  = nh.subscribe( "map",       1, rcvPointCloudCallBack );
    //���ĵ��յ���Ϣ�Ļص�����
    _pts_sub  = nh.subscribe( "waypoints", 1, rcvWaypointsCallback );

    _grid_map_vis_pub             = nh.advertise<sensor_msgs::PointCloud2>("grid_map_vis", 1);
    _grid_path_vis_pub            = nh.advertise<visualization_msgs::Marker>("grid_path_vis", 1);
    _visited_nodes_vis_pub        = nh.advertise<visualization_msgs::Marker>("visited_nodes_vis",1);
  //param�ǴӲ����������л�ȡ��һ������,��ֵ���浽�ڶ���������,���û��ȡ����һ��������ֵ,��ʹ��Ĭ��ֵ(����������)
    nh.param("map/cloud_margin",  _cloud_margin, 0.0);
    nh.param("map/resolution",    _resolution,   0.2);
    
    nh.param("map/x_size",        _x_size, 50.0);
    nh.param("map/y_size",        _y_size, 50.0);
    nh.param("map/z_size",        _z_size, 5.0 );
    
    nh.param("planning/start_x",  _start_pt(0),  0.0);
    nh.param("planning/start_y",  _start_pt(1),  0.0);
    nh.param("planning/start_z",  _start_pt(2),  0.0);

    _map_lower << - _x_size/2.0, - _y_size/2.0,     0.0;
    _map_upper << + _x_size/2.0, + _y_size/2.0, _z_size;
    
    _inv_resolution = 1.0 / _resolution;
    
    _max_x_id = (int)(_x_size * _inv_resolution);
    _max_y_id = (int)(_y_size * _inv_resolution);
    _max_z_id = (int)(_z_size * _inv_resolution);

    //�����˽ṹ�� AstarPathFinder ����_astar_path_finder���ýṹ��洢��ʵ���� Astar ·���滮�����������Ϣ�͹���
    _astar_path_finder  = new AstarPathFinder();
    _astar_path_finder  -> initGridMap(_resolution, _map_lower, _map_upper, _max_x_id, _max_y_id, _max_z_id);
    //�����˽ṹ�� JPSPathFinder ����_jps_path_finder���ýṹ��洢��ʵ���� JPS ·���滮�����������Ϣ�͹���
    _jps_path_finder    = new JPSPathFinder();
    _jps_path_finder    -> initGridMap(_resolution, _map_lower, _map_upper, _max_x_id, _max_y_id, _max_z_id);
    
    ros::Rate rate(100);
    bool status = ros::ok();
    while(status) 
    {
        ros::spinOnce();      
        status = ros::ok();
        rate.sleep();
    }

    delete _astar_path_finder;
    delete _jps_path_finder;
    return 0;
}
// ʵ�廯��������·��
void visGridPath( vector<Vector3d> nodes, bool is_use_jps )
{   
    visualization_msgs::Marker node_vis; 
    node_vis.header.frame_id = "world";
    node_vis.header.stamp = ros::Time::now();
    
    if(is_use_jps)// is_use_jps�������ж��õ��ǲ���jps�㷨
        node_vis.ns = "demo_node/jps_path";
    else
        node_vis.ns = "demo_node/astar_path";

    node_vis.type = visualization_msgs::Marker::CUBE_LIST;
    node_vis.action = visualization_msgs::Marker::ADD;
    node_vis.id = 0;

    node_vis.pose.orientation.x = 0.0;
    node_vis.pose.orientation.y = 0.0;
    node_vis.pose.orientation.z = 0.0;
    node_vis.pose.orientation.w = 1.0;
    //·������ɫ
    if(is_use_jps){
        node_vis.color.a = 1.0;
        node_vis.color.r = 1.0;
        node_vis.color.g = 0.0;
        node_vis.color.b = 0.0;
    }
    else{
        node_vis.color.a = 1.0;
        node_vis.color.r = 0.0;
        node_vis.color.g = 1.0;
        node_vis.color.b = 0.0;
    }


    node_vis.scale.x = _resolution;
    node_vis.scale.y = _resolution;
    node_vis.scale.z = _resolution;

    geometry_msgs::Point pt;// ������·���еĵ�ѹ���ջ
    for(int i = 0; i < int(nodes.size()); i++)
    {
        Vector3d coord = nodes[i];
        pt.x = coord(0);
        pt.y = coord(1);
        pt.z = coord(2);

        node_vis.points.push_back(pt);
    }

    _grid_path_vis_pub.publish(node_vis);// ������ʾ����·���ڵ�
}

void visVisitedNode( vector<Vector3d> nodes )
{   
    visualization_msgs::Marker node_vis; 
    node_vis.header.frame_id = "world";
    node_vis.header.stamp = ros::Time::now();
    node_vis.ns = "demo_node/expanded_nodes";
    node_vis.type = visualization_msgs::Marker::CUBE_LIST;
    node_vis.action = visualization_msgs::Marker::ADD;
    node_vis.id = 0;

    node_vis.pose.orientation.x = 0.0;
    node_vis.pose.orientation.y = 0.0;
    node_vis.pose.orientation.z = 0.0;
    node_vis.pose.orientation.w = 1.0;
    node_vis.color.a = 0.5;
    node_vis.color.r = 0.0;
    node_vis.color.g = 0.0;
    node_vis.color.b = 1.0;

    node_vis.scale.x = _resolution;
    node_vis.scale.y = _resolution;
    node_vis.scale.z = _resolution;

    geometry_msgs::Point pt;
    for(int i = 0; i < int(nodes.size()); i++)
    {
        Vector3d coord = nodes[i];
        pt.x = coord(0);
        pt.y = coord(1);
        pt.z = coord(2);

        node_vis.points.push_back(pt);
    }

    _visited_nodes_vis_pub.publish(node_vis);
}