cmake_minimum_required (VERSION 3.8)

project ("webrtcStream")

add_executable (webrtcStream "webrtcStream.cpp" "webrtcStream.h")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTLIBS REQUIRED
        gobject-2.0
        glib-2.0
        gstreamer-webrtc-1.0
        gstreamer-sdp-1.0
        libsoup-2.4
        json-glib-1.0)

set(GSTREAMER_BASE_DIR "C:\\gstreamer\\1.0\\msvc_x86_64")
                
target_include_directories(webrtcStream PUBLIC "${GSTREAMER_BASE_DIR}\\include\\glib-2.0\\")
target_include_directories(webrtcStream PUBLIC "${GSTREAMER_BASE_DIR}\\lib\\glib-2.0\\include\\")
target_include_directories(webrtcStream PUBLIC "${GSTREAMER_BASE_DIR}\\include\\gstreamer-1.0\\")
target_include_directories(webrtcStream PUBLIC "${GSTREAMER_BASE_DIR}\\include\\libsoup-2.4\\")
target_include_directories(webrtcStream PUBLIC "${GSTREAMER_BASE_DIR}\\include\\json-glib-1.0\\")
target_link_libraries(webrtcStream ${GST_LINK_LIBRARIES} ${GSTLIBS_LIBRARIES})
target_link_directories(webrtcStream PUBLIC ${GSTLIBS_LIBRARY_DIRS})