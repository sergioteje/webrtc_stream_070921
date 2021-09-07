#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#include <gst/webrtc/webrtc.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <iostream>
#include <string.h>

static GMainLoop* loop;
static GstElement* pipeline, * webrtc;
static GObject* sendChannel;

static SoupWebsocketConnection* wsConnection = NULL;
static const gchar* peerId = "browser";
static const gchar* signallingServerUrl = "ws://localhost:8888?client_id=gstreamer";

static void onOfferCreated(GstPromise* promise, gpointer userData);
static void onNegotiationNeeded(gpointer userData);
static void onIceCandidate(guint mline_index, gchar* candidate, gpointer userData);
static void connectToWebsocketServerAsync(void);
static void onServerConnected(SoupSession* session, GAsyncResult* res, SoupMessage* msg);
static gboolean cleanupAndQuitLoop(const gchar* msg);
static void onServerClosed(SoupWebsocketConnection* conn, gpointer userData);
static void onServerMessage(SoupWebsocketConnection* conn, SoupWebsocketDataType type, GBytes* message, gpointer userData);
static void onOfferReceived(GstSDPMessage* sdp);
static void sendSdpToPeer(GstWebRTCSessionDescription* desc);
static void onOfferSet(GstPromise* promise, gpointer userData);
static void onAnswerCreated(GstPromise* promise, gpointer userData);
static void connectDataChannelSignals(GObject* data_channel);
static void onDataChannel(GstElement* webrtc, GObject* data_channel, gpointer userData);
static void dataChannelOnError(GObject* dc, gpointer userData);
static void dataChannelOnOpen(GObject* dc, gpointer userData);
static void dataChannelOnClose(GObject* dc, gpointer userData);
static void dataChannelOnMessageString(GObject* dc, gchar* str, gpointer userData);
static void onIceGatheringStateNotify(GstElement* webrtcbin, GParamSpec* pspec, gpointer userData);
static void sendIceCandidateMessage(GstElement* webrtc G_GNUC_UNUSED, guint mlineindex, gchar* candidate, gpointer userData);
static gchar* getStringFromJsonObject(JsonObject* object);

int main(int argc, char* argv[]) {

    // Gstreamer initialization
    gst_init(&argc, &argv);

    loop = g_main_loop_new(NULL, FALSE);

    pipeline = gst_parse_launch("videotestsrc pattern=ball ! video/x-raw,framerate=10/1 ! queue ! vp8enc ! rtpvp8pay ! queue ! "
            "application/x-rtp,media=video,payload=96,encoding-name=VP8 ! "
            "webrtcbin name=sendWebRTC", NULL);

    webrtc = gst_bin_get_by_name(GST_BIN(pipeline), "sendWebRTC");
    g_assert(webrtc != NULL);
    g_signal_connect(webrtc, "on-negotiation-needed", G_CALLBACK(onNegotiationNeeded), NULL);
    g_signal_connect(webrtc, "on-ice-candidate", G_CALLBACK(sendIceCandidateMessage), NULL);
    g_signal_connect(webrtc, "notify::ice-gathering-state", G_CALLBACK(onIceGatheringStateNotify), NULL);

    // Connection to the signalling server
    connectToWebsocketServerAsync();

    // Pipeline state is set to READY
    gst_element_set_state(pipeline, GST_STATE_READY);

    // Configuration of the data channel for the output stream 
    g_signal_emit_by_name(webrtc, "create-data-channel", "channel", NULL, &sendChannel);
    if (sendChannel) {
        gst_print("Created data channel\n");
        connectDataChannelSignals(sendChannel);
    }
    else {
        gst_print("Could not create data channel?\n");
    }

    g_signal_connect(webrtc, "on-data-channel", G_CALLBACK(onDataChannel), NULL);

    // Pipeline state is set to PLAYING
    gst_print("Starting pipeline\n");
    gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING);

    g_main_loop_run(loop);

    // Cleanup of allocated resources
    gst_object_unref(webrtc);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);
}

/**
* Connection to the signalling server using websockets with the Soup library
*/
static void connectToWebsocketServerAsync() {
    SoupLogger* logger;
    SoupMessage* message;
    SoupSession* session;
    const char* https_aliases[] = { "wss", NULL };

    session = soup_session_new_with_options(SOUP_SESSION_SSL_STRICT, FALSE,
            SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
            SOUP_SESSION_HTTPS_ALIASES, https_aliases, NULL);

    logger = soup_logger_new(SOUP_LOGGER_LOG_BODY, -1);
    soup_session_add_feature(session, SOUP_SESSION_FEATURE(logger));
    g_object_unref(logger);

    message = soup_message_new(SOUP_METHOD_GET, signallingServerUrl);

    gst_print("Connecting to server...\n");

    /* Once connected, we will register */
    soup_session_websocket_connect_async(session, message, NULL, NULL, NULL,
        (GAsyncReadyCallback)onServerConnected, message);
}

