# Webrtc stream

Implementation of a video streaming system using GStreamer and the WebRTC framework
The goal of this set of applications is to build an environment in which a source of video is transmited from its origin, an application writen in C++ and based on the framework GStreamer, until its destination, a webpage that will show the video. Both parties will make use of the WebRTC specification. For this, a Node.js signalling server will act as an intermediary between them, transporting the necessary messages in both directions.

The project has three main parts:

- webrtc_stream C++ application: it contains the necessary logic for the generation of the video and the handling of the signalling messages coming from the server. In order to do so, it uses GStreamer and its webrtcbin module to provide a video source that can be consumed by other peers.
- signalling_server: it is a Node.js server that serves as a dispatcher for the messages that both sides of the communication (browser and gstreamer) send to eachother. It uses WebSockets for the connections and is also in charge of logging most of the actions that are performed in the system.
- webrtc_client: it includes a HTML webpage that will just show the video that is received and a Javascript implementation to consume the signal messages that the server provides. These will include SDP definitions and ICE candidates.

# Build and execute

It is important to follow the following order when building the system, since it contains different modules that depend on each other:

1. Run Node.js signalling server.

Execute the following commands from /signalling_server :

npm install ws
node signalling_server.js

A message will indicate that the server is ready and listening.

2. Open webrtc_client/webrtc_client.html

The logs of the console will show which messages the client is receiving and sending.

3. Build and run the gstreamer application.

The building part can be done at any moment. A CMake file called CMakeLists.txt has been included in order to facilitate the building of the project.

IMPORTANT: before building it, open the file webrtc_stream/CMakeLists.txt and modify the following lines according to the proper paths of your system (the next example was used in a windows machine):

    set(GSTREAMER_BASE_DIR "C:\\gstreamer\\1.0\\msvc_x86_64")

    target_include_directories(webrtcStream PUBLIC "${GSTREAMER_BASE_DIR}\\include\\glib-2.0\\")
    target_include_directories(webrtcStream PUBLIC "${GSTREAMER_BASE_DIR}\\lib\\glib-2.0\\include\\")
    target_include_directories(webrtcStream PUBLIC "${GSTREAMER_BASE_DIR}\\include\\gstreamer-1.0\\")
    target_include_directories(webrtcStream PUBLIC "${GSTREAMER_BASE_DIR}\\include\\libsoup-2.4\\")
    target_include_directories(webrtcStream PUBLIC "${GSTREAMER_BASE_DIR}\\include\\json-glib-1.0\\")

In order to build the application, execute the following commands from /webrtc_stream

    mkdir build
    cd build
    cmake ..
    cmake --build .

An executable file will have been created in the folder /webrtc_stream/build/Debug. It can be run with

    ./webrtcStream (Linux)
    webrtcStream.exe (Windows)


After these steps, the signalling messages will be exchanged and the data stream will begin.


