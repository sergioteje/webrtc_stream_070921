/**
* Signals and communications handlers for receiving a video signal from a different peer though a signalling server.
*/

// Websockets connection
var connection = null;

// RTCPeerConnection
var myPeerConnection = null;

// Connection to the signalling server and handling of messages
async function connect() {
  var serverUrl = "ws://localhost:8888?client_id=browser";

  console.log(`Connecting to server: ${serverUrl}`);
  connection = new WebSocket(serverUrl, "json");

  connection.onopen = function(evt) {
	console.log("Connection with the signalling server created");
  };

  connection.onerror = function(evt) {
    console.dir(evt);
  }

  connection.onmessage = async function(evt) {
	
	// The messages will be formatted in JSON
	var msgString;
	
	if (evt.data instanceof Blob) {
        msgString = await evt.data.text();
    } else {
		msgString = evt.data;
    }  
	  
	console.log("Message received: " + msgString);
    var msg = JSON.parse(msgString);

    switch(msg.type) {
      
      case "offer": // SDP offer
        handleOfferMessage(msg);
        break;
		
      case "new-ice-candidate": // New ICE candidate
        handleNewICECandidateMsg(msg);
        break;

      // Unknown message
      default:
        console.log("Unknown message received: " + msg);
    }
  };
}

// Send message to the signalling server
function sendToServer(msg) {
  var msgJSON = JSON.stringify(msg);

  console.log("Sending '" + msg.type + "' message: " + msgJSON);
  connection.send(msgJSON);
}

// Handler for the SDP offer messages
async function handleOfferMessage(msg) {
	
  console.log("Received SDP message");
  
  // Create peer connection if not already created
  if (!myPeerConnection) {
      createPeerConnection();
  }

  // Obtain SDP description
  var desc = new RTCSessionDescription(msg);
  
  if (myPeerConnection.signalingState != "stable") {
      Promise.all([
        myPeerConnection.setLocalDescription({type: "rollback"}),
        myPeerConnection.setRemoteDescription(desc)
      ]);
      return;
  } else {
      console.log ("Setting remote description");
      myPeerConnection.setRemoteDescription(desc);
  }


  // Create and send SDP answer
  console.log("Create SDP answer");
  myPeerConnection.createAnswer().then(function(answer) {
	  return myPeerConnection.setLocalDescription(answer);
	})
	.then(function() {
		sendToServer({
		type: "sdp",
		sdp: myPeerConnection.localDescription
	  });
	})
}

// Create peer connection
function createPeerConnection() {
    myPeerConnection = new RTCPeerConnection({
        iceServers: [
          {
            urls: "stun:stun.stunprotocol.org"
          }
        ]
    });

    // Handlers
    myPeerConnection.onicecandidate = handleICECandidateEvent;
    myPeerConnection.ontrack = handleTrackEvent;
    myPeerConnection.onremovetrack = handleRemoveTrackEvent;
    myPeerConnection.oniceconnectionstatechange = handleICEConnectionStateChangeEvent;
    myPeerConnection.onicegatheringstatechange = handleICEGatheringStateChangeEvent;
    myPeerConnection.onsignalingstatechange = handleSignalingStateChangeEvent;
}

// Send an ICE candidate to the other peer
function handleICECandidateEvent(event) {
    if (event.candidate) {
      console.log("Send ICE candidate: " + event.candidate.candidate);
      sendToServer({
        type: "new-ice-candidate",
        candidate: event.candidate
      });
    }
}

// Handler for the events that occur on the media tracks.
// Modifies the view in order to show the video.
function handleTrackEvent(event) {
    console.log("New track event");
    document.getElementById("received_video").srcObject = event.streams[0];
    document.getElementById("waiting-text").style.visibility = "hidden";
    document.getElementById("bitrate-selector").disabled = false;
}

// Handles new incoming ICE candidate
function handleNewICECandidateMsg(msg) {
  var candidate = new RTCIceCandidate(msg.ice);

  console.log("New ICE candidate received");
  try {
    myPeerConnection.addIceCandidate(candidate)
  } catch(err) {
    reportError(err);
  }
}

// Handler for tracks being removed
function handleRemoveTrackEvent(event) {
  var stream = document.getElementById("received_video").srcObject;
  var trackList = stream.getTracks();

  if (trackList.length == 0) {
    closeStreaming();
  }
}

// Handler for closed peer connection
function handleICEConnectionStateChangeEvent(event) {
  switch(myPeerConnection.iceConnectionState) {
    case "closed":
    case "failed":
      closeStreaming();
      break;
  }
}

// Handler for the changes on the states of ICE candidates
function handleICEGatheringStateChangeEvent(event) {
  // No action required
}

// Handler for the changes on the signalling server
function handleSignalingStateChangeEvent(event) {
  switch(myPeerConnection.signalingState) {
    case "closed":
      closeStreaming();
      break;
  }
};

// Close the video streaming
function closeStreaming() {
  var receivedVideo = document.getElementById("received_video");

  if (myPeerConnection) {
    myPeerConnection.ontrack = null;
    myPeerConnection.onremovetrack = null;
    myPeerConnection.onremovestream = null;
    myPeerConnection.onicecandidate = null;
    myPeerConnection.oniceconnectionstatechange = null;
    myPeerConnection.onsignalingstatechange = null;
    myPeerConnection.onicegatheringstatechange = null;
    myPeerConnection.onnegotiationneeded = null;

    if (receivedVideo.srcObject) {
      receivedVideo.srcObject.getTracks().forEach(track => track.stop());
    }

    myPeerConnection.close();
    myPeerConnection = null;
  }

  receivedVideo.removeAttribute("src");
  receivedVideo.removeAttribute("srcObject");
}

// Initialization of connections
connect();
createPeerConnection();