/**
* Handle callback after the connection to the signalling server has been made
*/
static void onServerConnected(SoupSession* session, GAsyncResult* res, SoupMessage* msg) {
    GError* error = NULL;

    wsConnection = soup_session_websocket_connect_finish(session, res, &error);
    if (error) {
        cleanupAndQuitLoop(error->message);
        g_error_free(error);
        return;
    }

    g_assert_nonnull(wsConnection);

    gst_print("Connected to signalling server\n");

    g_signal_connect(wsConnection, "closed", G_CALLBACK(onServerClosed), NULL);
    g_signal_connect(wsConnection, "message", G_CALLBACK(onServerMessage), NULL);
}

/**
* Close connection with the signalling server and free allocated memory
*/
static gboolean cleanupAndQuitLoop(const gchar* msg) {
    if (msg)
        gst_printerr("%s\n", msg);

    if (wsConnection) {
        if (soup_websocket_connection_get_state(wsConnection) == SOUP_WEBSOCKET_STATE_OPEN)
            /* This will call us again */
            soup_websocket_connection_close(wsConnection, 1000, "");
        else
            g_object_unref(wsConnection);
    }

    if (loop) {
        g_main_loop_quit(loop);
        loop = NULL;
    }

    return G_SOURCE_REMOVE;
}

/**
* Handler of the closing of the signalling server
*/
static void onServerClosed(SoupWebsocketConnection* conn G_GNUC_UNUSED, gpointer userData G_GNUC_UNUSED) {
    cleanupAndQuitLoop("Server connection closed");
}

/* Handler of the incoming messages from the signalling server */
static void onServerMessage(SoupWebsocketConnection* conn, SoupWebsocketDataType type, GBytes* message, gpointer userData) {
    gchar* text;

        gsize size;
        gconstpointer data = g_bytes_get_data(message, &size);
        text = g_strndup((gchar*)data, size);
        
        JsonNode* root;
        JsonObject* object, * child;
        JsonParser* parser = json_parser_new();
        if (!json_parser_load_from_data(parser, text, -1, NULL)) {
            gst_printerr("Unknown message '%s', ignoring", text);
            g_object_unref(parser);
            goto out;
        }

        root = json_parser_get_root(parser);
        if (!JSON_NODE_HOLDS_OBJECT(root)) {
            gst_printerr("Unknown json message '%s', ignoring", text);
            g_object_unref(parser);
            goto out;
        }

        object = json_node_get_object(root);

        /* Check type of signalling message */
        if (json_object_has_member(object, "sdp")) { // SDP
            int ret;
            GstSDPMessage* sdp;
            const gchar* text, * sdptype;
            GstWebRTCSessionDescription* answer;

            child = json_object_get_object_member(object, "sdp");

            if (!json_object_has_member(child, "type")) {
                cleanupAndQuitLoop("ERROR: received SDP without 'type'");
                goto out;
            }

            sdptype = json_object_get_string_member(child, "type");
            /* In this example, we create the offer and receive one answer by default,
             * but it's possible to comment out the offer creation and wait for an offer
             * instead, so we handle either here.
             *
             * See tests/examples/webrtcbidirectional.c in gst-plugins-bad for another
             * example how to handle offers from peers and reply with answers using webrtcbin. */
            text = json_object_get_string_member(child, "sdp");
            ret = gst_sdp_message_new(&sdp);
            g_assert_cmphex(ret, == , GST_SDP_OK);
            ret = gst_sdp_message_parse_buffer((guint8*)text, strlen(text), sdp);
            g_assert_cmphex(ret, == , GST_SDP_OK);

            if (g_str_equal(sdptype, "answer")) {
                gst_print("Received answer:\n%s\n", text);
                answer = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER,
                    sdp);
                g_assert_nonnull(answer);

                /* Set remote description on our pipeline */
                {
                    GstPromise* promise = gst_promise_new();
                    g_signal_emit_by_name(webrtc, "set-remote-description", answer,
                        promise);
                    gst_promise_interrupt(promise);
                    gst_promise_unref(promise);
                }
            }
            else {
                gst_print("Received offer:\n%s\n", text);
                onOfferReceived(sdp);
            }

        }
        else if (json_object_has_member(object, "candidate")) { // ICE candidate
            const gchar* candidate;
            gint sdpmlineindex;

            child = json_object_get_object_member(object, "candidate");
            candidate = json_object_get_string_member(child, "candidate");
            sdpmlineindex = json_object_get_int_member(child, "sdpMLineIndex");

            /* Add ice candidate sent by remote peer */
            g_signal_emit_by_name(webrtc, "add-ice-candidate", sdpmlineindex,
                candidate);
        }
        else { // Unknown
            gst_printerr("Ignoring unknown JSON message:\n%s\n", text);
        }
        g_object_unref(parser);

