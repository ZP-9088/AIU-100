# AIU-100
 The CIG AIU-100 project features hardware developed based on the Jetson AGX Orin platform and software based on Jetson Linux 36.4.3 (jetpack 6.2).
 
# 支持下列摄像头

1. GC3-ISX031-GMSL2-Hxxx
<img width="774" height="1032" alt="image" src="https://github.com/user-attachments/assets/b8a0bd9b-e2db-47ec-ab41-494c18f033ad" />

Clock configuration : /etc/mipifreq as 1400

Test command: gst-launch-1.0 v4l2src device=/dev/video0 ! 'video/x-bayer,format=grgb,width=1920,height=1080,framerate=30/1' ! bayer2rgb ! videoconvert ! autovideosink


2. OX03C10 MAX9295 GMSL2车载相机 quanta-J
<img width="636" height="741" alt="image" src="https://github.com/user-attachments/assets/2a864e7f-46e6-449d-a208-759b4c603b72" />

Clock configuration : /etc/mipifreq as 2500

Test command: gst-launch-1.0 v4l2src device=/dev/video0 ! video/x-raw,format=UYVY,width=1920,height=1536,framerate=30/1 ! videoconvert ! xvimagesink sync=false

3. SENSING, SG8-OX08BC-5300-GMSL2
<img width="789" height="702" alt="image" src="https://github.com/user-attachments/assets/693f00db-7dad-4104-9172-ba8831dd8ba0" />

Clock configuration : /etc/mipifreq as 2500

Test command : v4l2src device=/dev/video0 ! "video/x-raw,width=3840,height=2160" ! videoconvert ! xvimagesink


# 支持驱动AX210NGW型号的WIFI

# 支持驱动CUX3510型号的10G Phy
