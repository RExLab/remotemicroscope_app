// Setup basic express server
var panel = require('./build/Release/panel.node');
var express = require('express');
var app = express();

var server = require('http').createServer(app);
var io = require('socket.io')(server);
var port = process.env.PORT || 80;


server.listen(port, function () {
    console.log('Server listening at port %d', port);

});


var configured = false, authenticated = false;


io.on('connection', function (socket) {
    var password = "";

    socket.on('new connection', function (data) {
        // fazer acesso ao rlms para autenticar
        //password = data.pass;
        console.log('new connection' + data);

        authenticated = true;
        configured = panel.setup();

        if (configured) {
            panel.run();
        } else {
            panel.exit();
            panel.setup();
            panel.run();
        }
        var res = {};
        res.pos = panel.getslot() - 1;
        socket.emit('initial', res);
    });


    socket.on('new message', function (data) {
        var res = {};
        console.log('new message :' + data);
        if (authenticated) {
            var pos = panel.getslot();
            var newpos;
            if (data.key == "right") {
                newpos = ((pos + 1) % 7 == 0) ? 1 : pos + 1;
            } else {
                newpos = ((pos - 1) % 7 == 0) ? 6 : pos - 1;
            }
            console.log("pos: " + newpos);
            panel.update(newpos);
            res.pos = newpos - 1;
            socket.emit('initial', res);

        }

    });


    socket.on('disconnect', function () {
        console.log('disconnected');
        if (authenticated) {
            if (configured) {
                configured = authenticated = false;
                panel.exit();
            } else {
                panel.setup();
                panel.run();
                panel.exit();
            }
        }
    });


});

