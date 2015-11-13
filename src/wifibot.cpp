#include "libwifibot.h"
#include "wifibot.h"
#include "ros/ros.h"
#include "roswifibot/Status.h"

#define TWOPI (M_PI * 2)

Wifibot::Wifibot(const ros::NodeHandle& nh)
  : _nh (nh)
  , _updated(false)
  , _speedLeft(0.0)
  , _speedRight(0.0)
{
  // Parameters handler
  ros::NodeHandle pn("~");

  // Get device port parameter
  std::string dev;
  if (!pn.getParam("port", dev))
    {
      dev = "/dev/ttyS0";
      ROS_INFO("No device port set. Assuming : %s", dev.c_str());
    }

  // get base frame parameter
  std::string frameBase;
  if (!pn.getParam("base_frame", frameBase))
    _frameBase = "base_frame";
  else
    _frameBase = frameBase;

  // get entrax parameter
  double entrax;
  if (!pn.getParam("entrax", entrax))
    _entrax = 0.30;
  else
    _entrax = entrax;

  bool relay1, relay2, relay3;
  pn.param("relay1", relay1, false);
  pn.param("relay2", relay2, false);
  pn.param("relay3", relay3, false);

  ROS_INFO("Wifibot device : %s. Entrax : %0.3f, relay1:%i, relay2:%i, relay3:%i",
     dev.c_str(), _entrax, relay1, relay2, relay3);

  // Create and configure the driver
  _pDriver = new wifibot::Driver(dev);
  _pDriver->setRelays(relay1, relay2, relay3);
  _pDriver->loopControlSpeed(0.01);  // Default loop control speed
  _pDriver->setPid(0.8, 0.45, 0.0);  // Default PID values
  _pDriver->setTicsPerMeter(5312.0); // Adapt this value according your wheels size

  // Save initial position
  wifibot::driverData st = _pDriver->readData();
  _odometryLeftLast = st.odometryLeft;
  _odometryRightLast = st.odometryRight;

  _position.x = 0;
  _position.y = 0;
  _position.th = 0;

  // Create topics
  _pubOdometry = _nh.advertise<nav_msgs::Odometry>("odom", 10);
  _pubStatus = _nh.advertise<roswifibot::Status>("status", 10);
  _subSpeeds = _nh.subscribe("cmd_vel", 1, &Wifibot::velocityCallback, this);

  ros::Rate r(100);

  _timeCurrent = ros::Time::now();
  _timeLast = ros::Time::now();

  while (ros::ok())
    {
      ros::spinOnce();
      update();
      r.sleep();
    }
}

Wifibot::~Wifibot()
{
  delete _pDriver;
}

double Wifibot::getSpeedLinear(double left, double right)
{
  return (right + left) / 2.0;
}

double Wifibot::getSpeedAngular(double left, double right)
{
  return (right - left) / _entrax;
}

void Wifibot::computeOdometry(double left, double right)
{
  double dleft = left - _odometryLeftLast;
  double dright = right - _odometryRightLast;

  double distance = getSpeedLinear(dleft, dright);

  _position.th += getSpeedAngular(dleft, dright);
  _position.th -= (float)((int)(_position.th / TWOPI)) * TWOPI;

  _position.x += distance * cos(_position.th);
  _position.y += distance * sin(_position.th);

  _odometryLeftLast = left;
  _odometryRightLast = right;
}

void Wifibot::velocityCallback(const geometry_msgs::TwistConstPtr& vel)
{
  //ROS_INFO("input : %0.3f | %0.3f", vel->linear.x, vel->angular.z);

  _speedLeft = vel->linear.x - (vel->angular.z * (_entrax / 2.0));
  _speedRight =  vel->linear.x + (vel->angular.z * (_entrax / 2.0));
  _updated = true;
}

void Wifibot::update()
{
  roswifibot::Status topicStatus;

  // Send speeds only if needed
  if (_updated)
    _pDriver->setSpeeds(_speedLeft, _speedRight);
  _updated = false;

  // get data from driver
  wifibot::driverData st = _pDriver->readData();

  _timeCurrent = ros::Time::now();

  // Fill status topic
  topicStatus.battery_level = st.voltage;
  topicStatus.current = st.current; // see libwifibot to adapt this value
  topicStatus.ADC1 = st.adc[0];
  topicStatus.ADC2 = st.adc[1];
  topicStatus.ADC3 = st.adc[2];
  topicStatus.ADC4 = st.adc[3];

  topicStatus.speed_front_left = st.speedFrontLeft;
  topicStatus.speed_front_right = st.speedFrontRight;

  topicStatus.odometry_left = st.odometryLeft;
  topicStatus.odometry_right = st.odometryRight;
  topicStatus.version = st.version;
  topicStatus.relay1 = 0;

  // publish status
  _pubStatus.publish(topicStatus);

  // compute position
  computeOdometry(st.odometryLeft, st.odometryRight);

  //TRANSFORM we'll publish the transform over tf
  _odomTf.header.stamp = _timeCurrent;
  _odomTf.header.frame_id = "/odom";
  _odomTf.child_frame_id = _frameBase;

  _odomTf.transform.translation.x = _position.x;
  _odomTf.transform.translation.y = _position.y;
  _odomTf.transform.translation.z = 0.0;
  _odomTf.transform.rotation =
    tf::createQuaternionMsgFromYaw(_position.th);

  //send the transform
  _odomBroadcast.sendTransform(_odomTf);


  //TOPIC, we'll publish the odometry message over ROS
  nav_msgs::Odometry odometryTopic;
  odometryTopic.header.stamp = _timeCurrent;
  odometryTopic.header.frame_id = "/odom";

  //set the position
  odometryTopic.pose.pose.position.x = _position.x;
  odometryTopic.pose.pose.position.y = _position.y;
  odometryTopic.pose.pose.position.z = 0.0;
  odometryTopic.pose.pose.orientation =
    tf::createQuaternionMsgFromYaw(_position.th);

  //set the velocity
  odometryTopic.child_frame_id = _frameBase;
  odometryTopic.twist.twist.linear.x = getSpeedLinear(
    st.speedFrontLeft, st.speedFrontRight);
  odometryTopic.twist.twist.linear.y = 0.0;
  odometryTopic.twist.twist.angular.z = getSpeedAngular(
    st.speedFrontLeft, st.speedFrontRight);

  /*
  ROS_INFO("lin:%0.3f ang:%0.3f",
     odometryTopic.twist.twist.linear.x,
     odometryTopic.twist.twist.angular.z);
  */

  //publish the message
  _pubOdometry.publish(odometryTopic);

  // Update last time
  _timeLast = _timeCurrent;
}

int main (int argc, char **argv)
{
  ros::init(argc, argv, "wifibot_base");
  Wifibot _self(ros::NodeHandle(""));

  return 0;
}