out:
    g_free(text);
}

/**
* Handler for the start of the negotiation, once the connection with the server has been established
*/
static void onNegotiationNeeded(gpointer userData) {

    gst_print("On negotiation needed\n");
    GstPromise* promise = gst_promise_new_with_change_func(onOfferCreated, userData, NULL);;
    g_signal_emit_by_name(webrtc, "create-offer", NULL, promise);
}


/**
* Handler for the creation of the SDP offer to be sent to the peer
*/
static void onOfferCreated(GstPromise* promise, gpointer userData) {
    GstWebRTCSessionDescription* offer = NULL;
    const GstStructure* reply;

    reply = gst_promise_get_reply(promise);
    gst_structure_get(reply, "offer",
        GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
    gst_promise_unref(promise);

    promise = gst_promise_new();
    g_signal_emit_by_name(webrtc, "set-local-description", offer, promise);
    gst_promise_interrupt(promise);
    gst_promise_unref(promise);

    /* Send offer to peer */
    sendSdpToPeer(offer);
    gst_webrtc_session_description_free(offer);
}

/**
* Handler for the incoming offers from the peer
*/
static void onOfferReceived(GstSDPMessage* sdp) {
    GstWebRTCSessionDescription* offer = NULL;
    GstPromise* promise;

    offer = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp);
    g_assert_nonnull(offer);

    /* Set remote description on our pipeline */
    {
        promise = gst_promise_new_with_change_func(onOfferSet, NULL, NULL);
        g_signal_emit_by_name(webrtc, "set-remote-description", offer, promise);
    }
    gst_webrtc_session_description_free(offer);
}

/**
* Handler of the result of setting the remote description
*/
static void onOfferSet(GstPromise* promise, gpointer userData) {
    gst_promise_unref(promise);
    promise = gst_promise_new_with_change_func(onAnswerCreated, NULL, NULL);
    g_signal_emit_by_name(webrtc, "create-answer", NULL, promise);
}

/*
* Handler to create an answer to the received offer
*/
static void onAnswerCreated(GstPromise* promise, gpointer userData) {
    GstWebRTCSessionDescription* answer = NULL;
    const GstStructure* reply;

    reply = gst_promise_get_reply(promise);
    gst_structure_get(reply, "answer",
        GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);
    gst_promise_unref(promise);

    promise = gst_promise_new();
    g_signal_emit_by_name(webrtc, "set-local-description", answer, promise);
    gst_promise_interrupt(promise);
    gst_promise_unref(promise);

    /* Send answer to peer */
    sendSdpToPeer(answer);
    gst_webrtc_session_description_free(answer);
}

/**
* Send SDP offer/answer to the peer through the signalling server
*/
static void sendSdpToPeer(GstWebRTCSessionDescription* desc) {
    gchar* text;
    JsonObject* msg;

    text = gst_sdp_message_as_text(desc->sdp);
    msg = json_object_new();

    if (desc->type == GST_WEBRTC_SDP_TYPE_OFFER) {
        gst_print("Sending offer:\n%s\n", text);
        json_object_set_string_member(msg, "type", "offer");
    }
    else if (desc->type == GST_WEBRTC_SDP_TYPE_ANSWER) {
        gst_print("Sending answer:\n%s\n", text);
        json_object_set_string_member(msg, "type", "answer");
    }
    else {
        g_assert_not_reached();
    }

    json_object_set_string_member(msg, "sdp", text);
    g_free(text);

    text = getStringFromJsonObject(msg);
    json_object_unref(msg);

    soup_websocket_connection_send_text(wsConnection, text);
    g_free(text);
}

