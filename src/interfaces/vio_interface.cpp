/*******************************************************************************
 * Copyright 2021 ModalAI Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * 4. The Software is used solely in conjunction with devices provided by
 *    ModalAI Inc.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/
#include <modal_pipe.h>
#include "vio_interface.h"

static void _helper_cb(
    __attribute__((unused))int ch, 
                           char* data, 
                           int bytes, 
                           void* context);

VIOInterface::VIOInterface(
    ros::NodeHandle rosNodeHandle,
    int             baseChannel,
    const char *    name) :
    GenericInterface(rosNodeHandle, baseChannel, NUM_VIO_REQUIRED_CHANNELS, name)
{

    pipe_client_set_simple_helper_cb(m_baseChannel, _helper_cb, this);


}

void VIOInterface::AdvertiseTopics(){

    char frameName[64];

    sprintf(frameName, "/%s/pose", m_pipeName);
    m_posePublisher = ((ros::NodeHandle) m_rosNodeHandle).advertise<geometry_msgs::PoseStamped>(frameName,1);

    m_poseMsg = new geometry_msgs::PoseStamped;
    m_poseMsg->header.frame_id = "map";

    sprintf(frameName, "/%s/odometry", m_pipeName);
    m_odomPublisher = ((ros::NodeHandle) m_rosNodeHandle).advertise<nav_msgs::Odometry>(frameName,1);

    m_odomMsg = new nav_msgs::Odometry;
    m_odomMsg->header.frame_id = "map";
    m_odomMsg->child_frame_id  = "map";

    m_state = ST_AD;

}
void VIOInterface::StartPublishing(){

    char fullName[MODAL_PIPE_MAX_PATH_LEN];
    pipe_client_construct_full_path(m_pipeName, fullName);

    if(pipe_client_init_channel(m_baseChannel, fullName, PIPE_CLIENT_NAME,
                EN_PIPE_CLIENT_SIMPLE_HELPER, VIO_RECOMMENDED_READ_BUF_SIZE)){
        printf("Error opening pipe: %s\n", m_pipeName);
    } else {
        pipe_client_set_disconnect_cb(m_baseChannel, _interface_dc_cb, this);
        m_state = ST_RUNNING;
    }

}
void VIOInterface::StopPublishing(){

    if(m_state == ST_RUNNING){
        pipe_client_close_channel(m_baseChannel);
        m_state = ST_AD;
    }

}

void VIOInterface::Clean(){

    m_posePublisher.shutdown();
    m_odomPublisher.shutdown();
    delete m_poseMsg;
    delete m_odomMsg;
    m_state = ST_CLEAN;

}

int VIOInterface::GetNumClients(){
    return m_posePublisher.getNumSubscribers() + m_odomPublisher.getNumSubscribers();
}

// called when the simple helper has data for us
static void _helper_cb(__attribute__((unused))int ch, char* data, int bytes, void* context)
{

    // validate that the data makes sense
    int n_packets;
    vio_data_t* data_array = modal_vio_validate_pipe_data(data, bytes, &n_packets);
    if(data_array == NULL) return;

    VIOInterface *interface = (VIOInterface *) context;

    if(interface->GetState() != ST_RUNNING) return;

    ros::Publisher posePublisher  = interface->GetPosePublisher();
    ros::Publisher odomPublisher  = interface->GetOdometryPublisher();

    geometry_msgs::PoseStamped*       poseMsg  = interface->GetPoseMsg();
    nav_msgs::Odometry*               odomMsg  = interface->GetOdometryMsg();

    for(int i=0;i<n_packets;i++){

        vio_data_t data = data_array[i];

        poseMsg->header.stamp.fromNSec(data.timestamp_ns);
        odomMsg->header.stamp.fromNSec(data.timestamp_ns);

        // translate VIO pose to ROS pose

        rotation_to_quaternion(data.R_imu_to_vio, (double *)(&(poseMsg->pose.orientation.x)));

        poseMsg->pose.position.x = data.T_imu_wrt_vio[0];
        poseMsg->pose.position.y = data.T_imu_wrt_vio[1];
        poseMsg->pose.position.z = data.T_imu_wrt_vio[2];
        posePublisher.publish(*poseMsg);

        odomMsg->pose.pose = poseMsg->pose;
        odomMsg->twist.twist.linear.x = data.vel_imu_wrt_vio[0];
        odomMsg->twist.twist.linear.y = data.vel_imu_wrt_vio[1];
        odomMsg->twist.twist.linear.z = data.vel_imu_wrt_vio[2];
        odomMsg->twist.twist.angular.x = data.imu_angular_vel[0];
        odomMsg->twist.twist.angular.y = data.imu_angular_vel[1];
        odomMsg->twist.twist.angular.z = data.imu_angular_vel[2];

        odomPublisher.publish(*odomMsg);
    }

    return;
}