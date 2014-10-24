/**
Copyright (c) 2014, Konstantinos Chatzilygeroudis
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer 
    in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived 
    from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, 
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE 
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**/

#include <iostream>
#include "pepper_dcm_driver/pepper.h"
#include <alerror/alerror.h>
#include <alcommon/albroker.h>
#include <algorithm>

Nao::Nao(  const std::vector< std::string > &joint_names ):
      is_connected_( false ),
      joint_names_(joint_names)
{

}

Nao::~Nao()
{
    if(is_connected_)
        disconnect();
}

bool Nao::initialize()
{

      // Joints Initialization
    const char* joint[] = {"HeadYaw",
                            "HeadPitch",
                            "LShoulderPitch",
                            "LShoulderRoll",
                            "LElbowYaw",
                            "LElbowRoll",
                            "LWristYaw",
                            "LHand",
                            "RShoulderPitch",
                            "RShoulderRoll",
                            "RElbowYaw",
                            "RElbowRoll",
                            "RWristYaw",
                            "RHand",
                            "KneePitch",
                            "HipRoll",
                            "HipPitch"
                            };
    joint_names_ = vector<string>(joint, end(joint));

    for(vector<string>::iterator it=joint_names_.begin();it!=joint_names_.end();it++)
    {
        joints_names_.push_back("Device/SubDeviceList/"+(*it)+"/Position/Sensor/Value");
    }
    number_of_joints_ = joint_names_.size();

    // DCM Motion Commands Initialization
    try
    {
        // Create Motion Command
        commands_.arraySetSize(4);
        commands_[0] = string("Joints");
        commands_[1] = string("ClearAll");
        commands_[2] = string("time-mixed");
        commands_[3].arraySetSize(number_of_joints_);

        // Create Joints Actuators Alias
        AL::ALValue commandAlias;
        commandAlias.arraySetSize(2);
        commandAlias[0] = string("Joints");
        commandAlias[1].arraySetSize(number_of_joints_);
        for(int i=0;i<number_of_joints_;i++)
        {
            commandAlias[1][i] = string("Device/SubDeviceList/"+joint_names_[i]+"/Position/Actuator/Value");
            commands_[3][i].arraySetSize(1);
            commands_[3][i][0].arraySetSize(2);
        }
        dcm_proxy_.createAlias(commandAlias);

        // Create Joints Hardness Alias
        commandAlias[0] = string("JointsHardness");
        //commandAlias[1].arraySetSize(number_of_joints_-1);
        commandAlias[1].arraySetSize(number_of_joints_);
        int k = 0;
        for(int i=0;i<number_of_joints_;i++)
        {
            commandAlias[1][i+k] = string("Device/SubDeviceList/"+joint_names_[i]+"/Hardness/Actuator/Value");
        }
        dcm_proxy_.createAlias(commandAlias);

    }
    catch(const AL::ALError& e)
    {
        ROS_ERROR("Could not initialize dcm aliases!\n\tTrace: %s",e.what());
        return false;
    }

    return true;
}

bool Nao::initializeControllers(controller_manager::ControllerManager& cm)
{
    if(!initialize())
    {
        ROS_ERROR("Initialization method failed!");
        return false;
    }

    // Initialize Controllers' Interfaces
    joint_angles_.resize(number_of_joints_);
    joint_velocities_.resize(number_of_joints_);
    joint_efforts_.resize(number_of_joints_);
    joint_commands_.resize(number_of_joints_);

    try
    {
        for(int i=0;i<number_of_joints_;i++)
        {
            hardware_interface::JointStateHandle state_handle(joint_names_[i], &joint_angles_[i],
                                                              &joint_velocities_[i], &joint_efforts_[i]);
            jnt_state_interface_.registerHandle(state_handle);

            hardware_interface::JointHandle pos_handle(jnt_state_interface_.getHandle(joint_names_[i]),
                                                       &joint_commands_[i]);
            jnt_pos_interface_.registerHandle(pos_handle);
        }

        registerInterface(&jnt_state_interface_);
        registerInterface(&jnt_pos_interface_);
    }
    catch(const ros::Exception& e)
    {
        ROS_ERROR("Could not initialize hardware interfaces!\n\tTrace: %s",e.what());
        return false;
    }
    ROS_INFO("Nao Module initialized!");
    return true;
}


// ENTRY POINT FROM OUTSIDE
bool Nao::connect(const boost::shared_ptr<AL::ALBroker> &broker,
		  const ros::NodeHandle& nh)
{
    // Initialize ROS nodes
    node_handle_ = nh;

    is_connected_ = false;

    // Load ROS Parameters
    loadParams();

    // Initialize Memory Proxy
    try
    {
        memory_proxy_ = AL::ALMemoryProxy(broker);
    }
    catch (const AL::ALError& e)
    {
        ROS_ERROR("Failed to connect to Memory Proxy!\n\tTrace: %s",e.what());
        return false;
    }

    try
    {
        dcm_proxy_ = AL::DCMProxy(broker);
    }
    catch (const AL::ALError& e)
    {
        ROS_ERROR("Failed to connect to DCM Proxy!\n\tTrace: %s",e.what());
        return false;
    }

    is_connected_ = true;

    // Initialize Controller Manager and Controllers
    manager_ = new controller_manager::ControllerManager( this, node_handle_ );
    if(!initializeControllers(*manager_))
    {
        ROS_ERROR("Could not load controllers!");
        return false;
    }
    ROS_INFO("Controllers successfully loaded!");
    return true;
}

