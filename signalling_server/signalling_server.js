/**
* Signalling server for a WebRTC video stream configuration between two peers.
* The source of the video is a GStreamer application.
* The destintation of the video is a browser.
* The server forwards the messages in both directions in order to guarantee
* the communication
*/

var WebSocketServer = require('ws').Server,
    ws = new WebSocketServer({ port: 8888 });
	
// Connections for the browser and the gStreamer application	
var browserConn = null;
var gStreamerConn = null;
 
ws.on('listening', function () {
	console.log("Server started on server 8888...");
});
 
ws.on('connection', (connection, req) => {
	
  var id = req.url.replace('/?client_id=', '')
  
  if (id === 'browser') {
	  browserConn = connection;
	  console.log("Setting Browser connection");
  } else if (id === 'gstreamer') {
	  console.log("Setting GStreamer connection");
	  gStreamerConn = connection;
  } else {
	  console.log("Error: the client is not correct");
	  return null;
  }
	  
  console.log("User connected: " + id);
  
  // Message handler
  connection.on('message', function (message) {	
      console.log("message from user " + id + ": " + message);
	 
		  if (id === 'browser' && gStreamerConn) {
		      gStreamerConn.send(message);
			  console.log("Message forwarded to gStreamer");
		  } else if (id === 'gstreamer' && browserConn) {
			  console.log("Message forwarded to browser: " + message);
			  browserConn.send(message);
	  }	 
  });
  
  // Close connection handler
  connection.on('close', function () {
      console.log("Disconnecting user");
  });
});