/**
* Handler for sending a new ICE candidate to the peer
*/
static void onIceCandidate(guint mline_index, gchar* candidate, gpointer userData)
{
    JsonObject* ice_json;
    JsonObject* ice_data_json;
    gchar* json_string;

    ice_json = json_object_new();
    json_object_set_string_member(ice_json, "type", "ice");

    ice_data_json = json_object_new();
    json_object_set_int_member(ice_data_json, "sdpMLineIndex", mline_index);
    json_object_set_string_member(ice_data_json, "candidate", candidate);
    json_object_set_object_member(ice_json, "data", ice_data_json);

    json_string = getStringFromJsonObject(ice_json);
    json_object_unref(ice_json);

    soup_websocket_connection_send_text(wsConnection, json_string);
    g_free(json_string);
}

/**
* Send an ICE candidate message to the peer
*/
static void sendIceCandidateMessage(GstElement* webrtc G_GNUC_UNUSED, guint mlineindex, gchar* candidate, gpointer userData) {
    gchar* text;
    JsonObject* ice, * msg;

    ice = json_object_new();
    json_object_set_string_member(ice, "candidate", candidate);
    json_object_set_int_member(ice, "sdpMLineIndex", mlineindex);
    msg = json_object_new();
    json_object_set_string_member(msg, "type", "new-ice-candidate");
    json_object_set_object_member(msg, "ice", ice);
    text = getStringFromJsonObject(msg);
    json_object_unref(msg);

    soup_websocket_connection_send_text(wsConnection, text);
    g_free(text);
}


/**
* Print information about the state of the ICE candidates when they change
*/
static void onIceGatheringStateNotify(GstElement* webrtcbin, GParamSpec* pspec, gpointer userData) {
    GstWebRTCICEGatheringState ice_gather_state;
    const gchar* new_state = "unknown";

    g_object_get(webrtcbin, "ice-gathering-state", &ice_gather_state, NULL);
    switch (ice_gather_state) {
    case GST_WEBRTC_ICE_GATHERING_STATE_NEW:
        new_state = "new";
        break;
    case GST_WEBRTC_ICE_GATHERING_STATE_GATHERING:
        new_state = "gathering";
        break;
    case GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE:
        new_state = "complete";
        break;
    }
    gst_print("ICE gathering state changed to %s\n", new_state);
}

/**
* Handler to add new ICE candidates
*/
static void _on_ice_candidate(GstElement* webrtc, guint mlineindex, gchar* candidate, GstElement* other) {
    g_signal_emit_by_name(other, "add-ice-candidate", mlineindex, candidate);
}

/**
* Handler to connect the data channel signals
*/
static void onDataChannel(GstElement* webrtc, GObject* data_channel, gpointer userData) {
    connectDataChannelSignals(data_channel);
}

/**
* Connect data channel signals
*/
static void connectDataChannelSignals(GObject* data_channel) {
    g_signal_connect(data_channel, "on-error", G_CALLBACK(dataChannelOnError), NULL);
    g_signal_connect(data_channel, "on-open", G_CALLBACK(dataChannelOnOpen), NULL);
    g_signal_connect(data_channel, "on-close", G_CALLBACK(dataChannelOnClose), NULL);
    g_signal_connect(data_channel, "on-message-string", G_CALLBACK(dataChannelOnMessageString), NULL);
}

/**
* Handler of the errors related to the data channels
*/
static void dataChannelOnError(GObject* dc, gpointer userData) {
    cleanupAndQuitLoop("Data channel error");
}

/**
* Handler of the result of openning a new data channel
*/
static void dataChannelOnOpen(GObject* dc, gpointer userData) {
    GBytes* bytes = g_bytes_new("data", strlen("data"));
    gst_print("Data channel opened\n");
    g_signal_emit_by_name(dc, "send-string", "Data channel opened");
    g_signal_emit_by_name(dc, "send-data", bytes);
    g_bytes_unref(bytes);
}

/**
* Handler of the closing of a data channel
*/
static void dataChannelOnClose(GObject* dc, gpointer userData) {
    cleanupAndQuitLoop("Data channel closed");
}

/**
* Handler of the messages received on the data channel
*/
static void dataChannelOnMessageString(GObject* dc, gchar* str, gpointer userData) {
    gst_print("Received data channel message: %s\n", str);
}

/**
* Convert a certain Json object to a String
*/
static gchar* getStringFromJsonObject(JsonObject* object) {
    JsonNode* root;
    JsonGenerator* generator;
    gchar* text;

    /* Make it the root node */
    root = json_node_init_object(json_node_alloc(), object);
    generator = json_generator_new();
    json_generator_set_root(generator, root);
    text = json_generator_to_data(generator, NULL);

    /* Release everything */
    g_object_unref(generator);
    json_node_free(root);
    return text;
}


