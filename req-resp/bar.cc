#include <iostream>
#include <zmq.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sstream>

#include <foo.pb.h>

using std::atoi;

void client_routine(const std::vector<std::string>& servers)
{
    int rc;

    void *context = zmq_ctx_new();
    void *requester = zmq_socket(context, ZMQ_REQ);

    // Connect to all requested servers
    for(const std::string& server : servers)
    {
        std::cout << "Connecting to server " << server << std::endl;
        int rc = zmq_connect(requester, server.c_str());
        assert(rc == 0);
        std::cout << "Connected to server" << std::endl;
    }

    for(int i = 0; i < 10; i++)
    {
        // Initialize the message proto
        Message m;
        m.set_client_name("MyClient");
        m.set_payload("MyPayload");

        // Serialize the proto to string
        std::string serialized_proto;
        if(not m.SerializeToString(&serialized_proto))
        {
            std::cerr << "Error serializing proto" << std::endl;
            exit(1);
        }

        // Initialize our outbound ZMQ message and fill it with the serialized proto
        zmq_msg_t msg_out;
        if(zmq_msg_init_size (&msg_out, serialized_proto.size()))
        {
            std::cerr << "Error initializing outbound message" << std::endl;
            exit(1);
        }
        memcpy ((void *) zmq_msg_data(&msg_out), serialized_proto.c_str(), serialized_proto.size());

        std::cout << "Sending: " << m.DebugString() << " to server" << std::endl;
        // zmq_send (requester, buffer_out.c_str(), buffer_out.size(), 0);
        if(zmq_msg_send(&msg_out, requester, 0) == -1)
        {
            std::cerr << "Error sending zmq msg" << std::endl;
        }

        // Receive the potentially multi-part response, parse, print
        int more;
        size_t more_size = sizeof(more);
        zmq_msg_t msg_in;

        do
        {
            rc = zmq_msg_init(&msg_in);
            assert(rc == 0);

            int len = zmq_msg_recv(&msg_in, requester, 0);
            assert(len != -1);

            rc = zmq_getsockopt (requester, ZMQ_RCVMORE, &more, &more_size);
            assert(rc == 0);

            printf("%.*s", len, (char*)zmq_msg_data(&msg_in));

            zmq_msg_close(&msg_in);
        }
        while(more);
        printf("\n");
    }

    // Close our messages out
    // zmq_msg_close(&msg_out);
    // zmq_msg_close(&msg_in);

    // Shutdown
    zmq_close (requester);
    zmq_ctx_destroy (context);
}

void server_routine(uint16_t port)
{
    //  Socket to talk to clients
    void *context = zmq_ctx_new ();
    void *responder = zmq_socket (context, ZMQ_REP);

    std::stringstream server;
    server << "tcp://*" << ":" << port;
    std::cout << "Attempting to bind to " << server.str() << std::endl;
    int rc = zmq_bind (responder, server.str().c_str());
    assert (rc == 0);
    std::cout << "Bind Success" << std::endl;

    Message m;
    while (1) {
        zmq_msg_t msg_in;
        rc = zmq_msg_init(&msg_in);
        int n = zmq_msg_recv(&msg_in, responder, 0);
        m.ParseFromArray(zmq_msg_data(&msg_in), n);
        std::cout << "Received: " << m.payload() << std::endl;

        sleep (1);          //  Do some 'work'

        /*
         * - Closing messages for sending is not necessary
         * - You must init the size each time you send a message
        */
        zmq_msg_t msg_out;

        rc = zmq_msg_init_size(&msg_out, server.str().size());
        assert(rc == 0);
        memcpy(zmq_msg_data(&msg_out), server.str().c_str(), server.str().size());
        rc = zmq_msg_send(&msg_out, responder, ZMQ_SNDMORE);
        assert(rc != -1);

        rc = zmq_msg_init_size(&msg_out, 1);
        assert(rc == 0);
        ((char*)zmq_msg_data(&msg_out))[0] = 'A';
        rc = zmq_msg_send(&msg_out, responder, ZMQ_SNDMORE);
        assert(rc != -1);

        rc = zmq_msg_init_size(&msg_out, 1);
        assert(rc == 0);
        ((char*)zmq_msg_data(&msg_out))[0] = 'C';
        rc = zmq_msg_send(&msg_out, responder, ZMQ_SNDMORE);
        assert(rc != -1);

        rc = zmq_msg_init_size(&msg_out, 1);
        assert(rc == 0);
        ((char*)zmq_msg_data(&msg_out))[0] = 'K';
        rc = zmq_msg_send(&msg_out, responder, 0);
        assert(rc != -1);
    }
}

int main(int argc, char** argv) {
    if(std::string(argv[1]) == "client")
    {
        std::cout << "Running as client with the following ports:" << std::endl;
        std::cout << "    ";

        std::vector<std::string> servers;

        for(int i = 2; i < argc; i++)
        {
            servers.push_back(std::string(argv[i]));
        }
        
        client_routine(servers);
    }
    else if(std::string(argv[1]) == "server")
    {
        uint16_t port = (uint16_t)atoi(argv[2]);
        std::cout << argv[2] << std::endl;
        std::cout << "Running as server on port " << (int)port << std::endl;
        server_routine(port);
    }
    else
    {
        std::cout << "Usage: client|server" << std::endl;
        exit(1);
    }
        


    return 0;
}