void Nao::disconnect()
{
    if(!is_connected_)
        return;
    is_connected_ = false;
}

void Nao::brokerDisconnected(const string& event_name, const string &broker_name, const string& subscriber_identifier)
{
    if(broker_name == "Nao Driver Broker")
        is_connected_ = false;
}

bool Nao::connected()
{
    return is_connected_;
}

void Nao::loadParams()
{
    ros::NodeHandle n_p("~");
    // Load Server Parameters
    n_p.param("Version", version_, string("V4"));
    n_p.param("BodyType", body_type_, string("H21"));
    n_p.param("TopicQueue", topic_queue_, 50);

    n_p.param("Prefix", prefix_, string("pepper_dcm"));
    prefix_ = prefix_+"/";

    n_p.param("ControllerFrequency", controller_freq_, 15.0);
    n_p.param("JointPrecision", joint_precision_, 0.00174532925);
    n_p.param("OdomFrame", odom_frame_, string("odom"));
}


//void Nao::DCMTimedCommand(const string &key, const AL::ALValue &value, const int &timeOffset, const string &type)
//{
//    try
//    {
//        // Create timed-command
//        AL::ALValue command;
//        command.arraySetSize(3);
//        command[0] = key;
//        command[1] = type;
//        command[2].arraySetSize(1);
//        command[2][0].arraySetSize(2);
//        command[2][0][0] = value;
//        command[2][0][1] = dcm_proxy_.getTime(timeOffset);
//
//        // Execute timed-command
//        dcm_proxy_.set(command);
//    }
//    catch(const AL::ALError& e)
//    {
//        ROS_ERROR("Could not execute DCM timed-command!\n\t%s\n\n\tTrace: %s", key.c_str(), e.what());
//    }
//}

void Nao::DCMAliasTimedCommand(const string &alias, const vector<float> &values, const vector<int> &timeOffsets,
                               const string &type, const string &type2)
{
    try
    {
        // Create Alias timed-command
        AL::ALValue command;
        command.arraySetSize(4);
        command[0] = alias;
        command[1] = type;
        command[2] = type2;
        command[3].arraySetSize(values.size());
        int T = dcm_proxy_.getTime(0);
        for(int i=0;i<values.size();i++)
        {
            command[3][i].arraySetSize(1);
            command[3][i][0].arraySetSize(2);
            command[3][i][0][0] = values[i];
            command[3][i][0][1] = T+timeOffsets[i];
        }

        // Execute Alias timed-command
        dcm_proxy_.setAlias(command);
    }
    catch(const AL::ALError& e)
    {
        ROS_ERROR("Could not execute DCM timed-command!\n\t%s\n\n\tTrace: %s", alias.c_str(), e.what());
    }
}

void Nao::run()
{
    controllerLoop();
}

void Nao::controllerLoop()
{
    static ros::Rate rate(controller_freq_);
    while(ros::ok())
    {
        ros::Time time = ros::Time::now();

        if(!is_connected_)
            break;

        readJoints();

        manager_->update(time,ros::Duration(1.0f/controller_freq_));

        writeJoints();

        rate.sleep();
    }

    ROS_INFO_STREAM("Shutting down coooler Typ");
}

void Nao::readJoints()
{
    vector<float> jointData;
    try
    {
        jointData = memory_proxy_.getListData(joints_names_);
    }
    catch(const AL::ALError& e)
    {
        ROS_ERROR("Could not get joint data from Nao.\n\tTrace: %s",e.what());
        return;
    }

    for(short i = 0; i<jointData.size(); i++)
    {
        joint_angles_[i] = jointData[i];
        // Set commands to the read angles for when no command specified
        joint_commands_[i] = jointData[i];
    }
}

void Nao::writeJoints()
{
    // Update joints only when actual command is issued
    bool changed = false;
    for(int i=0;i<number_of_joints_;i++)
    {
        if(fabs(joint_commands_[i]-joint_angles_[i])>joint_precision_)
        {
            changed = true;
            break;
        }
    }
    // Do not write joints if no change in joint values
    if(!changed)
    {
        return;
    }

    try
    {
        int T = dcm_proxy_.getTime(0);
        for(int i=0;i<number_of_joints_;i++)
        {
            commands_[3][i][0][0] = float(joint_commands_[i]);
	    // whatever this is...
            commands_[3][i][0][1] = T/*+(int)(800.0f/controller_freq_)*/;
        }
        
        dcm_proxy_.setAlias(commands_);
    }
    catch(const AL::ALError& e)
    {
        ROS_ERROR("Could not send joint commands to Nao.\n\tTrace: %s",e.what());
        return;
    }
}

bool Nao::setStiffness(float stiffness)
{
  DCMAliasTimedCommand("JointsHardness",vector<float>(number_of_joints_, stiffness), vector<int>(number_of_joints_,0));
}

